#include "RetroBackend.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <algorithm>

#ifdef Q_OS_LINUX
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/vt.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#ifndef DRM_IOCTL_SET_MASTER
#define DRM_IOCTL_SET_MASTER   _IO('d', 0x1e)
#define DRM_IOCTL_DROP_MASTER  _IO('d', 0x1f)
#endif
#endif

namespace {
constexpr const char *kModuleId = "com.240mp.retro";
constexpr const char *kRetroMountHelper = "/usr/local/sbin/240mp-retro-mount";
constexpr const char *kRetroCoreHelper = "/usr/local/sbin/240mp-retro-core-control";
constexpr const char *kGameCacheFile = "game-cache.json";
constexpr const char *kRetroNasVirtualPrefix = "retronas-cache:/";
constexpr int kGameCacheVersion = 3;

QString normalizedRemotePath(QString path)
{
    path.replace('\\', '/');
    path = path.trimmed();
    while (path.startsWith('/'))
        path.remove(0, 1);
    while (path.endsWith('/'))
        path.chop(1);
    return path;
}

QString safeRemoteRelativePath(QString path)
{
    path = normalizedRemotePath(path);
    if (path.isEmpty())
        return QString();

    const QStringList segments = path.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    for (const QString &segment : segments) {
        if (segment.isEmpty() || segment == QLatin1String(".")
            || segment == QLatin1String("..")
            || segment.contains(QLatin1Char('\n'))
            || segment.contains(QLatin1Char('\r'))) {
            return QString();
        }
    }
    return segments.join(QLatin1Char('/'));
}

bool lexicalPathIsInside(const QString &child, const QString &parent)
{
    const QString childPath = QDir::cleanPath(QFileInfo(child).absoluteFilePath());
    const QString parentPath = QDir::cleanPath(QFileInfo(parent).absoluteFilePath());
    return childPath == parentPath
        || childPath.startsWith(parentPath + QDir::separator());
}

QString cleanGameTitle(const QString &fileName)
{
    QString title = QFileInfo(fileName).completeBaseName();
    title.replace('_', ' ');
    title.replace(QRegularExpression("\\s+"), " ");
    return title.trimmed();
}

QString escapeRetroValue(QString value)
{
    value.replace('\\', "\\\\");
    value.replace('"', "\\\"");
    return value;
}

QString connectedPiHdmiAudioCard()
{
#ifdef Q_OS_LINUX
    QDir drmDir(QStringLiteral("/sys/class/drm"));
    const QStringList connectors = drmDir.entryList(
        QStringList{QStringLiteral("card*-HDMI-A-*")},
        QDir::Dirs | QDir::NoDotAndDotDot,
        QDir::Name);
    for (const QString &connector : connectors) {
        QFile statusFile(drmDir.absoluteFilePath(connector + QStringLiteral("/status")));
        if (!statusFile.open(QIODevice::ReadOnly))
            continue;
        const QString status = QString::fromLatin1(statusFile.readAll()).trimmed();
        if (status != QStringLiteral("connected"))
            continue;

        QString card;
        if (connector.endsWith(QStringLiteral("HDMI-A-1")))
            card = QStringLiteral("vc4hdmi0");
        else if (connector.endsWith(QStringLiteral("HDMI-A-2")))
            card = QStringLiteral("vc4hdmi1");

        if (!card.isEmpty() && QFile::exists(QStringLiteral("/proc/asound/") + card))
            return card;
    }
#endif
    return QString();
}

QVariantMap loadControllerMapping(const QString &dataRoot)
{
    QFile file(QDir(dataRoot).absoluteFilePath(QStringLiteral("controller-map.json")));
    if (!file.open(QIODevice::ReadOnly))
        return {};

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return {};

    return doc.object().toVariantMap();
}

void writeRetroBinding(QTextStream &out, int player, const QString &retroKey,
                       const QVariantMap &binding)
{
    const QString type = binding.value(QStringLiteral("retroType")).toString();
    const QString value = binding.value(QStringLiteral("retroValue")).toString();
    if (player < 1 || retroKey.isEmpty() || value.isEmpty())
        return;

    const QString prefix = QStringLiteral("input_player%1_%2").arg(player).arg(retroKey);
    if (type == QStringLiteral("axis")) {
        out << prefix << "_axis = \"" << escapeRetroValue(value) << "\"\n";
        out << prefix << "_btn = \"nul\"\n";
    } else {
        out << prefix << "_axis = \"nul\"\n";
        out << prefix << "_btn = \"" << escapeRetroValue(value) << "\"\n";
    }
}

void writeRetroCommandBinding(QTextStream &out, const QString &commandKey,
                              const QVariantMap &binding)
{
    const QString type = binding.value(QStringLiteral("retroType")).toString();
    const QString value = binding.value(QStringLiteral("retroValue")).toString();
    if (commandKey.isEmpty() || value.isEmpty())
        return;

    if (type == QStringLiteral("axis")) {
        out << commandKey << "_axis = \"" << escapeRetroValue(value) << "\"\n";
    } else {
        out << commandKey << "_btn = \"" << escapeRetroValue(value) << "\"\n";
    }
}

bool pathIsInside(const QString &child, const QString &parent)
{
    const QString c = QFileInfo(child).canonicalFilePath();
    const QString p = QFileInfo(parent).canonicalFilePath();
    if (c.isEmpty() || p.isEmpty())
        return false;
    return c == p || c.startsWith(p + QDir::separator());
}

QStringList executableSearchPaths()
{
    QStringList paths = qEnvironmentVariable("PATH").split(':', Qt::SkipEmptyParts);
    const QStringList extra{"/opt/homebrew/bin", "/usr/local/bin", "/usr/bin", "/bin"};
    for (const QString &path : extra) {
        if (!paths.contains(path))
            paths.append(path);
    }
    return paths;
}
}

RetroBackend::RetroBackend(const QString &appRoot, const QString &dataRoot, QObject *parent)
    : QObject(parent)
    , m_appRoot(appRoot)
    , m_dataRoot(dataRoot)
{
    QDir().mkpath(m_dataRoot + "/retroarch");
    QDir().mkpath(mountPoint());
}

RetroBackend::~RetroBackend()
{
    stop_game();
    if (m_retroNasTransferProcess) {
        m_retroNasTransferProcess->disconnect(this);
        if (m_retroNasTransferProcess->state() != QProcess::NotRunning) {
            m_retroNasTransferProcess->kill();
            m_retroNasTransferProcess->waitForFinished(1000);
        }
        delete m_retroNasTransferProcess;
        m_retroNasTransferProcess = nullptr;
    }
    unmountDesktopRetroNas();
    if (m_coreInstallProcess) {
        m_coreInstallProcess->disconnect(this);
        m_coreInstallProcess->deleteLater();
        m_coreInstallProcess = nullptr;
    }
}

bool RetroBackend::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

QVariantMap RetroBackend::moduleConfig() const
{
    QFile file(m_dataRoot + "/config.json");
    if (!file.open(QIODevice::ReadOnly))
        return {};

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return {};

    return doc.object()["modules"].toObject()[QString::fromUtf8(kModuleId)]
        .toObject().toVariantMap();
}

QString RetroBackend::setting(const QString &key, const QString &fallback) const
{
    const QVariantMap cfg = moduleConfig();
    const QString value = cfg.value(key).toString().trimmed();
    return value.isEmpty() ? fallback : value;
}

QString RetroBackend::mountPoint() const
{
    return QDir(m_dataRoot).absoluteFilePath("retronas");
}

QString RetroBackend::gamesRoot() const
{
    const QString localPath = setting(QStringLiteral("local_path"));
    if (!localPath.isEmpty())
        return localPath;

    const QString remotePath = normalizedRemotePath(setting(QStringLiteral("retronas_path"),
                                                           QStringLiteral("games")));
    if (remotePath.isEmpty())
        return mountPoint();
    return QDir(mountPoint()).absoluteFilePath(remotePath);
}

QString RetroBackend::retroarchPath() const
{
    const QString bundled = QDir(m_appRoot).absoluteFilePath(
        QStringLiteral("vendor/retroarch/bin/retroarch"));
    const QFileInfo bundledInfo(bundled);
    if (bundledInfo.exists() && bundledInfo.isExecutable())
        return bundledInfo.absoluteFilePath();

    return QStandardPaths::findExecutable(QStringLiteral("retroarch"), executableSearchPaths());
}

QString RetroBackend::rclonePath() const
{
    const QString bundled = QDir(m_appRoot).absoluteFilePath(
        QStringLiteral("vendor/rclone/bin/rclone"));
    const QFileInfo bundledInfo(bundled);
    if (bundledInfo.exists() && bundledInfo.isExecutable())
        return bundledInfo.absoluteFilePath();

    return QStandardPaths::findExecutable(QStringLiteral("rclone"), executableSearchPaths());
}

QString RetroBackend::rcloneConfigPath() const
{
    return QDir(m_dataRoot).absoluteFilePath(QStringLiteral("retronas-rclone.conf"));
}

QString RetroBackend::desktopRetroNasCacheMarkerPath() const
{
    return QDir(m_dataRoot).absoluteFilePath(QStringLiteral("retronas-cache-mode"));
}

bool RetroBackend::desktopRetroNasCacheMode() const
{
#if defined(TATER_TUBE_STEAM_BUILD) && defined(Q_OS_LINUX)
    return setting(QStringLiteral("local_path")).isEmpty()
        && QFileInfo::exists(desktopRetroNasCacheMarkerPath());
#else
    return false;
#endif
}

void RetroBackend::setDesktopRetroNasCacheMode(bool enabled) const
{
#if defined(TATER_TUBE_STEAM_BUILD) && defined(Q_OS_LINUX)
    if (!enabled) {
        QFile::remove(desktopRetroNasCacheMarkerPath());
        return;
    }

    QSaveFile marker(desktopRetroNasCacheMarkerPath());
    if (!marker.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    marker.write("rclone-copy-cache-v1\n");
    marker.commit();
#else
    Q_UNUSED(enabled)
#endif
}

QString RetroBackend::desktopRetroNasRemoteRoot() const
{
    const QString share = setting(QStringLiteral("retronas_share"),
                                  QStringLiteral("mister"));
    const QString remotePath = normalizedRemotePath(
        setting(QStringLiteral("retronas_path"), QStringLiteral("games")));
    QString root = QStringLiteral("tater-tube-retronas:") + share;
    if (!remotePath.isEmpty())
        root += QLatin1Char('/') + remotePath;
    return root;
}

QString RetroBackend::desktopRetroNasDownloadRoot() const
{
    const QByteArray sourceKey = QStringLiteral("%1\n%2\n%3")
                                     .arg(setting(QStringLiteral("retronas_host")),
                                          setting(QStringLiteral("retronas_share"),
                                                  QStringLiteral("mister")),
                                          setting(QStringLiteral("retronas_path"),
                                                  QStringLiteral("games")))
                                     .toUtf8();
    const QString connectionId = QString::fromLatin1(
        QCryptographicHash::hash(sourceKey, QCryptographicHash::Sha256).toHex().left(16));
    return QDir(m_dataRoot).absoluteFilePath(
        QStringLiteral("retronas-download-cache/") + connectionId);
}

QString RetroBackend::desktopRetroNasVirtualPath(const QString &relativePath) const
{
    const QString clean = safeRemoteRelativePath(relativePath);
    if (clean.isEmpty())
        return QString();
    return QString::fromUtf8(kRetroNasVirtualPrefix)
        + QString::fromLatin1(QUrl::toPercentEncoding(clean, "/"));
}

QString RetroBackend::desktopRetroNasRelativePath(const QString &virtualPath) const
{
    const QString prefix = QString::fromUtf8(kRetroNasVirtualPrefix);
    if (!virtualPath.startsWith(prefix))
        return QString();
    return safeRemoteRelativePath(
        QUrl::fromPercentEncoding(virtualPath.mid(prefix.size()).toLatin1()));
}

bool RetroBackend::buildDesktopRetroNasCatalog(QString *errorOut) const
{
#if defined(TATER_TUBE_STEAM_BUILD) && defined(Q_OS_LINUX)
    const QString bin = rclonePath();
    if (bin.isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("RETRO NETWORK RUNTIME IS NOT INSTALLED");
        return false;
    }

    QProcess listProcess;
    listProcess.setProcessChannelMode(QProcess::SeparateChannels);
    listProcess.start(bin, {
        QStringLiteral("lsjson"),
        desktopRetroNasRemoteRoot(),
        QStringLiteral("--config"), rcloneConfigPath(),
        QStringLiteral("--recursive"),
        QStringLiteral("--files-only"),
        QStringLiteral("--no-mimetype"),
        QStringLiteral("--no-modtime")
    });
    if (!listProcess.waitForStarted(2000)) {
        if (errorOut)
            *errorOut = QStringLiteral("COULD NOT START RETRONAS CATALOG");
        return false;
    }
    if (!listProcess.waitForFinished(120000)) {
        listProcess.kill();
        listProcess.waitForFinished(1000);
        if (errorOut)
            *errorOut = QStringLiteral("RETRONAS CATALOG TIMED OUT");
        return false;
    }
    if (listProcess.exitStatus() != QProcess::NormalExit || listProcess.exitCode() != 0) {
        QString output = QString::fromUtf8(listProcess.readAllStandardError()).trimmed();
        if (output.size() > 400)
            output = output.right(400);
        if (errorOut) {
            *errorOut = output.isEmpty()
                ? QStringLiteral("COULD NOT READ RETRONAS GAME LIST")
                : output.toUpper();
        }
        return false;
    }

    const QByteArray catalogJson = listProcess.readAllStandardOutput();
    if (catalogJson.size() > 50 * 1024 * 1024) {
        if (errorOut)
            *errorOut = QStringLiteral("RETRONAS GAME LIST IS TOO LARGE");
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(catalogJson, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (errorOut)
            *errorOut = QStringLiteral("RETRONAS RETURNED AN INVALID GAME LIST");
        return false;
    }

    QMap<QString, QList<QVariantMap>> gamesBySystem;
    QMap<QString, QString> systemFolders;
    const QList<SystemDef> definitions = systemDefinitions();
    for (const QJsonValue &value : document.array()) {
        const QString relativePath = safeRemoteRelativePath(
            value.toObject().value(QStringLiteral("Path")).toString());
        if (relativePath.isEmpty())
            continue;

        const int slash = relativePath.indexOf(QLatin1Char('/'));
        if (slash <= 0)
            continue;
        const QString topFolder = relativePath.left(slash);
        const QString withinSystem = relativePath.mid(slash + 1);
        const QFileInfo remoteInfo(withinSystem);
        if (remoteInfo.fileName().startsWith(QLatin1Char('.')))
            continue;

        for (const SystemDef &def : definitions) {
            bool folderMatches = false;
            for (const QString &folder : def.folders) {
                if (topFolder.compare(folder, Qt::CaseInsensitive) == 0) {
                    folderMatches = true;
                    break;
                }
            }
            if (!folderMatches || !def.extensions.contains(
                    remoteInfo.suffix(), Qt::CaseInsensitive)) {
                continue;
            }
            if (corePath(def).isEmpty())
                break;

            QVariantMap game;
            game[QStringLiteral("systemId")] = def.id;
            game[QStringLiteral("title")] = cleanGameTitle(remoteInfo.fileName());
            game[QStringLiteral("path")] = desktopRetroNasVirtualPath(relativePath);
            const QString folder = remoteInfo.path() == QLatin1String(".")
                ? QString()
                : remoteInfo.path();
            game[QStringLiteral("folder")] = folder;
            gamesBySystem[def.id].append(game);
            systemFolders[def.id] = topFolder;
            break;
        }
    }

    QVariantList systems;
    QVariantMap cachedGames;
    for (const SystemDef &def : definitions) {
        QList<QVariantMap> games = gamesBySystem.value(def.id);
        if (games.isEmpty())
            continue;
        std::sort(games.begin(), games.end(), [](const QVariantMap &left,
                                                 const QVariantMap &right) {
            return QString::compare(left.value(QStringLiteral("title")).toString(),
                                    right.value(QStringLiteral("title")).toString(),
                                    Qt::CaseInsensitive) < 0;
        });

        QVariantList gameValues;
        for (const QVariantMap &game : games)
            gameValues.append(game);

        QVariantMap system;
        system[QStringLiteral("id")] = def.id;
        system[QStringLiteral("label")] = def.label;
        system[QStringLiteral("path")] = desktopRetroNasVirtualPath(
            systemFolders.value(def.id) + QStringLiteral("/catalog"));
        system[QStringLiteral("core")] = corePath(def);
        system[QStringLiteral("corePackage")] = def.corePackage;
        system[QStringLiteral("gameCount")] = gameValues.size();
        systems.append(system);
        cachedGames.insert(def.id, gameValues);
    }

    QVariantMap cache;
    cache[QStringLiteral("version")] = kGameCacheVersion;
    cache[QStringLiteral("createdAt")] =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    cache[QStringLiteral("gamesRoot")] = gameCacheRootKey();
    cache[QStringLiteral("sourceMode")] = QStringLiteral("rclone-copy-cache");
    cache[QStringLiteral("systems")] = systems;
    cache[QStringLiteral("games")] = cachedGames;
    if (!saveGameCache(cache)) {
        if (errorOut)
            *errorOut = QStringLiteral("COULD NOT SAVE RETRONAS GAME LIST");
        return false;
    }
    qInfo("[RetroBackend] cached %d remote game system(s)", int(systems.size()));
    return true;
#else
    if (errorOut)
        *errorOut = QStringLiteral("STEAM RETRONAS CACHE REQUIRES LINUX");
    return false;
#endif
}

bool RetroBackend::unmountDesktopRetroNas() const
{
#if defined(TATER_TUBE_STEAM_BUILD) && defined(Q_OS_LINUX)
    QString unmountBin = QStandardPaths::findExecutable(
        QStringLiteral("fusermount3"), executableSearchPaths());
    if (unmountBin.isEmpty()) {
        unmountBin = QStandardPaths::findExecutable(
            QStringLiteral("fusermount"), executableSearchPaths());
    }
    if (unmountBin.isEmpty())
        return false;

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(unmountBin, {QStringLiteral("-u"), mountPoint()});
    if (!process.waitForStarted(1000))
        return false;
    if (!process.waitForFinished(5000)) {
        process.kill();
        process.waitForFinished(1000);
        return false;
    }
    if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0)
        return true;

    process.start(unmountBin, {QStringLiteral("-uz"), mountPoint()});
    if (!process.waitForStarted(1000))
        return false;
    if (!process.waitForFinished(5000)) {
        process.kill();
        process.waitForFinished(1000);
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
#else
    return false;
#endif
}

QVariantList RetroBackend::coreInstallStatusOptions() const
{
    QString label = m_coreInstallStatus;
    if (m_coreInstallProcess && m_coreInstallProcess->state() != QProcess::NotRunning) {
        label = QStringLiteral("INSTALLING...");
    } else if (label.isEmpty()) {
#ifdef Q_OS_LINUX
        const QFileInfo helperInfo(QString::fromUtf8(kRetroCoreHelper));
        label = helperInfo.exists() && helperInfo.isExecutable()
            ? QStringLiteral("READY")
            : QStringLiteral("N/A");
#else
        label = QStringLiteral("PI ONLY");
#endif
    }

    QString id = label.toLower();
    id.replace(QRegularExpression("[^a-z0-9]+"), QStringLiteral("-"));

    return QVariantList{
        QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("label"), label}
        }
    };
}

void RetroBackend::emitCoreInstallStatus()
{
    emit dynamicOptionsReady(QStringLiteral("core_install_status"), coreInstallStatusOptions());
}

QString RetroBackend::credentialsFilePath() const
{
    return QDir(m_dataRoot).absoluteFilePath("retronas.credentials");
}

bool RetroBackend::writeCredentialsFile(const QString &username, const QString &password,
                                        QString *pathOut, QString *errorOut) const
{
    const QString user = username.trimmed();
    const QString path = credentialsFilePath();

    if (user.isEmpty()) {
        QFile::remove(path);
        if (pathOut)
            pathOut->clear();
        return true;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorOut)
            *errorOut = file.errorString();
        return false;
    }

    QTextStream out(&file);
    out << "username=" << user << "\n";
    out << "password=" << password << "\n";
    out.flush();

    if (!file.commit()) {
        if (errorOut)
            *errorOut = file.errorString();
        return false;
    }

    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    if (pathOut)
        *pathOut = path;
    return true;
}

QList<RetroBackend::SystemDef> RetroBackend::systemDefinitions() const
{
    return {
        {"atari2600", "Atari 2600",
         {"Atari2600", "Atari 2600", "A2600", "2600"},
         {"a26", "bin", "rom", "zip", "7z"},
         {"stella_libretro.so", "stella2014_libretro.so"},
         "stella"},
        {"atari5200", "Atari 5200",
         {"Atari5200", "Atari 5200", "A5200", "5200"},
         {"a52", "bin", "rom", "zip", "7z"},
         {"atari800_libretro.so"},
         "atari800"},
        {"atari7800", "Atari 7800",
         {"Atari7800", "Atari 7800", "A7800", "7800"},
         {"a78", "bin", "rom", "zip", "7z"},
         {"prosystem_libretro.so"},
         "prosystem"},
        {"lynx", "Atari Lynx",
         {"AtariLynx", "Atari Lynx", "Lynx"},
         {"lnx", "zip", "7z"},
         {"handy_libretro.so"},
         "handy"},
        {"coleco", "ColecoVision",
         {"ColecoVision", "Coleco", "CV"},
         {"col", "rom", "bin", "zip", "7z"},
         {"bluemsx_libretro.so", "gearcoleco_libretro.so"},
         "bluemsx"},
        {"intellivision", "Intellivision",
         {"Intellivision", "INTV"},
         {"int", "bin", "rom", "zip", "7z"},
         {"freeintv_libretro.so"},
         "freeintv"},
        {"odyssey2", "Odyssey2",
         {"Odyssey2", "Odyssey 2", "Videopac"},
         {"o2", "bin", "rom", "zip", "7z"},
         {"o2em_libretro.so"},
         "o2em"},
        {"vectrex", "Vectrex",
         {"Vectrex", "VEC"},
         {"vec", "bin", "rom", "zip", "7z"},
         {"vecx_libretro.so"},
         "vecx"},
        {"msx", "MSX",
         {"MSX", "MSX1", "MSX2"},
         {"rom", "mx1", "mx2", "dsk", "cas", "zip", "7z"},
         {"bluemsx_libretro.so", "fmsx_libretro.so"},
         "bluemsx"},
        {"nes", "NES",
         {"NES", "Famicom", "FC"},
         {"nes", "fds", "zip", "7z"},
         {"nestopia_libretro.so", "fceumm_libretro.so", "quicknes_libretro.so"},
         "nestopia"},
        {"sg1000", "SG-1000",
         {"SG1000", "SG-1000", "Sega SG-1000"},
         {"sg", "zip", "7z"},
         {"gearsystem_libretro.so", "genesis_plus_gx_libretro.so"},
         "gearsystem"},
        {"sms", "Master System",
         {"SMS", "Master System", "Sega Master System"},
         {"sms", "zip", "7z"},
         {"gearsystem_libretro.so", "genesis_plus_gx_libretro.so"},
         "gearsystem"},
        {"gamegear", "Game Gear",
         {"GameGear", "Game Gear", "GG"},
         {"gg", "zip", "7z"},
         {"gearsystem_libretro.so", "genesis_plus_gx_libretro.so"},
         "gearsystem"},
        {"genesis", "Genesis",
         {"Genesis", "MegaDrive", "Mega Drive", "MD"},
         {"md", "gen", "bin", "rom", "zip", "7z"},
         {"blastem_libretro.so", "genesis_plus_gx_libretro.so", "picodrive_libretro.so"},
         "blastem"},
        {"segacd", "Sega CD",
         {"SegaCD", "Sega CD", "MegaCD", "Mega-CD"},
         {"cue", "chd", "iso", "bin", "zip", "7z"},
         {"genesis_plus_gx_libretro.so", "picodrive_libretro.so"},
         "genesis-plus-gx"},
        {"sega32x", "Sega 32X",
         {"32X", "Sega32X", "Sega 32X"},
         {"32x", "bin", "zip", "7z"},
         {"picodrive_libretro.so"},
         "picodrive"},
        {"gb", "Game Boy",
         {"Gameboy", "Game Boy", "GB"},
         {"gb", "zip", "7z"},
         {"gambatte_libretro.so"},
         "gambatte"},
        {"gbc", "Game Boy Color",
         {"Gameboy Color", "Game Boy Color", "GBC"},
         {"gbc", "zip", "7z"},
         {"gambatte_libretro.so"},
         "gambatte"},
        {"gba", "Game Boy Advance",
         {"GBA", "Game Boy Advance"},
         {"gba", "zip", "7z"},
         {"mgba_libretro.so"},
         "mgba"},
        {"pokemonmini", "Pokemon Mini",
         {"PokemonMini", "Pokemon Mini", "PokeMini"},
         {"min", "zip", "7z"},
         {"pokemini_libretro.so"},
         "pokemini"},
        {"snes", "SNES",
         {"SNES", "Super Nintendo", "SFC"},
         {"sfc", "smc", "bs", "fig", "zip", "7z"},
         {"bsnes_libretro.so", "snes9x_libretro.so", "bsnes_mercury_balanced_libretro.so"},
         "bsnes"},
        {"satellaview", "Satellaview",
         {"Satellaview", "BS-X", "BSX"},
         {"bs", "sfc", "smc", "zip", "7z"},
         {"bsnes_libretro.so", "snes9x_libretro.so", "bsnes_mercury_balanced_libretro.so"},
         "bsnes"},
        {"pce", "TurboGrafx-16",
         {"TGFX16", "TurboGrafx16", "TurboGrafx-16", "PC Engine", "PCEngine", "PCE"},
         {"pce", "sgx", "cue", "chd", "zip", "7z"},
         {"mednafen_pce_fast_libretro.so", "mednafen_supergrafx_libretro.so"},
         "beetle-pce-fast"},
        {"supergrafx", "SuperGrafx",
         {"SuperGrafx", "Super Grafx"},
         {"sgx", "pce", "zip", "7z"},
         {"mednafen_supergrafx_libretro.so", "mednafen_pce_fast_libretro.so"},
         "beetle-supergrafx"},
        {"virtualboy", "Virtual Boy",
         {"VirtualBoy", "Virtual Boy", "VB"},
         {"vb", "vboy", "bin", "zip", "7z"},
         {"mednafen_vb_libretro.so"},
         "beetle-vb"},
        {"wonderswan", "WonderSwan",
         {"WonderSwan", "Wonder Swan", "WS"},
         {"ws", "wsc", "zip", "7z"},
         {"mednafen_wswan_libretro.so"},
         "beetle-wswan"},
        {"ngp", "Neo Geo Pocket",
         {"NGP", "NGPC", "NeoGeo Pocket", "Neo Geo Pocket", "NeoGeo Pocket Color", "Neo Geo Pocket Color"},
         {"ngp", "ngc", "zip", "7z"},
         {"race_libretro.so", "mednafen_ngp_libretro.so"},
         "race"},
        {"neogeo", "Neo Geo",
         {"NeoGeo", "Neo Geo", "NEOGEO"},
         {"zip", "7z", "neo", "cue", "chd"},
         {"mame_libretro.so", "geolith_libretro.so", "fbneo_libretro.so",
          "fbalpha2012_neogeo_libretro.so"},
         "mame"},
        {"fbneo", "Arcade FBNeo",
         {"FBNeo", "FBN", "FinalBurnNeo", "Final Burn Neo", "Arcade"},
         {"zip", "7z"},
         {"mame_libretro.so", "fbneo_libretro.so"},
         "mame"},
        {"mame2003", "Arcade MAME 2003",
         {"MAME2003", "MAME 2003", "MAME", "Arcade MAME"},
         {"zip", "7z"},
         {"mame_libretro.so", "mame2003_plus_libretro.so",
          "mame2003_libretro.so", "mame2000_libretro.so"},
         "mame"},
        {"psx", "PlayStation",
         {"PSX", "PS1", "PlayStation", "Playstation"},
         {"cue", "chd", "pbp", "m3u"},
         {"pcsx_rearmed_libretro.so", "mednafen_psx_hw_libretro.so",
          "mednafen_psx_libretro.so", "beetle_psx_hw_libretro.so",
          "beetle_psx_libretro.so", "swanstation_libretro.so"},
         "pcsx-rearmed"},
        {"doom", "Doom",
         {"Doom", "DOOM", "PrBoom"},
         {"wad", "iwad", "zip", "7z"},
         {"prboom_libretro.so"},
         "prboom"},
        {"quake", "Quake",
         {"Quake", "TyrQuake"},
         {"pak", "zip", "7z"},
         {"tyrquake_libretro.so"},
         "tyrquake"}
    };
}

const RetroBackend::SystemDef *RetroBackend::systemById(const QString &systemId) const
{
    static const QList<SystemDef> defs = systemDefinitions();
    for (const SystemDef &def : defs) {
        if (def.id == systemId)
            return &def;
    }
    return nullptr;
}

QString RetroBackend::corePath(const SystemDef &def,
                               const QString &contentPath) const
{
    const QStringList roots{
        QDir(m_appRoot).absoluteFilePath(QStringLiteral("vendor/retroarch/cores")),
        "/usr/lib/aarch64-linux-gnu/libretro",
        "/usr/lib/arm-linux-gnueabihf/libretro",
        "/usr/lib/x86_64-linux-gnu/libretro",
        "/usr/lib/libretro",
        "/usr/local/lib/libretro",
        "/opt/homebrew/lib/libretro"
    };

    QStringList coreNames = def.coreNames;
    if (def.id == QLatin1String("neogeo") && !contentPath.isEmpty()) {
        const QString suffix = QFileInfo(contentPath).suffix().toLower();
        if (suffix == QLatin1String("neo")
            || suffix == QLatin1String("cue")
            || suffix == QLatin1String("chd")) {
            coreNames.removeAll(QStringLiteral("geolith_libretro.so"));
            coreNames.prepend(QStringLiteral("geolith_libretro.so"));
        }
    }

    for (const QString &root : roots) {
        for (const QString &name : coreNames) {
            const QString candidate = QDir(root).absoluteFilePath(name);
            if (QFileInfo::exists(candidate))
                return candidate;
        }
    }

    return QString();
}

QString RetroBackend::systemDirectory(const SystemDef &def) const
{
    const QDir rootDir(gamesRoot());
    for (const QString &folder : def.folders) {
        const QString candidate = rootDir.absoluteFilePath(folder);
        if (QFileInfo(candidate).isDir())
            return candidate;
    }
    return QString();
}

int RetroBackend::gameCount(const SystemDef &def, const QString &dirPath, int limit) const
{
    int count = 0;
    const QSet<QString> extensions(def.extensions.constBegin(), def.extensions.constEnd());
    QDirIterator it(dirPath, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        if (info.fileName().startsWith('.'))
            continue;
        if (extensions.contains(info.suffix().toLower())) {
            ++count;
            if (count >= limit)
                return count;
        }
    }
    return count;
}

QVariantList RetroBackend::availableSystems() const
{
    QVariantList result;
    if (!QDir(gamesRoot()).exists())
        return result;

    for (const SystemDef &def : systemDefinitions()) {
        const QString core = corePath(def);
        if (core.isEmpty())
            continue;

        const QString dir = systemDirectory(def);
        if (dir.isEmpty())
            continue;

        const int count = gameCount(def, dir);
        if (count == 0)
            continue;

        QVariantMap item;
        item["id"] = def.id;
        item["label"] = def.label;
        item["path"] = dir;
        item["core"] = core;
        item["corePackage"] = def.corePackage;
        item["gameCount"] = count;
        result.append(item);
    }

    return result;
}

QVariantList RetroBackend::gamesForSystem(const SystemDef &def) const
{
    QVariantList result;
    const QString dirPath = systemDirectory(def);
    if (dirPath.isEmpty())
        return result;

    const QSet<QString> extensions(def.extensions.constBegin(), def.extensions.constEnd());
    QList<QVariantMap> games;

    QDirIterator it(dirPath, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        if (info.fileName().startsWith('.'))
            continue;

        const QString suffix = info.suffix().toLower();
        if (!extensions.contains(suffix))
            continue;

        QVariantMap item;
        item["systemId"] = def.id;
        item["title"] = cleanGameTitle(info.fileName());
        item["path"] = info.absoluteFilePath();
        item["folder"] = QDir(dirPath).relativeFilePath(info.absolutePath());
        games.append(item);
    }

    std::sort(games.begin(), games.end(), [](const QVariantMap &left, const QVariantMap &right) {
        return QString::compare(left.value("title").toString(),
                                right.value("title").toString(),
                                Qt::CaseInsensitive) < 0;
    });

    for (const QVariantMap &game : games)
        result.append(game);
    return result;
}

QString RetroBackend::gameCachePath() const
{
    return QDir(m_dataRoot).absoluteFilePath(QString::fromUtf8(kGameCacheFile));
}

QString RetroBackend::gameCacheRootKey() const
{
    if (desktopRetroNasCacheMode()) {
        return QStringLiteral("rclone-copy-cache:")
            + QString::fromLatin1(QCryptographicHash::hash(
                  QStringLiteral("%1\n%2").arg(
                      setting(QStringLiteral("retronas_host")),
                      desktopRetroNasRemoteRoot()).toUtf8(),
                  QCryptographicHash::Sha256).toHex());
    }

    const QFileInfo info(gamesRoot());
    const QString canonical = info.exists() ? info.canonicalFilePath() : QString();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

bool RetroBackend::gameCacheIsCurrent(const QVariantMap &cache) const
{
    return cache.value(QStringLiteral("version")).toInt() == kGameCacheVersion
        && cache.value(QStringLiteral("gamesRoot")).toString() == gameCacheRootKey();
}

QVariantMap RetroBackend::loadGameCache() const
{
    QFile file(gameCachePath());
    if (!file.open(QIODevice::ReadOnly))
        return {};

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return {};

    const QVariantMap cache = doc.object().toVariantMap();
    return gameCacheIsCurrent(cache) ? cache : QVariantMap{};
}

bool RetroBackend::saveGameCache(const QVariantMap &cache) const
{
    QDir().mkpath(m_dataRoot);
    QSaveFile file(gameCachePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning("[RetroBackend] could not write game cache: %s",
                 qPrintable(file.errorString()));
        return false;
    }

    file.write(QJsonDocument(QJsonObject::fromVariantMap(cache)).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        qWarning("[RetroBackend] could not save game cache: %s",
                 qPrintable(file.errorString()));
        return false;
    }
    return true;
}

QVariantMap RetroBackend::buildGameCache() const
{
    QVariantMap cache;
    cache[QStringLiteral("version")] = kGameCacheVersion;
    cache[QStringLiteral("createdAt")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    cache[QStringLiteral("gamesRoot")] = gameCacheRootKey();

    QVariantList systems;
    QVariantMap gamesBySystem;

    if (QDir(gamesRoot()).exists()) {
        for (const SystemDef &def : systemDefinitions()) {
            const QString core = corePath(def);
            if (core.isEmpty())
                continue;

            const QString dir = systemDirectory(def);
            if (dir.isEmpty())
                continue;

            const QVariantList games = gamesForSystem(def);
            if (games.isEmpty())
                continue;

            QVariantMap item;
            item[QStringLiteral("id")] = def.id;
            item[QStringLiteral("label")] = def.label;
            item[QStringLiteral("path")] = dir;
            item[QStringLiteral("core")] = core;
            item[QStringLiteral("corePackage")] = def.corePackage;
            item[QStringLiteral("gameCount")] = games.size();
            systems.append(item);
            gamesBySystem.insert(def.id, games);
        }
    }

    cache[QStringLiteral("systems")] = systems;
    cache[QStringLiteral("games")] = gamesBySystem;
    saveGameCache(cache);
    qInfo("[RetroBackend] cached %d game system(s)", int(systems.size()));
    return cache;
}

QVariantMap RetroBackend::ensureGameCache() const
{
    const QVariantMap cache = loadGameCache();
    if (!cache.isEmpty())
        return cache;
    return buildGameCache();
}

QVariantList RetroBackend::cachedSystems() const
{
    return ensureGameCache().value(QStringLiteral("systems")).toList();
}

QVariantList RetroBackend::cachedGamesForSystem(const QString &systemId) const
{
    const QVariantMap gamesBySystem = ensureGameCache().value(QStringLiteral("games")).toMap();
    return gamesBySystem.value(systemId).toList();
}

void RetroBackend::clearGameCache() const
{
    QFile::remove(gameCachePath());
}

QString RetroBackend::get_auth_state()
{
    if (QDir(gamesRoot()).exists())
        return QStringLiteral("authed");
    if (!setting(QStringLiteral("local_path")).isEmpty()
        || !setting(QStringLiteral("retronas_host")).isEmpty())
        return QStringLiteral("authed");
    return QStringLiteral("none");
}

QVariantMap RetroBackend::get_setup_status()
{
    QVariantMap status;
    status["host"] = setting(QStringLiteral("retronas_host"));
    status["share"] = setting(QStringLiteral("retronas_share"), QStringLiteral("mister"));
    status["remotePath"] = setting(QStringLiteral("retronas_path"), QStringLiteral("games"));
    status["username"] = setting(QStringLiteral("retronas_username"));
    status["localPath"] = setting(QStringLiteral("local_path"));
    status["mountPoint"] = mountPoint();
    status["gamesRoot"] = gamesRoot();
    status["accessMode"] = desktopRetroNasCacheMode()
        ? QStringLiteral("download-cache")
        : QStringLiteral("filesystem");
    status["gamesRootExists"] = QDir(gamesRoot()).exists()
        || (desktopRetroNasCacheMode() && !loadGameCache().isEmpty());
    status["retroarchAvailable"] = !retroarchPath().isEmpty();
    status["running"] = isRunning();
    return status;
}

void RetroBackend::mount_retronas(const QString &host,
                                  const QString &share,
                                  const QString &remotePath,
                                  const QString &username,
                                  const QString &password)
{
#if defined(TATER_TUBE_STEAM_BUILD) && defined(Q_OS_LINUX)
    const QString cleanHost = host.trimmed();
    const QString cleanShare = share.trimmed().isEmpty()
        ? QStringLiteral("mister")
        : share.trimmed();
    const QString cleanRemotePath = normalizedRemotePath(remotePath);
    const QString cleanUsername = username.trimmed().isEmpty()
        ? QStringLiteral("guest")
        : username.trimmed();

    const auto containsLineBreak = [](const QString &value) {
        return value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\r'));
    };
    if (cleanHost.isEmpty()) {
        emit mountFinished(false, QStringLiteral("ENTER RETRONAS ADDRESS"));
        return;
    }
    if (containsLineBreak(cleanHost) || containsLineBreak(cleanUsername)
        || containsLineBreak(cleanShare) || cleanShare.contains(QLatin1Char('/'))
        || cleanShare.contains(QLatin1Char('\\')) || cleanShare.contains(QLatin1Char(':'))
        || cleanShare == QLatin1String(".") || cleanShare == QLatin1String("..")) {
        emit mountFinished(false, QStringLiteral("INVALID RETRONAS CONNECTION INFO"));
        return;
    }
    const QStringList remoteSegments = cleanRemotePath.split(
        QLatin1Char('/'), Qt::SkipEmptyParts);
    if (containsLineBreak(cleanRemotePath)
        || remoteSegments.contains(QStringLiteral("."))
        || remoteSegments.contains(QStringLiteral(".."))) {
        emit mountFinished(false, QStringLiteral("INVALID RETRONAS ROM PATH"));
        return;
    }

    const QString bin = rclonePath();
    if (bin.isEmpty()) {
        emit mountFinished(false, QStringLiteral("RETRO NETWORK RUNTIME IS NOT INSTALLED"));
        return;
    }

    QString unmountBin = QStandardPaths::findExecutable(
        QStringLiteral("fusermount3"), executableSearchPaths());
    if (unmountBin.isEmpty()) {
        unmountBin = QStandardPaths::findExecutable(
            QStringLiteral("fusermount"), executableSearchPaths());
    }

    QString obscuredPassword;
    if (password.isEmpty() && QFileInfo::exists(rcloneConfigPath())) {
        QSettings previousConfig(rcloneConfigPath(), QSettings::IniFormat);
        previousConfig.beginGroup(QStringLiteral("tater-tube-retronas"));
        const QString previousHost =
            previousConfig.value(QStringLiteral("host")).toString();
        const QString previousUser =
            previousConfig.value(QStringLiteral("user")).toString();
        if (previousHost == cleanHost && previousUser == cleanUsername) {
            obscuredPassword =
                previousConfig.value(QStringLiteral("pass")).toString();
        }
        previousConfig.endGroup();
    }
    if (!password.isEmpty()) {
        QProcess obscure;
        obscure.setProcessChannelMode(QProcess::SeparateChannels);
        obscure.start(bin, {QStringLiteral("obscure"), QStringLiteral("-")});
        if (!obscure.waitForStarted(2000)) {
            emit mountFinished(false, QStringLiteral("COULD NOT START RETRO NETWORK RUNTIME"));
            return;
        }
        obscure.write(password.toUtf8());
        obscure.write("\n");
        obscure.closeWriteChannel();
        if (!obscure.waitForFinished(5000)
            || obscure.exitStatus() != QProcess::NormalExit
            || obscure.exitCode() != 0) {
            obscure.kill();
            obscure.waitForFinished(1000);
            emit mountFinished(false, QStringLiteral("COULD NOT PROTECT RETRONAS LOGIN"));
            return;
        }
        obscuredPassword = QString::fromUtf8(obscure.readAllStandardOutput()).trimmed();
        if (obscuredPassword.isEmpty()) {
            emit mountFinished(false, QStringLiteral("COULD NOT PROTECT RETRONAS LOGIN"));
            return;
        }
    }

    QSaveFile configFile(rcloneConfigPath());
    if (!configFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit mountFinished(false, QStringLiteral("COULD NOT SAVE RETRONAS LOGIN"));
        return;
    }
    QTextStream config(&configFile);
    config << "[tater-tube-retronas]\n";
    config << "type = smb\n";
    config << "host = " << cleanHost << "\n";
    config << "user = " << cleanUsername << "\n";
    if (!obscuredPassword.isEmpty())
        config << "pass = " << obscuredPassword << "\n";
    config.flush();
    if (!configFile.commit()) {
        emit mountFinished(false, QStringLiteral("COULD NOT SAVE RETRONAS LOGIN"));
        return;
    }
    QFile::setPermissions(rcloneConfigPath(), QFile::ReadOwner | QFile::WriteOwner);

    bool ready = false;
    QString output;
    if (!unmountBin.isEmpty()) {
        unmountDesktopRetroNas();
        QDir().mkpath(mountPoint());
        const QString cachePath = QDir(m_dataRoot).absoluteFilePath(
            QStringLiteral("retronas-vfs-cache"));
        QDir().mkpath(cachePath);

        QStringList args{
            QStringLiteral("mount"),
            QStringLiteral("tater-tube-retronas:") + cleanShare,
            mountPoint(),
            QStringLiteral("--config"), rcloneConfigPath(),
            QStringLiteral("--read-only"),
            QStringLiteral("--vfs-cache-mode"), QStringLiteral("full"),
            QStringLiteral("--cache-dir"), cachePath,
            QStringLiteral("--vfs-cache-max-age"), QStringLiteral("168h"),
            QStringLiteral("--vfs-cache-max-size"), QStringLiteral("20G"),
            QStringLiteral("--dir-cache-time"), QStringLiteral("5m"),
            QStringLiteral("--attr-timeout"), QStringLiteral("1s"),
            QStringLiteral("--daemon"),
            QStringLiteral("--daemon-wait"), QStringLiteral("20s"),
            QStringLiteral("--log-file"),
            QDir(m_dataRoot).absoluteFilePath(QStringLiteral("retronas-rclone.log")),
            QStringLiteral("--log-level"), QStringLiteral("NOTICE")
        };

        QProcess mountProcess;
        mountProcess.setProcessChannelMode(QProcess::MergedChannels);
        mountProcess.start(bin, args);
        if (mountProcess.waitForStarted(2000)
            && mountProcess.waitForFinished(30000)) {
            output = QString::fromUtf8(mountProcess.readAll()).trimmed();
            const bool mounted = mountProcess.exitStatus() == QProcess::NormalExit
                && mountProcess.exitCode() == 0;
            const QString mountedGamesRoot = cleanRemotePath.isEmpty()
                ? mountPoint()
                : QDir(mountPoint()).absoluteFilePath(cleanRemotePath);
            ready = mounted && QDir(mountedGamesRoot).exists();
        } else if (mountProcess.state() != QProcess::NotRunning) {
            mountProcess.kill();
            mountProcess.waitForFinished(1000);
            output = QStringLiteral("RETRONAS MOUNT TIMED OUT");
        }
    }

    if (ready) {
        setDesktopRetroNasCacheMode(false);
        buildGameCache();
        emit authStateChanged();
        emit mountFinished(true, QStringLiteral("RETRONAS READY"));
        return;
    }

    unmountDesktopRetroNas();
    setDesktopRetroNasCacheMode(true);
    QString catalogError;
    const bool catalogReady = buildDesktopRetroNasCatalog(&catalogError);
    if (!catalogReady)
        setDesktopRetroNasCacheMode(false);

    emit authStateChanged();
    emit mountFinished(
        catalogReady,
        catalogReady
            ? QStringLiteral("RETRONAS READY (DOWNLOAD CACHE)")
            : (!catalogError.isEmpty()
                   ? catalogError
                   : (output.isEmpty()
                          ? QStringLiteral("RETRONAS ROM PATH NOT FOUND")
                          : output.toUpper())));
    return;
#elif defined(TATER_TUBE_STEAM_BUILD)
    Q_UNUSED(host)
    Q_UNUSED(share)
    Q_UNUSED(remotePath)
    Q_UNUSED(username)
    Q_UNUSED(password)
    const bool ready = QDir(gamesRoot()).exists();
    if (ready)
        buildGameCache();
    emit mountFinished(ready,
                       ready
                           ? QStringLiteral("LOCAL ROM PATH READY")
                           : QStringLiteral("STEAM RETRONAS REQUIRES LINUX"));
    emit authStateChanged();
    return;
#else
    Q_UNUSED(remotePath)
    const QString cleanHost = host.trimmed();
    const QString cleanShare = share.trimmed().isEmpty() ? QStringLiteral("mister") : share.trimmed();
    if (cleanHost.isEmpty()) {
        emit mountFinished(false, QStringLiteral("ENTER RETRONAS ADDRESS"));
        return;
    }

    QString credPath;
    QString credError;
    if (!writeCredentialsFile(username, password, &credPath, &credError)) {
        emit mountFinished(false, QStringLiteral("COULD NOT SAVE RETRONAS LOGIN: %1").arg(credError));
        return;
    }

#ifndef Q_OS_LINUX
    const bool ready = QDir(gamesRoot()).exists();
    if (ready)
        buildGameCache();
    emit mountFinished(ready,
                       ready
                           ? QStringLiteral("LOCAL ROM PATH READY")
                           : QStringLiteral("RETRO MOUNT IS PI-ONLY"));
    emit authStateChanged();
    return;
#else
    const QFileInfo helperInfo(QString::fromUtf8(kRetroMountHelper));
    if (!helperInfo.exists() || !helperInfo.isExecutable()) {
        emit mountFinished(false, QStringLiteral("RETRO MOUNT HELPER IS NOT INSTALLED"));
        return;
    }

    QDir().mkpath(mountPoint());

    const QFileInfo sudoInfo("/usr/bin/sudo");
    const QString sudoPath = sudoInfo.exists() ? QStringLiteral("/usr/bin/sudo")
                                               : QStringLiteral("sudo");
    QStringList args{
        QStringLiteral("-n"),
        QString::fromUtf8(kRetroMountHelper),
        QStringLiteral("mount"),
        cleanHost,
        cleanShare,
        mountPoint(),
        credPath.isEmpty() ? QStringLiteral("-") : credPath
    };

    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(sudoPath, args);
    if (!process.waitForStarted(2000)) {
        emit mountFinished(false, QStringLiteral("COULD NOT START RETRO MOUNT HELPER"));
        return;
    }

    if (!process.waitForFinished(60000)) {
        process.kill();
        process.waitForFinished(1000);
        emit mountFinished(false, QStringLiteral("RETRO MOUNT TIMED OUT"));
        return;
    }

    const QString output = QString::fromUtf8(process.readAll()).trimmed();
    const bool ok = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    if (ok)
        buildGameCache();
    emit authStateChanged();
    emit mountFinished(ok, ok
        ? QStringLiteral("RETRONAS READY")
        : (output.isEmpty() ? QStringLiteral("RETRO MOUNT FAILED") : output.toUpper()));
#endif
#endif
}

void RetroBackend::load_systems()
{
    emit systemsLoaded(cachedSystems());
}

void RetroBackend::load_games(const QString &systemId)
{
    emit gamesLoaded(cachedGamesForSystem(systemId));
}

void RetroBackend::refresh_game_cache()
{
#if defined(TATER_TUBE_STEAM_BUILD) && defined(Q_OS_LINUX)
    if (desktopRetroNasCacheMode()) {
        QString error;
        if (!buildDesktopRetroNasCatalog(&error)) {
            emit errorOccurred(error.isEmpty()
                                   ? QStringLiteral("COULD NOT REFRESH RETRONAS GAME LIST")
                                   : error);
            return;
        }
        const QVariantMap cache = loadGameCache();
        emit authStateChanged();
        emit systemsLoaded(cache.value(QStringLiteral("systems")).toList());
        return;
    }
#endif
    const QVariantMap cache = buildGameCache();
    emit authStateChanged();
    emit systemsLoaded(cache.value(QStringLiteral("systems")).toList());
}

QVariantList RetroBackend::api_search_games(const QString &query, int limit)
{
    QVariantList result;
    const QString needle = query.trimmed();
    if (needle.isEmpty())
        return result;

    const int maxResults = std::max(1, std::min(limit <= 0 ? 10 : limit, 50));
    for (const QVariant &systemValue : cachedSystems()) {
        const QVariantMap system = systemValue.toMap();
        const QString systemId = system.value(QStringLiteral("id")).toString();
        const QString systemLabel = system.value(QStringLiteral("label")).toString();

        for (const QVariant &gameValue : cachedGamesForSystem(systemId)) {
            QVariantMap game = gameValue.toMap();
            const QString title = game.value(QStringLiteral("title")).toString();
            if (!title.contains(needle, Qt::CaseInsensitive))
                continue;

            const QString path = game.value(QStringLiteral("path")).toString();
            QVariantMap item;
            item[QStringLiteral("id")] = QStringLiteral("game:%1:%2").arg(
                systemId,
                QString::fromLatin1(QUrl::toPercentEncoding(path)));
            item[QStringLiteral("module")] = QStringLiteral("game_center");
            item[QStringLiteral("kind")] = QStringLiteral("game");
            item[QStringLiteral("title")] = title.toUpper();
            item[QStringLiteral("system_id")] = systemId;
            item[QStringLiteral("system")] = systemLabel;
            item[QStringLiteral("path")] = path;
            item[QStringLiteral("folder")] = game.value(QStringLiteral("folder")).toString();
            result.append(item);

            if (result.size() >= maxResults)
                return result;
        }
    }

    return result;
}

QString RetroBackend::writeRetroarchConfig()
{
    const QString retroRoot = QDir(m_dataRoot).absoluteFilePath("retroarch");
    const QString systemRoot = QDir(retroRoot).absoluteFilePath("system");
    QDir().mkpath(retroRoot);
    QDir().mkpath(QDir(retroRoot).absoluteFilePath("saves"));
    QDir().mkpath(QDir(retroRoot).absoluteFilePath("states"));
    QDir().mkpath(systemRoot);
    seedBundledRetroarchSystemFiles(systemRoot);

    const QString cfgPath = QDir(retroRoot).absoluteFilePath("retroarch-240mp.cfg");
    QSaveFile file(cfgPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning("[RetroBackend] could not write RetroArch config: %s",
                 qPrintable(file.errorString()));
        return QString();
    }

    QTextStream out(&file);
    out << "video_fullscreen = \"true\"\n";
    out << "pause_nonactive = \"false\"\n";
    out << "quit_press_twice = \"false\"\n";
    out << "input_exit_emulator = \"escape\"\n";
    out << "input_exit_emulator_axis = \"nul\"\n";
    out << "input_exit_emulator_btn = \"nul\"\n";
    out << "input_driver = \"udev\"\n";
    out << "input_autodetect_enable = \"false\"\n";
    for (int player = 1; player <= 4; ++player) {
        out << "input_libretro_device_p" << player << " = \"1\"\n";
        out << "input_device_p" << player << " = \"1\"\n";
    }
    out << "menu_show_start_screen = \"false\"\n";
    out << "savestate_directory = \"" << escapeRetroValue(QDir(retroRoot).absoluteFilePath("states")) << "\"\n";
    out << "savefile_directory = \"" << escapeRetroValue(QDir(retroRoot).absoluteFilePath("saves")) << "\"\n";
    out << "system_directory = \"" << escapeRetroValue(systemRoot) << "\"\n";
    out << "assets_directory = \"" << escapeRetroValue(QDir(retroRoot).absoluteFilePath("assets")) << "\"\n";

    const QVariantMap controllerMapping = loadControllerMapping(m_dataRoot);
    const QVariantMap bindings = controllerMapping.value(QStringLiteral("bindings")).toMap();
    if (!bindings.isEmpty()) {
        out << "input_joypad_driver = \"udev\"\n";

        for (int player = 1; player <= 4; ++player) {
            out << "input_player" << player << "_joypad_index = \"" << (player - 1) << "\"\n";

            const auto writeMapped = [&](const QString &retroKey, const QString &bindingKey) {
                writeRetroBinding(out, player, retroKey, bindings.value(bindingKey).toMap());
            };

            writeMapped(QStringLiteral("up"), QStringLiteral("up"));
            writeMapped(QStringLiteral("down"), QStringLiteral("down"));
            writeMapped(QStringLiteral("left"), QStringLiteral("left"));
            writeMapped(QStringLiteral("right"), QStringLiteral("right"));
            writeMapped(QStringLiteral("b"), QStringLiteral("a"));
            writeMapped(QStringLiteral("a"), QStringLiteral("b"));
            writeMapped(QStringLiteral("y"), QStringLiteral("x"));
            writeMapped(QStringLiteral("x"), QStringLiteral("y"));
            writeMapped(QStringLiteral("select"), QStringLiteral("select"));
            writeMapped(QStringLiteral("start"), QStringLiteral("start"));
            writeMapped(QStringLiteral("l"), QStringLiteral("l"));
            writeMapped(QStringLiteral("r"), QStringLiteral("r"));
            writeMapped(QStringLiteral("l2"), QStringLiteral("l2"));
            writeMapped(QStringLiteral("r2"), QStringLiteral("r2"));
            writeMapped(QStringLiteral("l3"), QStringLiteral("l3"));
            writeMapped(QStringLiteral("r3"), QStringLiteral("r3"));
        }
    }

    if (detectHeadlessMode()) {
        out << "audio_driver = \"alsa\"\n";
        const QString hdmiCard = connectedPiHdmiAudioCard();
        if (!hdmiCard.isEmpty())
            out << "audio_device = \"sysdefault:CARD=" << hdmiCard << "\"\n";
        else if (hasPiHeadphonesAudioDevice())
            out << "audio_device = \"sysdefault:CARD=Headphones\"\n";
    }

    out.flush();
    if (!file.commit()) {
        qWarning("[RetroBackend] could not commit RetroArch config: %s",
                 qPrintable(file.errorString()));
        return QString();
    }
    return cfgPath;
}

void RetroBackend::seedBundledRetroarchSystemFiles(
    const QString &destinationRoot) const
{
    const QString sourceRoot = QDir(m_appRoot).absoluteFilePath(
        QStringLiteral("vendor/retroarch/system"));
    if (!QDir(sourceRoot).exists())
        return;

    const QDir sourceDirectory(sourceRoot);
    QDirIterator it(sourceRoot, QDir::Files | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString sourcePath = it.next();
        const QString relativePath = sourceDirectory.relativeFilePath(sourcePath);
        const QString destinationPath =
            QDir(destinationRoot).absoluteFilePath(relativePath);
        if (QFileInfo::exists(destinationPath))
            continue;

        QDir().mkpath(QFileInfo(destinationPath).absolutePath());
        if (!QFile::copy(sourcePath, destinationPath)) {
            qWarning("[RetroBackend] could not seed RetroArch system file: %s",
                     qPrintable(relativePath));
        }
    }
}

void RetroBackend::pruneDesktopRetroNasDownloadCache() const
{
#if defined(TATER_TUBE_STEAM_BUILD) && defined(Q_OS_LINUX)
    constexpr qint64 maxBytes = 20LL * 1024LL * 1024LL * 1024LL;
    constexpr qint64 targetBytes = 18LL * 1024LL * 1024LL * 1024LL;
    const QString root = QDir(m_dataRoot).absoluteFilePath(
        QStringLiteral("retronas-download-cache"));
    if (!QDir(root).exists())
        return;

    QList<QFileInfo> files;
    qint64 totalBytes = 0;
    QDirIterator it(root, QDir::Files | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        files.append(info);
        totalBytes += info.size();
    }
    if (totalBytes <= maxBytes)
        return;

    std::sort(files.begin(), files.end(), [](const QFileInfo &left,
                                             const QFileInfo &right) {
        return left.lastModified() < right.lastModified();
    });
    for (const QFileInfo &info : files) {
        if (totalBytes <= targetBytes)
            break;
        const qint64 size = info.size();
        if (QFile::remove(info.absoluteFilePath()))
            totalBytes -= size;
    }
#endif
}

void RetroBackend::queueDesktopRetroNasCompanions(const QString &relativePath,
                                                   const QString &localPath)
{
#if defined(TATER_TUBE_STEAM_BUILD) && defined(Q_OS_LINUX)
    const QString suffix = QFileInfo(localPath).suffix().toLower();
    if (suffix != QLatin1String("cue") && suffix != QLatin1String("m3u"))
        return;

    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text) || file.size() > 1024 * 1024)
        return;

    const QString baseDir = QFileInfo(relativePath).path();
    const QString text = QString::fromUtf8(file.readAll());
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                         Qt::SkipEmptyParts);
    const QRegularExpression cueFile(
        QStringLiteral("^\\s*FILE\\s+(?:\"([^\"]+)\"|(\\S+))"),
        QRegularExpression::CaseInsensitiveOption);

    for (QString reference : lines) {
        reference = reference.trimmed();
        if (suffix == QLatin1String("cue")) {
            const QRegularExpressionMatch match = cueFile.match(reference);
            if (!match.hasMatch())
                continue;
            reference = match.captured(1).isEmpty()
                ? match.captured(2)
                : match.captured(1);
        } else if (reference.isEmpty() || reference.startsWith(QLatin1Char('#'))) {
            continue;
        }

        reference.replace('\\', '/');
        const QString resolved = safeRemoteRelativePath(
            QDir(baseDir).filePath(reference));
        if (resolved.isEmpty() || m_retroNasQueuedPaths.contains(resolved))
            continue;
        m_retroNasQueuedPaths.insert(resolved);
        m_retroNasTransferQueue.append(resolved);
    }
#else
    Q_UNUSED(relativePath)
    Q_UNUSED(localPath)
#endif
}

void RetroBackend::startDesktopRetroNasDownload(const QString &systemId,
                                                 const QString &relativePath)
{
#if defined(TATER_TUBE_STEAM_BUILD) && defined(Q_OS_LINUX)
    const QString clean = safeRemoteRelativePath(relativePath);
    if (clean.isEmpty()) {
        emit errorOccurred(QStringLiteral("INVALID RETRONAS ROM PATH"));
        return;
    }
    if (rclonePath().isEmpty() || !QFileInfo::exists(rcloneConfigPath())) {
        emit errorOccurred(QStringLiteral("RETRO NETWORK RUNTIME IS NOT READY"));
        return;
    }

    stop_game();
    pruneDesktopRetroNasDownloadCache();
    m_pendingRemoteSystemId = systemId;
    m_pendingRemotePrimaryPath = clean;
    m_activeRemoteTransferPath.clear();
    m_retroNasTransferQueue = {clean};
    m_retroNasQueuedPaths = {clean};
    startNextDesktopRetroNasDownload();
#else
    Q_UNUSED(systemId)
    Q_UNUSED(relativePath)
    emit errorOccurred(QStringLiteral("STEAM RETRONAS CACHE REQUIRES LINUX"));
#endif
}

void RetroBackend::startNextDesktopRetroNasDownload()
{
#if defined(TATER_TUBE_STEAM_BUILD) && defined(Q_OS_LINUX)
    if (m_retroNasTransferProcess
        && m_retroNasTransferProcess->state() != QProcess::NotRunning) {
        return;
    }
    if (m_retroNasTransferQueue.isEmpty()) {
        finishDesktopRetroNasDownload(true);
        return;
    }

    const QString relativePath = m_retroNasTransferQueue.takeFirst();
    const QString localPath = QDir(desktopRetroNasDownloadRoot()).absoluteFilePath(
        relativePath);
    if (!lexicalPathIsInside(localPath, desktopRetroNasDownloadRoot())) {
        finishDesktopRetroNasDownload(false,
                                      QStringLiteral("INVALID RETRONAS CACHE PATH"));
        return;
    }
    QDir().mkpath(QFileInfo(localPath).absolutePath());

    if (QFileInfo(localPath).isFile() && QFileInfo(localPath).size() > 0) {
        queueDesktopRetroNasCompanions(relativePath, localPath);
        QTimer::singleShot(0, this, &RetroBackend::startNextDesktopRetroNasDownload);
        return;
    }

    const QString source = desktopRetroNasRemoteRoot()
        + QLatin1Char('/') + relativePath;
    m_activeRemoteTransferPath = relativePath;
    m_retroNasTransferProcess = new QProcess(this);
    m_retroNasTransferProcess->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_retroNasTransferProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this, relativePath, localPath](int exitCode,
                                            QProcess::ExitStatus exitStatus) {
        QProcess *finished = m_retroNasTransferProcess;
        const QString output = finished
            ? QString::fromUtf8(finished->readAll()).trimmed()
            : QString();
        if (finished)
            finished->deleteLater();
        m_retroNasTransferProcess = nullptr;
        m_activeRemoteTransferPath.clear();

        if (exitStatus != QProcess::NormalExit || exitCode != 0
            || !QFileInfo(localPath).isFile()) {
            QString error = output;
            if (error.size() > 400)
                error = error.right(400);
            finishDesktopRetroNasDownload(
                false,
                error.isEmpty()
                    ? QStringLiteral("COULD NOT DOWNLOAD RETRONAS ROM")
                    : error.toUpper());
            return;
        }

        queueDesktopRetroNasCompanions(relativePath, localPath);
        startNextDesktopRetroNasDownload();
    });

    m_retroNasTransferProcess->start(rclonePath(), {
        QStringLiteral("copyto"),
        source,
        localPath,
        QStringLiteral("--config"), rcloneConfigPath(),
        QStringLiteral("--no-traverse"),
        QStringLiteral("--retries"), QStringLiteral("3"),
        QStringLiteral("--low-level-retries"), QStringLiteral("10"),
        QStringLiteral("--contimeout"), QStringLiteral("10s"),
        QStringLiteral("--timeout"), QStringLiteral("5m")
    });
    if (!m_retroNasTransferProcess->waitForStarted(2000)) {
        m_retroNasTransferProcess->deleteLater();
        m_retroNasTransferProcess = nullptr;
        m_activeRemoteTransferPath.clear();
        finishDesktopRetroNasDownload(
            false, QStringLiteral("COULD NOT START RETRONAS DOWNLOAD"));
    } else {
        qInfo("[RetroBackend] caching remote game file: %s",
              qPrintable(relativePath));
    }
#endif
}

void RetroBackend::finishDesktopRetroNasDownload(bool ok, const QString &error)
{
#if defined(TATER_TUBE_STEAM_BUILD) && defined(Q_OS_LINUX)
    const QString systemId = m_pendingRemoteSystemId;
    const QString primaryPath = m_pendingRemotePrimaryPath;
    m_retroNasTransferQueue.clear();
    m_retroNasQueuedPaths.clear();
    m_pendingRemoteSystemId.clear();
    m_pendingRemotePrimaryPath.clear();
    m_activeRemoteTransferPath.clear();

    if (!ok) {
        emit errorOccurred(error.isEmpty()
                               ? QStringLiteral("RETRONAS DOWNLOAD FAILED")
                               : error);
        return;
    }

    const QString localPath = QDir(desktopRetroNasDownloadRoot()).absoluteFilePath(
        primaryPath);
    launchLocalGame(systemId, localPath);
#else
    Q_UNUSED(ok)
    Q_UNUSED(error)
#endif
}

void RetroBackend::launch_game(const QString &systemId, const QString &path)
{
    const QString remoteRelativePath = desktopRetroNasRelativePath(path);
    if (!remoteRelativePath.isEmpty()) {
        startDesktopRetroNasDownload(systemId, remoteRelativePath);
        return;
    }
    launchLocalGame(systemId, path);
}

void RetroBackend::launchLocalGame(const QString &systemId, const QString &path)
{
    const SystemDef *def = systemById(systemId);
    if (!def) {
        emit errorOccurred(QStringLiteral("UNKNOWN RETRO SYSTEM"));
        return;
    }

    const QString bin = retroarchPath();
    if (bin.isEmpty()) {
        emit errorOccurred(QStringLiteral("RETROARCH IS NOT INSTALLED"));
        return;
    }

    const QString core = corePath(*def, path);
    if (core.isEmpty()) {
        emit errorOccurred(QStringLiteral("MISSING RETRO CORE: %1").arg(def->corePackage));
        return;
    }

    const QString sysDir = systemDirectory(*def);
    const bool cachedRemoteGame = desktopRetroNasCacheMode()
        && lexicalPathIsInside(path, desktopRetroNasDownloadRoot());
    if (!cachedRemoteGame && (sysDir.isEmpty() || !pathIsInside(path, sysDir))) {
        emit errorOccurred(QStringLiteral("ROM PATH IS NOT IN THE SYSTEM FOLDER"));
        return;
    }

    const QFileInfo gameInfo(path);
    if (!gameInfo.exists() || !gameInfo.isFile()) {
        emit errorOccurred(QStringLiteral("ROM FILE NOT FOUND"));
        return;
    }

    stop_game();

    const QString cfgPath = writeRetroarchConfig();
    if (cfgPath.isEmpty()) {
        emit errorOccurred(QStringLiteral("COULD NOT WRITE RETROARCH CONFIG"));
        return;
    }

    m_currentTitle = cleanGameTitle(gameInfo.fileName());
    qInfo("[RetroBackend] selected game: system=%s title=%s path=%s",
          qPrintable(systemId),
          qPrintable(m_currentTitle),
          qPrintable(gameInfo.canonicalFilePath()));
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyRead, this, [this]() {
        const QByteArray out = m_process ? m_process->readAll() : QByteArray();
        if (!out.isEmpty())
            qWarning("[retroarch] %s", out.trimmed().constData());
    });
    connect(m_process, &QProcess::started, this, [this]() {
        emit runningChanged(true);
        emit gameStarted(m_currentTitle);
    });
    connect(m_process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &RetroBackend::onProcessFinished);

    QStringList args{
        QStringLiteral("--config"), cfgPath,
        QStringLiteral("--verbose"),
        QStringLiteral("-L"), core,
        gameInfo.canonicalFilePath()
    };

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("APP_ROOT"), m_appRoot);
    env.insert(QStringLiteral("XDG_CONFIG_HOME"), QDir(m_dataRoot).absoluteFilePath("retroarch/config"));
    env.insert(QStringLiteral("XDG_DATA_HOME"), QDir(m_dataRoot).absoluteFilePath("retroarch/data"));
    env.insert(QStringLiteral("XDG_CACHE_HOME"), QDir(m_dataRoot).absoluteFilePath("retroarch/cache"));

    m_headlessMode = detectHeadlessMode();
    if (m_headlessMode) {
        env.remove(QStringLiteral("DISPLAY"));
        env.remove(QStringLiteral("WAYLAND_DISPLAY"));
        prepareHeadlessLaunch();
    } else {
        env.remove(QStringLiteral("WAYLAND_DISPLAY"));
    }
    m_process->setProcessEnvironment(env);

    qInfo("[RetroBackend] launching RetroArch: %s %s",
          qPrintable(bin), qPrintable(args.join(' ')));
    m_process->start(bin, args);
}

void RetroBackend::stop_game()
{
    if (m_retroNasTransferProcess) {
        m_retroNasTransferProcess->disconnect(this);
        if (m_retroNasTransferProcess->state() != QProcess::NotRunning) {
            m_retroNasTransferProcess->kill();
            m_retroNasTransferProcess->waitForFinished(1000);
        }
        m_retroNasTransferProcess->deleteLater();
        m_retroNasTransferProcess = nullptr;
        m_retroNasTransferQueue.clear();
        m_retroNasQueuedPaths.clear();
        m_pendingRemoteSystemId.clear();
        m_pendingRemotePrimaryPath.clear();
        m_activeRemoteTransferPath.clear();
    }

    if (!m_process)
        return;

    if (m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(2500)) {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
    }
}

void RetroBackend::get_retro_system_options()
{
    QVariantList options;
    for (const QVariant &item : cachedSystems()) {
        const QVariantMap system = item.toMap();
        QVariantMap option;
        option["id"] = system.value("id");
        option["label"] = system.value("label");
        options.append(option);
    }
    emit dynamicOptionsReady(QStringLiteral("systems"), options);
}

void RetroBackend::load_core_install_status_options()
{
    emitCoreInstallStatus();
}

void RetroBackend::install_game_cores()
{
#ifdef TATER_TUBE_STEAM_BUILD
    m_coreInstallStatus = QStringLiteral("MANAGED BY STEAM");
    emitCoreInstallStatus();
    return;
#elif !defined(Q_OS_LINUX)
    m_coreInstallStatus = QStringLiteral("PI ONLY");
    emitCoreInstallStatus();
    return;
#else
    if (m_coreInstallProcess && m_coreInstallProcess->state() != QProcess::NotRunning) {
        m_coreInstallStatus = QStringLiteral("INSTALLING...");
        emitCoreInstallStatus();
        return;
    }

    const QFileInfo helperInfo(QString::fromUtf8(kRetroCoreHelper));
    if (!helperInfo.exists() || !helperInfo.isExecutable()) {
        m_coreInstallStatus = QStringLiteral("HELPER N/A");
        emitCoreInstallStatus();
        return;
    }

    const QFileInfo sudoInfo(QStringLiteral("/usr/bin/sudo"));
    const QString sudoPath = sudoInfo.exists() ? QStringLiteral("/usr/bin/sudo")
                                               : QStringLiteral("sudo");

    if (m_coreInstallProcess) {
        m_coreInstallProcess->deleteLater();
        m_coreInstallProcess = nullptr;
    }

    m_coreInstallProcess = new QProcess(this);
    m_coreInstallProcess->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_coreInstallProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &RetroBackend::onCoreInstallFinished);
    m_coreInstallProcess->start(sudoPath, {
        QStringLiteral("-n"),
        QString::fromUtf8(kRetroCoreHelper),
        QStringLiteral("install")
    });

    if (!m_coreInstallProcess->waitForStarted(2000)) {
        m_coreInstallStatus = QStringLiteral("START FAILED");
        m_coreInstallProcess->deleteLater();
        m_coreInstallProcess = nullptr;
        emitCoreInstallStatus();
        return;
    }

    m_coreInstallStatus = QStringLiteral("INSTALLING...");
    emitCoreInstallStatus();
#endif
}

void RetroBackend::onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value)
{
    Q_UNUSED(value)
    if (moduleId != QString::fromUtf8(kModuleId))
        return;
    if (key == QLatin1String("local_path")
        || key == QLatin1String("retronas_host")
        || key == QLatin1String("retronas_share")
        || key == QLatin1String("retronas_path")) {
        clearGameCache();
        emit authStateChanged();
    }
}

void RetroBackend::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    QProcess *finished = m_process;
    if (finished) {
        const QByteArray remaining = finished->readAll();
        if (!remaining.isEmpty())
            qWarning("[retroarch] %s", remaining.trimmed().constData());
        finished->deleteLater();
        m_process = nullptr;
    }

    restoreHeadlessDisplay();
    emit runningChanged(false);
    emit gameFinished();

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        qWarning("[RetroBackend] RetroArch exited with code %d", exitCode);
        emit errorOccurred(QStringLiteral("RETROARCH PLAYBACK FAILED"));
    }
}

void RetroBackend::onCoreInstallFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    QProcess *finished = m_coreInstallProcess;
    if (finished) {
        const QByteArray remaining = finished->readAll();
        if (!remaining.isEmpty())
            qInfo("[retro-core] %s", remaining.trimmed().constData());
        finished->deleteLater();
        m_coreInstallProcess = nullptr;
    }

    const bool ok = exitStatus == QProcess::NormalExit && exitCode == 0;
    m_coreInstallStatus = ok ? QStringLiteral("DONE") : QStringLiteral("FAILED");
    if (ok) {
        clearGameCache();
        emit authStateChanged();
    }
    emitCoreInstallStatus();
}

bool RetroBackend::detectHeadlessMode() const
{
#ifdef Q_OS_LINUX
    return qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
#else
    return false;
#endif
}

bool RetroBackend::hasPiHeadphonesAudioDevice() const
{
#ifdef Q_OS_LINUX
    QFile cards(QStringLiteral("/proc/asound/cards"));
    if (!cards.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    return QString::fromUtf8(cards.readAll()).contains(QStringLiteral("Headphones"),
                                                       Qt::CaseInsensitive);
#else
    return false;
#endif
}

int RetroBackend::getActiveVt() const
{
#ifdef Q_OS_LINUX
    QFile f("/sys/class/tty/tty0/active");
    if (!f.open(QIODevice::ReadOnly))
        return -1;
    const QString name = QString::fromLatin1(f.readAll()).trimmed();
    bool ok = false;
    const int n = name.mid(3).toInt(&ok);
    return ok ? n : -1;
#else
    return -1;
#endif
}

int RetroBackend::findFreeVt() const
{
#ifdef Q_OS_LINUX
    bool envOk = false;
    int configuredVt = qEnvironmentVariableIntValue("MP240_RETRO_VT", &envOk);
    if (!envOk)
        configuredVt = qEnvironmentVariableIntValue("MP240_MPV_VT", &envOk);
    const int preferredVt = envOk ? configuredVt : 12;
    const int activeVt = getActiveVt();
    if (preferredVt > 0 && preferredVt <= 63 && preferredVt != activeVt)
        return preferredVt;

    int fd = ::open("/dev/tty0", O_WRONLY);
    if (fd < 0)
        return 7;
    int n = -1;
    ::ioctl(fd, VT_OPENQRY, &n);
    ::close(fd);
    return (n > 0) ? n : 7;
#else
    return -1;
#endif
}

void RetroBackend::switchToVt(int vt)
{
#ifdef Q_OS_LINUX
    int fd = ::open("/dev/tty0", O_WRONLY);
    if (fd < 0) {
        qWarning("[RetroBackend] switchToVt %d: open /dev/tty0 failed: %s",
                 vt, strerror(errno));
        return;
    }
    if (::ioctl(fd, VT_ACTIVATE, vt) < 0)
        qWarning("[RetroBackend] VT_ACTIVATE %d failed: %s", vt, strerror(errno));
    if (::ioctl(fd, VT_WAITACTIVE, vt) < 0)
        qWarning("[RetroBackend] VT_WAITACTIVE %d failed: %s", vt, strerror(errno));
    ::close(fd);
#else
    Q_UNUSED(vt)
#endif
}

int RetroBackend::findQtDrmFd() const
{
#ifdef Q_OS_LINUX
    QDir fdDir("/proc/self/fd");
    const QStringList entries = fdDir.entryList(QDir::Files | QDir::System);
    for (const QString &entry : entries) {
        bool ok = false;
        const int fd = entry.toInt(&ok);
        if (!ok)
            continue;
        struct stat st {};
        if (::fstat(fd, &st) < 0)
            continue;
        if (!S_ISCHR(st.st_mode))
            continue;
        if (major(st.st_rdev) != 226)
            continue;
        if (minor(st.st_rdev) >= 64)
            continue;
        if (::ioctl(fd, DRM_IOCTL_DROP_MASTER, 0) == 0)
            return fd;
    }
    return -1;
#else
    return -1;
#endif
}

void RetroBackend::prepareHeadlessLaunch()
{
#ifdef Q_OS_LINUX
    if (m_previousVt > 0)
        return;

    m_previousVt = getActiveVt();
    m_qtDrmFd = -1;
    m_qtDrmMasterDropped = false;

    switchToVt(findFreeVt());
    m_qtDrmFd = findQtDrmFd();
    if (m_qtDrmFd < 0) {
        qWarning("[RetroBackend] could not find Qt DRM fd");
        return;
    }

    m_qtDrmMasterDropped = true;
    saveDrmCrtcState(m_qtDrmFd);
#endif
}

void RetroBackend::restoreHeadlessDisplay()
{
#ifdef Q_OS_LINUX
    if (m_qtDrmFd >= 0) {
        if (m_qtDrmMasterDropped && ::ioctl(m_qtDrmFd, DRM_IOCTL_SET_MASTER, 0) < 0)
            qWarning("[RetroBackend] drmSetMaster failed: %s", strerror(errno));
        restoreDrmCrtcState(m_qtDrmFd);
    }

    const int previous = m_previousVt;
    m_previousVt = -1;
    m_qtDrmFd = -1;
    m_qtDrmMasterDropped = false;
    if (previous > 0)
        switchToVt(previous);
#endif
}

#ifdef Q_OS_LINUX
void RetroBackend::saveDrmCrtcState(int fd)
{
    m_savedDrm = {};
    drmModeResPtr res = drmModeGetResources(fd);
    if (!res) {
        qWarning("[RetroBackend] saveDrmCrtcState: drmModeGetResources failed");
        return;
    }

    for (int i = 0; i < res->count_crtcs && !m_savedDrm.valid; ++i) {
        drmModeCrtcPtr crtc = drmModeGetCrtc(fd, res->crtcs[i]);
        if (!crtc)
            continue;

        if (crtc->mode_valid) {
            m_savedDrm.crtcId = crtc->crtc_id;
            m_savedDrm.fbId = crtc->buffer_id;
            m_savedDrm.x = crtc->x;
            m_savedDrm.y = crtc->y;
            m_savedDrm.mode = crtc->mode;

            for (int j = 0; j < res->count_connectors; ++j) {
                drmModeConnectorPtr conn = drmModeGetConnector(fd, res->connectors[j]);
                if (!conn)
                    continue;
                if (conn->encoder_id) {
                    drmModeEncoderPtr enc = drmModeGetEncoder(fd, conn->encoder_id);
                    if (enc) {
                        if (enc->crtc_id == m_savedDrm.crtcId) {
                            m_savedDrm.connectorId = conn->connector_id;
                            m_savedDrm.valid = true;
                        }
                        drmModeFreeEncoder(enc);
                    }
                }
                drmModeFreeConnector(conn);
                if (m_savedDrm.valid)
                    break;
            }
        }
        drmModeFreeCrtc(crtc);
    }
    drmModeFreeResources(res);
}

void RetroBackend::restoreDrmCrtcState(int fd)
{
    if (!m_savedDrm.valid)
        return;

    const int ret = drmModeSetCrtc(fd,
                                   m_savedDrm.crtcId,
                                   m_savedDrm.fbId,
                                   m_savedDrm.x,
                                   m_savedDrm.y,
                                   &m_savedDrm.connectorId,
                                   1,
                                   &m_savedDrm.mode);
    if (ret < 0)
        qWarning("[RetroBackend] drmModeSetCrtc restore failed: %s", strerror(errno));

    m_savedDrm.valid = false;
}
#endif
