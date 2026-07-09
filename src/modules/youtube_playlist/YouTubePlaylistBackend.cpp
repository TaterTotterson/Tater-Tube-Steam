#include "YouTubePlaylistBackend.h"

#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVariantMap>

namespace {
constexpr const char *kModuleId = "com.240mp.youtube_playlist";
constexpr const char *kLegacyPlaylistCacheFile = "public-access-cache.json";
constexpr int kPlaylistCacheVersion = 3;
constexpr int kPlaylistLimit = 500;

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

bool runningOnRaspberryPi3()
{
#ifdef Q_OS_LINUX
    QFile f(QStringLiteral("/proc/device-tree/model"));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QString model = QString::fromLatin1(f.readAll()).remove(QChar('\0')).trimmed();
    return model.startsWith(QStringLiteral("Raspberry Pi 3"));
#else
    return false;
#endif
}

QString cleanTitle(QString title, const QString &fallback)
{
    title = title.trimmed();
    title.replace(QRegularExpression("\\s+"), " ");
    return title.isEmpty() ? fallback : title;
}

QString fallbackPlaylistTitle(const QString &input, int index)
{
    const QString cleaned = input.trimmed();
    if (!cleaned.isEmpty()) {
        QString tail = cleaned;
        const int slash = tail.lastIndexOf('/');
        if (slash >= 0)
            tail = tail.mid(slash + 1);
        const int eq = tail.lastIndexOf('=');
        if (eq >= 0)
            tail = tail.mid(eq + 1);
        tail.remove(QRegularExpression("[^A-Za-z0-9_-]"));
        if (!tail.isEmpty())
            return QStringLiteral("PLAYLIST %1").arg(tail.left(8).toUpper());
    }
    return QStringLiteral("PLAYLIST %1").arg(index + 1);
}

bool isCommercialVideoFile(const QFileInfo &fileInfo)
{
    if (!fileInfo.isFile())
        return false;

    const QString suffix = fileInfo.suffix().toLower();
    static const QSet<QString> videoSuffixes{
        QStringLiteral("mp4"), QStringLiteral("m4v"), QStringLiteral("mkv"),
        QStringLiteral("mov"), QStringLiteral("webm"), QStringLiteral("avi"),
        QStringLiteral("mpg"), QStringLiteral("mpeg"), QStringLiteral("ts")
    };
    return videoSuffixes.contains(suffix);
}

QString commercialTitleFromFile(const QFileInfo &fileInfo, int index)
{
    QString title = fileInfo.completeBaseName().trimmed();
    title.replace(QRegularExpression("[._-]+"), QStringLiteral(" "));
    title.replace(QRegularExpression("\\s+"), QStringLiteral(" "));
    return cleanTitle(title, QStringLiteral("COMMERCIAL %1").arg(index + 1));
}

int commercialVideoCount(const QString &categoryPath)
{
    const QFileInfoList files = QDir(categoryPath).entryInfoList(
        QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    int count = 0;
    for (const QFileInfo &fileInfo : files) {
        if (isCommercialVideoFile(fileInfo))
            ++count;
    }
    return count;
}

QString textFromYouTubeText(const QJsonValue &value);

double durationFromText(const QString &raw)
{
    const QString value = raw.trimmed();
    if (value.isEmpty())
        return 0.0;

    const QStringList parts = value.split(QLatin1Char(':'), Qt::SkipEmptyParts);
    if (parts.isEmpty() || parts.size() > 3)
        return 0.0;

    double seconds = 0.0;
    for (const QString &part : parts) {
        bool ok = false;
        const double number = part.toDouble(&ok);
        if (!ok)
            return 0.0;
        seconds = seconds * 60.0 + number;
    }
    return seconds > 0.0 ? seconds : 0.0;
}

double durationFromJsonValue(const QJsonValue &value)
{
    if (value.isDouble()) {
        const double duration = value.toDouble();
        return duration > 0.0 ? duration : 0.0;
    }
    if (value.isString()) {
        bool ok = false;
        const double duration = value.toString().toDouble(&ok);
        if (ok && duration > 0.0)
            return duration;
        return durationFromText(value.toString());
    }
    return 0.0;
}

double durationFromRenderer(const QJsonObject &renderer)
{
    const double direct = durationFromJsonValue(renderer.value(QStringLiteral("duration")));
    if (direct > 0.0)
        return direct;

    const double lengthText = durationFromText(
        textFromYouTubeText(renderer.value(QStringLiteral("lengthText"))));
    if (lengthText > 0.0)
        return lengthText;

    const double thumbnailOverlay = durationFromText(
        textFromYouTubeText(renderer.value(QStringLiteral("thumbnailOverlays"))
                                .toArray()
                                .first()
                                .toObject()
                                .value(QStringLiteral("thumbnailOverlayTimeStatusRenderer"))
                                .toObject()
                                .value(QStringLiteral("text"))));
    return thumbnailOverlay > 0.0 ? thumbnailOverlay : 0.0;
}

QString decodeJsonString(const QString &value)
{
    const QByteArray json = QByteArray("[\"") + value.toUtf8() + QByteArray("\"]");
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error == QJsonParseError::NoError && doc.isArray() && !doc.array().isEmpty())
        return doc.array().first().toString();
    return value;
}

QString decodeHtmlEntities(QString value)
{
    value.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    value.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    value.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    value.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
    value.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    value.replace(QStringLiteral("&gt;"), QStringLiteral(">"));

    QRegularExpression numericRe(QStringLiteral("&#(x?[0-9A-Fa-f]+);"));
    QRegularExpressionMatch match;
    int offset = 0;
    while ((match = numericRe.match(value, offset)).hasMatch()) {
        const QString code = match.captured(1);
        bool ok = false;
        const uint point = code.startsWith(QLatin1Char('x'), Qt::CaseInsensitive)
            ? code.mid(1).toUInt(&ok, 16)
            : code.toUInt(&ok, 10);
        if (!ok) {
            offset = match.capturedEnd();
            continue;
        }
        const char32_t character = static_cast<char32_t>(point);
        value.replace(match.capturedStart(), match.capturedLength(),
                      QString::fromUcs4(&character, 1));
        offset = match.capturedStart() + 1;
    }

    return value;
}

QString textFromYouTubeText(const QJsonValue &value)
{
    if (value.isString())
        return cleanTitle(value.toString(), QString());
    if (!value.isObject())
        return QString();

    const QJsonObject obj = value.toObject();
    QString text = obj.value(QStringLiteral("simpleText")).toString();
    if (text.isEmpty()) {
        const QJsonArray runs = obj.value(QStringLiteral("runs")).toArray();
        for (const QJsonValue &runValue : runs) {
            const QString part = runValue.toObject().value(QStringLiteral("text")).toString();
            if (!part.isEmpty()) {
                text += part;
            }
        }
    }

    return cleanTitle(text, QString());
}

QString extractJsonObjectAfter(const QString &text, const QString &marker)
{
    const int markerIndex = text.indexOf(marker);
    if (markerIndex < 0)
        return QString();

    const int start = text.indexOf('{', markerIndex + marker.size());
    if (start < 0)
        return QString();

    bool inString = false;
    bool escaped = false;
    int depth = 0;
    for (int i = start; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (ch == QLatin1Char('\\')) {
                escaped = true;
            } else if (ch == QLatin1Char('"')) {
                inString = false;
            }
            continue;
        }

        if (ch == QLatin1Char('"')) {
            inString = true;
        } else if (ch == QLatin1Char('{')) {
            ++depth;
        } else if (ch == QLatin1Char('}')) {
            --depth;
            if (depth == 0)
                return text.mid(start, i - start + 1);
        }
    }

    return QString();
}

QString playlistTitleFromInitialData(const QJsonValue &value)
{
    if (value.isArray()) {
        for (const QJsonValue &child : value.toArray()) {
            const QString title = playlistTitleFromInitialData(child);
            if (!title.isEmpty())
                return title;
        }
        return QString();
    }
    if (!value.isObject())
        return QString();

    const QJsonObject obj = value.toObject();
    const QJsonObject metadata = obj.value(QStringLiteral("playlistMetadataRenderer")).toObject();
    const QString metadataTitle = cleanTitle(metadata.value(QStringLiteral("title")).toString(),
                                             QString());
    if (!metadataTitle.isEmpty())
        return metadataTitle;

    const QJsonObject header = obj.value(QStringLiteral("playlistHeaderRenderer")).toObject();
    const QString headerTitle = textFromYouTubeText(header.value(QStringLiteral("title")));
    if (!headerTitle.isEmpty())
        return headerTitle;

    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        const QString title = playlistTitleFromInitialData(it.value());
        if (!title.isEmpty())
            return title;
    }

    return QString();
}

QString playlistTitleFromHtmlMeta(const QString &html)
{
    const QRegularExpression metaRe(QStringLiteral(
        "<meta\\s+[^>]*(?:property|name)=[\"'](?:og:title|title)[\"'][^>]*content=[\"']([^\"']+)[\"']"),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = metaRe.match(html);
    if (!match.hasMatch()) {
        const QRegularExpression reversedRe(QStringLiteral(
            "<meta\\s+[^>]*content=[\"']([^\"']+)[\"'][^>]*(?:property|name)=[\"'](?:og:title|title)[\"']"),
            QRegularExpression::CaseInsensitiveOption);
        match = reversedRe.match(html);
    }
    if (!match.hasMatch())
        return QString();

    QString title = decodeHtmlEntities(match.captured(1));
    title.remove(QRegularExpression("\\s+-\\s+YouTube\\s*$", QRegularExpression::CaseInsensitiveOption));
    return cleanTitle(title, QString());
}

QString textFromLockupTitle(const QJsonObject &renderer)
{
    const QJsonObject metadata = renderer.value(QStringLiteral("metadata")).toObject()
        .value(QStringLiteral("lockupMetadataViewModel")).toObject();
    return cleanTitle(metadata.value(QStringLiteral("title")).toObject()
                          .value(QStringLiteral("content")).toString(),
                      QString());
}

void collectRendererVideos(const QJsonValue &value,
                           const QString &rendererKey,
                           int limit,
                           QSet<QString> &seen,
                           QVariantList &items)
{
    if (items.size() >= limit)
        return;

    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue &child : array) {
            collectRendererVideos(child, rendererKey, limit, seen, items);
            if (items.size() >= limit)
                return;
        }
        return;
    }

    if (!value.isObject())
        return;

    const QJsonObject obj = value.toObject();
    const QJsonObject renderer = obj.value(rendererKey).toObject();
    const bool lockup = rendererKey == QLatin1String("lockupViewModel");
    const QString id = renderer.value(lockup ? QStringLiteral("contentId")
                                             : QStringLiteral("videoId"))
                           .toString()
                           .trimmed();
    if (id.size() == 11 && !seen.contains(id)) {
        seen.insert(id);
        QVariantMap item;
        item[QStringLiteral("id")] = id;
        item[QStringLiteral("url")] = QStringLiteral("https://www.youtube.com/watch?v=%1").arg(id);
        item[QStringLiteral("title")] = cleanTitle(
            lockup ? textFromLockupTitle(renderer)
                   : textFromYouTubeText(renderer.value(QStringLiteral("title"))),
            QStringLiteral("VIDEO %1").arg(items.size() + 1));
        const double duration = durationFromRenderer(renderer);
        if (duration > 0.0)
            item[QStringLiteral("duration")] = duration;
        item[QStringLiteral("index")] = items.size();
        items.append(item);
        if (items.size() >= limit)
            return;
    }

    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        collectRendererVideos(it.value(), rendererKey, limit, seen, items);
        if (items.size() >= limit)
            return;
    }
}

QVariantMap playlistDataFromHtml(const QString &playlistUrl, int limit)
{
    QVariantMap result;
    QVariantList items;
    const QString curlPath = QStandardPaths::findExecutable(QStringLiteral("curl"),
                                                            executableSearchPaths());
    if (curlPath.isEmpty())
        return result;

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(curlPath, {
        QStringLiteral("-L"),
        QStringLiteral("-sS"),
        QStringLiteral("--compressed"),
        QStringLiteral("--max-time"),
        QStringLiteral("20"),
        playlistUrl
    });
    if (!process.waitForStarted(2000))
        return result;
    if (!process.waitForFinished(25000)) {
        process.kill();
        process.waitForFinished(1000);
        return result;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return result;

    const QString html = QString::fromUtf8(process.readAllStandardOutput());
    const QString initialData = extractJsonObjectAfter(html, QStringLiteral("ytInitialData"));
    if (!initialData.isEmpty()) {
        QJsonParseError jsonError;
        const QJsonDocument doc = QJsonDocument::fromJson(initialData.toUtf8(), &jsonError);
        if (jsonError.error == QJsonParseError::NoError) {
            result[QStringLiteral("title")] = playlistTitleFromInitialData(doc.object());
            QSet<QString> seen;
            collectRendererVideos(doc.object(), QStringLiteral("playlistVideoRenderer"),
                                  limit, seen, items);
            if (items.isEmpty())
                collectRendererVideos(doc.object(), QStringLiteral("videoRenderer"),
                                      limit, seen, items);
            if (items.isEmpty())
                collectRendererVideos(doc.object(), QStringLiteral("lockupViewModel"),
                                      limit, seen, items);
        }
    }

    const QRegularExpression videoRe(QStringLiteral("\"videoId\"\\s*:\\s*\"([A-Za-z0-9_-]{11})\""));
    const QRegularExpression titleRe(QStringLiteral(
        "\"title\"\\s*:\\s*\\{\\s*(?:\"runs\"\\s*:\\s*\\[\\s*\\{\\s*\"text\"\\s*:\\s*\"([^\"]+)\"|\"simpleText\"\\s*:\\s*\"([^\"]+)\")"));

    QSet<QString> seen;
    for (const QVariant &value : items)
        seen.insert(value.toMap().value(QStringLiteral("id")).toString());

    QRegularExpressionMatchIterator it = videoRe.globalMatch(html);
    while (it.hasNext() && items.size() < limit) {
        const QRegularExpressionMatch match = it.next();
        const QString id = match.captured(1);
        if (seen.contains(id))
            continue;
        seen.insert(id);

        QString title;
        const QString segment = html.mid(match.capturedStart(), 3000);
        const QRegularExpressionMatch titleMatch = titleRe.match(segment);
        if (titleMatch.hasMatch())
            title = decodeJsonString(titleMatch.captured(1).isEmpty()
                ? titleMatch.captured(2)
                : titleMatch.captured(1));

        QVariantMap item;
        item[QStringLiteral("id")] = id;
        item[QStringLiteral("url")] = QStringLiteral("https://www.youtube.com/watch?v=%1").arg(id);
        item[QStringLiteral("title")] = cleanTitle(title, QStringLiteral("VIDEO %1").arg(items.size() + 1));
        item[QStringLiteral("index")] = items.size();
        items.append(item);
    }

    if (result.value(QStringLiteral("title")).toString().isEmpty())
        result[QStringLiteral("title")] = playlistTitleFromHtmlMeta(html);
    result[QStringLiteral("items")] = items;
    return result;
}

}

YouTubePlaylistBackend::YouTubePlaylistBackend(const QString &appRoot,
                                               const QString &dataRoot,
                                               QObject *parent)
    : QObject(parent)
    , m_appRoot(appRoot)
    , m_dataRoot(dataRoot)
{
}

QVariantMap YouTubePlaylistBackend::moduleConfig() const
{
    const QJsonObject config = loadConfig();
    return config.value(QStringLiteral("modules")).toObject()[QString::fromUtf8(kModuleId)]
        .toObject().toVariantMap();
}

QJsonObject YouTubePlaylistBackend::loadConfig() const
{
    QFile file(m_dataRoot + "/config.json");
    if (!file.open(QIODevice::ReadOnly))
        return {};

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return {};

    return doc.object();
}

bool YouTubePlaylistBackend::saveConfig(const QJsonObject &config) const
{
    QDir().mkpath(m_dataRoot);
    QSaveFile file(m_dataRoot + "/config.json");
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning("[YouTubePlaylistBackend] could not write config: %s",
                 qPrintable(file.errorString()));
        return false;
    }

    file.write(QJsonDocument(config).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        qWarning("[YouTubePlaylistBackend] could not save config: %s",
                 qPrintable(file.errorString()));
        return false;
    }
    return true;
}

QString YouTubePlaylistBackend::setting(const QString &key, const QString &fallback) const
{
    const QVariantMap cfg = moduleConfig();
    const QString value = cfg.value(key).toString().trimmed();
    return value.isEmpty() ? fallback : value;
}

QString YouTubePlaylistBackend::ytDlpPath() const
{
    QString path = QStandardPaths::findExecutable(QStringLiteral("yt-dlp"), executableSearchPaths());
    if (!path.isEmpty())
        return path;
    return QStandardPaths::findExecutable(QStringLiteral("youtube-dl"), executableSearchPaths());
}

QString YouTubePlaylistBackend::commercialRootPath() const
{
    return QDir(m_dataRoot).absoluteFilePath(QStringLiteral("commercials"));
}

QVariantList YouTubePlaylistBackend::commercialCategoryOptions() const
{
    QVariantList options;
    const QDir root(commercialRootPath());
    const QFileInfoList categories = root.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable, QDir::Name | QDir::IgnoreCase);

    for (const QFileInfo &category : categories) {
        const QString id = category.fileName();
        if (id.trimmed().isEmpty())
            continue;
        const int count = commercialVideoCount(category.absoluteFilePath());
        options.append(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("label"), QStringLiteral("%1 (%2)").arg(id, QString::number(count))},
            {QStringLiteral("count"), count}
        });
    }
    return options;
}

QVariantList YouTubePlaylistBackend::commercialVideosForCategoryId(const QString &categoryId,
                                                                   int startIndex) const
{
    QVariantList videos;
    const QString cleanCategory = QFileInfo(categoryId).fileName().trimmed();
    if (cleanCategory.isEmpty() || cleanCategory != categoryId.trimmed())
        return videos;

    const QDir categoryDir(QDir(commercialRootPath()).absoluteFilePath(cleanCategory));
    if (!categoryDir.exists())
        return videos;

    const QFileInfoList files = categoryDir.entryInfoList(
        QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &fileInfo : files) {
        if (!isCommercialVideoFile(fileInfo))
            continue;

        QVariantMap item;
        item[QStringLiteral("id")] = QStringLiteral("%1/%2").arg(cleanCategory, fileInfo.fileName());
        item[QStringLiteral("title")] = commercialTitleFromFile(fileInfo, startIndex + videos.size());
        item[QStringLiteral("url")] = QUrl::fromLocalFile(fileInfo.absoluteFilePath()).toString();
        item[QStringLiteral("category")] = cleanCategory;
        item[QStringLiteral("duration")] = 30;
        item[QStringLiteral("commercial")] = true;
        item[QStringLiteral("local")] = true;
        videos.append(item);
    }

    return videos;
}

QVariantList YouTubePlaylistBackend::commercialVideosForSelection(const QVariantMap &selection) const
{
    QVariantList videos;
    const QDir root(commercialRootPath());
    const QFileInfoList categories = root.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable, QDir::Name | QDir::IgnoreCase);

    for (const QFileInfo &category : categories) {
        const QString categoryId = category.fileName();
        if (categoryId.trimmed().isEmpty())
            continue;

        const QVariant selectedValue = selection.value(categoryId, true);
        const bool selected = selectedValue.toBool();
        if (!selected)
            continue;

        const QVariantList categoryVideos = commercialVideosForCategoryId(categoryId, videos.size());
        for (const QVariant &video : categoryVideos)
            videos.append(video);
    }

    return videos;
}

QVariantList YouTubePlaylistBackend::get_commercial_categories() const
{
    return commercialCategoryOptions();
}

QVariantList YouTubePlaylistBackend::get_commercial_videos_for_setting(const QString &settingKey) const
{
    return commercialVideosForSelection(moduleConfig().value(settingKey).toMap());
}

QVariantList YouTubePlaylistBackend::get_commercial_videos_for_category(const QString &categoryId) const
{
    return commercialVideosForCategoryId(categoryId);
}

QString YouTubePlaylistBackend::playlistCachePath(const QString &playlistUrl) const
{
    const QByteArray hash = QCryptographicHash::hash(playlistUrl.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QDir(m_dataRoot).absoluteFilePath(QStringLiteral("public-access-cache-%1.json")
                                             .arg(QString::fromLatin1(hash)));
}

QVariantMap YouTubePlaylistBackend::loadPlaylistCache(const QString &playlistUrl) const
{
    QFile file(playlistCachePath(playlistUrl));
    if (!file.open(QIODevice::ReadOnly))
        return {};

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return {};

    const QVariantMap cache = doc.object().toVariantMap();
    if (cache.value(QStringLiteral("version")).toInt() != kPlaylistCacheVersion)
        return {};
    if (cache.value(QStringLiteral("playlistUrl")).toString() != playlistUrl)
        return {};
    if (cache.value(QStringLiteral("items")).toList().isEmpty())
        return {};
    return cache;
}

bool YouTubePlaylistBackend::savePlaylistCache(const QString &playlistUrl,
                                               const QString &title,
                                               const QVariantList &items) const
{
    if (playlistUrl.isEmpty() || items.isEmpty())
        return false;

    QDir().mkpath(m_dataRoot);
    QSaveFile file(playlistCachePath(playlistUrl));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning("[YouTubePlaylistBackend] could not write playlist cache: %s",
                 qPrintable(file.errorString()));
        return false;
    }

    QVariantMap cache;
    cache[QStringLiteral("version")] = kPlaylistCacheVersion;
    cache[QStringLiteral("playlistUrl")] = playlistUrl;
    cache[QStringLiteral("title")] = title;
    cache[QStringLiteral("items")] = items;
    cache[QStringLiteral("cachedAt")] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    file.write(QJsonDocument(QJsonObject::fromVariantMap(cache)).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        qWarning("[YouTubePlaylistBackend] could not save playlist cache: %s",
                 qPrintable(file.errorString()));
        return false;
    }
    return true;
}

void YouTubePlaylistBackend::clearPlaylistCache() const
{
    QFile::remove(QDir(m_dataRoot).absoluteFilePath(QString::fromUtf8(kLegacyPlaylistCacheFile)));
    const QStringList names = QDir(m_dataRoot).entryList(
        {QStringLiteral("public-access-cache-*.json")}, QDir::Files);
    for (const QString &name : names)
        QFile::remove(QDir(m_dataRoot).absoluteFilePath(name));
}

void YouTubePlaylistBackend::clearPlaylistCache(const QString &playlistUrl) const
{
    if (playlistUrl.isEmpty())
        return;
    QFile::remove(playlistCachePath(playlistUrl));
}

QString YouTubePlaylistBackend::get_auth_state()
{
    return get_saved_playlists().isEmpty()
        ? QStringLiteral("none")
        : QStringLiteral("authed");
}

QString YouTubePlaylistBackend::get_saved_playlist_input() const
{
    return setting(QStringLiteral("playlist_input"));
}

QVariantList YouTubePlaylistBackend::get_saved_playlists() const
{
    QVariantList result;
    const QVariantMap cfg = moduleConfig();
    const QVariantList saved = cfg.value(QStringLiteral("playlists")).toList();
    QSet<QString> seen;

    int index = 0;
    for (const QVariant &value : saved) {
        QVariantMap playlist = value.toMap();
        const QString input = playlist.value(QStringLiteral("input")).toString().trimmed();
        const QString url = normalize_playlist_input(
            playlist.value(QStringLiteral("url")).toString().trimmed().isEmpty()
                ? input
                : playlist.value(QStringLiteral("url")).toString());
        if (url.isEmpty() || seen.contains(url))
            continue;

        playlist[QStringLiteral("input")] = input.isEmpty() ? url : input;
        playlist[QStringLiteral("url")] = url;
        playlist[QStringLiteral("title")] = cleanTitle(
            playlist.value(QStringLiteral("title")).toString(),
            fallbackPlaylistTitle(input.isEmpty() ? url : input, index));
        playlist[QStringLiteral("id")] = playlist.value(QStringLiteral("id")).toString().isEmpty()
            ? QString::fromLatin1(QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex())
            : playlist.value(QStringLiteral("id")).toString();
        result.append(playlist);
        seen.insert(url);
        ++index;
    }

    const QString legacyInput = get_saved_playlist_input();
    const QString legacyUrl = normalize_playlist_input(legacyInput);
    if (!legacyUrl.isEmpty() && !seen.contains(legacyUrl)) {
        QVariantMap legacy;
        legacy[QStringLiteral("id")] = QString::fromLatin1(
            QCryptographicHash::hash(legacyUrl.toUtf8(), QCryptographicHash::Sha1).toHex());
        legacy[QStringLiteral("input")] = legacyInput;
        legacy[QStringLiteral("url")] = legacyUrl;
        legacy[QStringLiteral("title")] = fallbackPlaylistTitle(legacyInput, result.size());
        legacy[QStringLiteral("legacy")] = true;
        result.append(legacy);
    }

    return result;
}

QVariantList YouTubePlaylistBackend::playlistRemovalOptions() const
{
    QVariantList options;
    const QVariantList saved = get_saved_playlists();
    for (const QVariant &value : saved) {
        const QVariantMap playlist = value.toMap();
        const QString id = playlist.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;
        options.append(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("label"), playlist.value(QStringLiteral("title")).toString()}
        });
    }
    return options;
}

QString YouTubePlaylistBackend::normalize_playlist_input(const QString &input) const
{
    QString raw = input.trimmed();
    if (raw.isEmpty())
        return QString();

    raw.remove(QRegularExpression("^['\"]|['\"]$"));

    const QRegularExpression listParamRe("(^|[?&])list=([A-Za-z0-9_-]+)");
    const QRegularExpressionMatch listMatch = listParamRe.match(raw);
    if (listMatch.hasMatch())
        raw = listMatch.captured(2);

    if (raw.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) ||
        raw.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        const QUrl url(raw);
        const QString listId = QUrlQuery(url).queryItemValue(QStringLiteral("list"));
        if (!listId.isEmpty())
            raw = listId;
        else
            return QString();
    }

    raw.remove(QRegularExpression("[^A-Za-z0-9_-]"));
    if (raw.isEmpty())
        return QString();

    return QStringLiteral("https://www.youtube.com/playlist?list=%1").arg(raw);
}

QString YouTubePlaylistBackend::ytdl_format_for_quality(const QString &quality) const
{
    const QString q = quality.trimmed().toLower();
    if (runningOnRaspberryPi3() && q == QLatin1String("best"))
        return QStringLiteral("best[height<=480][ext=mp4]/bestvideo[height<=480][vcodec^=avc1]+bestaudio[acodec^=mp4a]/best[height<=480]/best");
    if (q == QLatin1String("best"))
        return QStringLiteral("bestvideo[vcodec^=avc1]+bestaudio[acodec^=mp4a]/best");
    if (q == QLatin1String("480p"))
        return QStringLiteral("best[height<=480][ext=mp4]/bestvideo[height<=480][vcodec^=avc1]+bestaudio[acodec^=mp4a]/best[height<=480]/best");
    return QStringLiteral("best[height<=360][ext=mp4]/bestvideo[height<=360][vcodec^=avc1]+bestaudio[acodec^=mp4a]/best[height<=360]/best");
}

QString YouTubePlaylistBackend::directStreamFormatForQuality(const QString &quality) const
{
    const QString q = quality.trimmed().toLower();
    if (runningOnRaspberryPi3() && q == QLatin1String("best"))
        return QStringLiteral("best[height<=480][ext=mp4]/best[height<=480]/best");
    if (q == QLatin1String("best"))
        return QStringLiteral("best[ext=mp4]/best");
    if (q == QLatin1String("480p"))
        return QStringLiteral("best[height<=480][ext=mp4]/best[height<=480]/best");
    return QStringLiteral("best[height<=360][ext=mp4]/best[height<=360]/best");
}

void YouTubePlaylistBackend::cancel_video_stream_resolve()
{
    if (!m_streamResolver)
        return;

    QProcess *process = m_streamResolver;
    m_streamResolver = nullptr;
    m_streamResolveRequestId.clear();
    process->disconnect(this);
    if (process->state() != QProcess::NotRunning) {
        process->kill();
        process->waitForFinished(1000);
    }
    process->deleteLater();
}

void YouTubePlaylistBackend::resolve_video_stream(const QString &requestId,
                                                  const QString &url,
                                                  const QString &quality)
{
    cancel_video_stream_resolve();

    QVariantMap failure;
    failure[QStringLiteral("ok")] = false;

    const QString trimmedUrl = url.trimmed();
    if (requestId.trimmed().isEmpty() || trimmedUrl.isEmpty()) {
        failure[QStringLiteral("message")] = QStringLiteral("NO VIDEO URL");
        emit videoStreamResolved(requestId, failure);
        return;
    }

    const QString bin = ytDlpPath();
    if (bin.isEmpty()) {
        failure[QStringLiteral("message")] = QStringLiteral("YT-DLP IS NOT INSTALLED");
        emit videoStreamResolved(requestId, failure);
        return;
    }

    QStringList args{
        QStringLiteral("--no-warnings"),
        QStringLiteral("--no-playlist"),
        QStringLiteral("--format"),
        directStreamFormatForQuality(quality),
        QStringLiteral("--get-url"),
        trimmedUrl
    };

    QProcess *process = new QProcess(this);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    m_streamResolver = process;
    m_streamResolveRequestId = requestId;

    connect(process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this, process, requestId, trimmedUrl](int exitCode, QProcess::ExitStatus exitStatus) {
        if (process != m_streamResolver) {
            process->deleteLater();
            return;
        }

        const QString stdoutText = QString::fromUtf8(process->readAllStandardOutput()).trimmed();
        const QString stderrText = QString::fromUtf8(process->readAllStandardError()).trimmed();
        m_streamResolver = nullptr;
        m_streamResolveRequestId.clear();
        process->deleteLater();

        QVariantMap result;
        result[QStringLiteral("ok")] = false;
        result[QStringLiteral("sourceUrl")] = trimmedUrl;

        if (exitStatus != QProcess::NormalExit || exitCode != 0) {
            result[QStringLiteral("message")] = stderrText.isEmpty()
                ? QStringLiteral("YOUTUBE STREAM RESOLVE FAILED")
                : stderrText.toUpper().left(160);
            emit videoStreamResolved(requestId, result);
            return;
        }

        QStringList urls;
        const QStringList lines = stdoutText.split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                                   Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QString candidate = line.trimmed();
            if (candidate.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) ||
                candidate.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
                urls.append(candidate);
            }
        }

        if (urls.size() != 1) {
            result[QStringLiteral("message")] = urls.isEmpty()
                ? QStringLiteral("YOUTUBE STREAM URL NOT FOUND")
                : QStringLiteral("YOUTUBE STREAM IS SPLIT");
            emit videoStreamResolved(requestId, result);
            return;
        }

        result[QStringLiteral("ok")] = true;
        result[QStringLiteral("url")] = urls.first();
        result[QStringLiteral("message")] = QString();
        emit videoStreamResolved(requestId, result);
    });

    QTimer::singleShot(30000, process, [this, process, requestId, trimmedUrl]() {
        if (process != m_streamResolver || process->state() == QProcess::NotRunning)
            return;

        process->kill();
        QVariantMap result;
        result[QStringLiteral("ok")] = false;
        result[QStringLiteral("sourceUrl")] = trimmedUrl;
        result[QStringLiteral("message")] = QStringLiteral("YOUTUBE STREAM RESOLVE TIMED OUT");
        m_streamResolver = nullptr;
        m_streamResolveRequestId.clear();
        emit videoStreamResolved(requestId, result);
    });

    process->start(bin, args);
    if (!process->waitForStarted(2000)) {
        m_streamResolver = nullptr;
        m_streamResolveRequestId.clear();
        process->deleteLater();
        failure[QStringLiteral("message")] = QStringLiteral("COULD NOT START YT-DLP");
        emit videoStreamResolved(requestId, failure);
    }
}

QVariantMap YouTubePlaylistBackend::inspectPlaylist(const QString &playlistUrl, int limit,
                                                    QString *errorOut) const
{
    QVariantMap result;
    const QString bin = ytDlpPath();
    if (bin.isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("YT-DLP IS NOT INSTALLED");
        return result;
    }

    QStringList args{
        QStringLiteral("--flat-playlist"),
        QStringLiteral("--dump-single-json"),
        QStringLiteral("--no-warnings"),
        QStringLiteral("--playlist-end"),
        QString::number(limit),
        playlistUrl
    };

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(bin, args);
    if (!process.waitForStarted(2000)) {
        if (errorOut)
            *errorOut = QStringLiteral("COULD NOT START YT-DLP");
        return result;
    }

    if (!process.waitForFinished(90000)) {
        process.kill();
        process.waitForFinished(1000);
        if (errorOut)
            *errorOut = QStringLiteral("YOUTUBE PLAYLIST TIMED OUT");
        return result;
    }

    const QByteArray stdoutData = process.readAllStandardOutput();
    const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (errorOut)
            *errorOut = stderrText.isEmpty()
                ? QStringLiteral("YOUTUBE PLAYLIST FAILED")
                : stderrText.toUpper().left(160);
        return result;
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(stdoutData, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorOut)
            *errorOut = QStringLiteral("YOUTUBE PLAYLIST DATA IS INVALID");
        return result;
    }

    result = doc.object().toVariantMap();
    return result;
}

QVariantMap YouTubePlaylistBackend::resolve_playlist_info(const QString &input) const
{
    QVariantMap result;
    const QString playlistUrl = normalize_playlist_input(input);
    if (playlistUrl.isEmpty()) {
        result[QStringLiteral("ok")] = false;
        result[QStringLiteral("message")] = QStringLiteral("ENTER PLAYLIST CODE");
        return result;
    }

    QString error;
    const QVariantMap info = inspectPlaylist(playlistUrl, 1, &error);
    const QVariantList entries = info.value(QStringLiteral("entries")).toList();
    const int playlistCount = info.value(QStringLiteral("playlist_count")).toInt();
    const QString infoTitle = info.value(QStringLiteral("title")).toString().trimmed();
    const QVariantMap fallbackData = error.isEmpty() && !entries.isEmpty()
        ? QVariantMap{}
        : playlistDataFromHtml(playlistUrl, 1);
    const QVariantList fallbackItems = fallbackData.value(QStringLiteral("items")).toList();
    const QString title = cleanTitle(
        infoTitle,
        cleanTitle(fallbackData.value(QStringLiteral("title")).toString(),
                   fallbackPlaylistTitle(input, 0)));
    const bool hasMetadata = error.isEmpty()
        && (!entries.isEmpty() || playlistCount > 0 || !infoTitle.isEmpty());
    const bool ok = hasMetadata || !fallbackItems.isEmpty();

    result[QStringLiteral("ok")] = ok;
    result[QStringLiteral("input")] = input.trimmed();
    result[QStringLiteral("url")] = playlistUrl;
    result[QStringLiteral("title")] = title;
    result[QStringLiteral("id")] = QString::fromLatin1(
        QCryptographicHash::hash(playlistUrl.toUtf8(), QCryptographicHash::Sha1).toHex());
    result[QStringLiteral("message")] = ok
        ? QString()
        : (error.isEmpty() ? QStringLiteral("PLAYLIST LOOKUP FOUND NO VIDEOS") : error);
    return result;
}

void YouTubePlaylistBackend::load_playlist(const QString &input)
{
    fetchPlaylist(input, false);
}

void YouTubePlaylistBackend::refresh_playlist_cache()
{
    clearPlaylistCache();
    emit authStateChanged();
}

void YouTubePlaylistBackend::refresh_playlist(const QString &input)
{
    fetchPlaylist(input, true);
}

void YouTubePlaylistBackend::load_playlist_remove_options()
{
    emit dynamicOptionsReady(QStringLiteral("remove_playlist_id"), playlistRemovalOptions());
}

void YouTubePlaylistBackend::load_playlist_rename_options()
{
    emit dynamicOptionsReady(QStringLiteral("rename_playlist_id"), playlistRemovalOptions());
}

void YouTubePlaylistBackend::load_public_access_commercial_category_options()
{
    emit dynamicOptionsReady(QStringLiteral("public_access_commercial_categories"),
                             commercialCategoryOptions());
}

void YouTubePlaylistBackend::load_vod_commercial_category_options()
{
    emit dynamicOptionsReady(QStringLiteral("vod_commercial_categories"),
                             commercialCategoryOptions());
}

void YouTubePlaylistBackend::remove_selected_playlist()
{
    const QVariantList saved = get_saved_playlists();
    if (saved.isEmpty()) {
        emit dynamicOptionsReady(QStringLiteral("remove_playlist_id"), QVariantList{});
        emit dynamicOptionsReady(QStringLiteral("rename_playlist_id"), QVariantList{});
        emit authStateChanged();
        return;
    }

    const QVariantMap cfg = moduleConfig();
    QString selectedId = cfg.value(QStringLiteral("remove_playlist_id")).toString();
    if (selectedId.isEmpty())
        selectedId = saved.first().toMap().value(QStringLiteral("id")).toString();

    QVariantList remaining;
    QString removedUrl;
    for (const QVariant &value : saved) {
        const QVariantMap playlist = value.toMap();
        if (playlist.value(QStringLiteral("id")).toString() == selectedId) {
            removedUrl = playlist.value(QStringLiteral("url")).toString();
            continue;
        }

        QVariantMap clean;
        clean[QStringLiteral("id")] = playlist.value(QStringLiteral("id")).toString();
        clean[QStringLiteral("input")] = playlist.value(QStringLiteral("input")).toString();
        clean[QStringLiteral("url")] = playlist.value(QStringLiteral("url")).toString();
        clean[QStringLiteral("title")] = playlist.value(QStringLiteral("title")).toString();
        remaining.append(clean);
    }

    if (remaining.size() == saved.size()) {
        emit errorOccurred(QStringLiteral("PLAYLIST NOT FOUND"));
        return;
    }

    QJsonObject config = loadConfig();
    QJsonObject modules = config.value(QStringLiteral("modules")).toObject();
    QJsonObject module = modules.value(QString::fromUtf8(kModuleId)).toObject();
    module[QStringLiteral("playlists")] = QJsonValue::fromVariant(remaining);
    module.remove(QStringLiteral("playlist_input"));
    if (remaining.isEmpty()) {
        module.remove(QStringLiteral("remove_playlist_id"));
        module.remove(QStringLiteral("rename_playlist_id"));
        module.remove(QStringLiteral("rename_playlist_title"));
    } else {
        const QString firstId = remaining.first().toMap().value(QStringLiteral("id")).toString();
        module[QStringLiteral("remove_playlist_id")] = firstId;
        if (module.value(QStringLiteral("rename_playlist_id")).toString() == selectedId)
            module[QStringLiteral("rename_playlist_id")] = firstId;
    }

    modules[QString::fromUtf8(kModuleId)] = module;
    config[QStringLiteral("modules")] = modules;
    if (!saveConfig(config)) {
        emit errorOccurred(QStringLiteral("COULD NOT REMOVE PLAYLIST"));
        return;
    }

    clearPlaylistCache(removedUrl);
    emit dynamicOptionsReady(QStringLiteral("remove_playlist_id"), playlistRemovalOptions());
    emit dynamicOptionsReady(QStringLiteral("rename_playlist_id"), playlistRemovalOptions());
    emit authStateChanged();
}

void YouTubePlaylistBackend::rename_selected_playlist()
{
    const QVariantList saved = get_saved_playlists();
    if (saved.isEmpty()) {
        emit dynamicOptionsReady(QStringLiteral("rename_playlist_id"), QVariantList{});
        emit authStateChanged();
        return;
    }

    const QVariantMap cfg = moduleConfig();
    QString selectedId = cfg.value(QStringLiteral("rename_playlist_id")).toString();
    if (selectedId.isEmpty())
        selectedId = saved.first().toMap().value(QStringLiteral("id")).toString();

    const QString newTitle = cleanTitle(cfg.value(QStringLiteral("rename_playlist_title")).toString(),
                                        QString());
    if (newTitle.isEmpty()) {
        emit errorOccurred(QStringLiteral("ENTER PLAYLIST NAME"));
        return;
    }

    QVariantList renamed;
    bool changed = false;
    for (const QVariant &value : saved) {
        const QVariantMap playlist = value.toMap();
        QVariantMap clean;
        clean[QStringLiteral("id")] = playlist.value(QStringLiteral("id")).toString();
        clean[QStringLiteral("input")] = playlist.value(QStringLiteral("input")).toString();
        clean[QStringLiteral("url")] = playlist.value(QStringLiteral("url")).toString();
        clean[QStringLiteral("title")] = playlist.value(QStringLiteral("title")).toString();
        if (clean.value(QStringLiteral("id")).toString() == selectedId) {
            clean[QStringLiteral("title")] = newTitle;
            changed = true;
        }
        renamed.append(clean);
    }

    if (!changed) {
        emit errorOccurred(QStringLiteral("PLAYLIST NOT FOUND"));
        return;
    }

    QJsonObject config = loadConfig();
    QJsonObject modules = config.value(QStringLiteral("modules")).toObject();
    QJsonObject module = modules.value(QString::fromUtf8(kModuleId)).toObject();
    module[QStringLiteral("playlists")] = QJsonValue::fromVariant(renamed);
    module[QStringLiteral("rename_playlist_id")] = selectedId;
    module[QStringLiteral("rename_playlist_title")] = newTitle;

    modules[QString::fromUtf8(kModuleId)] = module;
    config[QStringLiteral("modules")] = modules;
    if (!saveConfig(config)) {
        emit errorOccurred(QStringLiteral("COULD NOT RENAME PLAYLIST"));
        return;
    }

    emit dynamicOptionsReady(QStringLiteral("remove_playlist_id"), playlistRemovalOptions());
    emit dynamicOptionsReady(QStringLiteral("rename_playlist_id"), playlistRemovalOptions());
    emit authStateChanged();
}

void YouTubePlaylistBackend::fetchPlaylist(const QString &input, bool forceRefresh)
{
    const QString playlistUrl = normalize_playlist_input(input);
    if (playlistUrl.isEmpty()) {
        emit errorOccurred(QStringLiteral("ENTER PLAYLIST CODE"));
        return;
    }

    if (!forceRefresh) {
        const QVariantMap cache = loadPlaylistCache(playlistUrl);
        if (!cache.isEmpty()) {
            emit playlistLoaded(cache.value(QStringLiteral("title")).toString(),
                                cache.value(QStringLiteral("items")).toList());
            return;
        }
    }

    if (forceRefresh)
        clearPlaylistCache(playlistUrl);

    QString error;
    const QVariantMap rootMap = inspectPlaylist(playlistUrl, kPlaylistLimit, &error);
    QVariantList items;
    if (error.isEmpty()) {
        const QJsonArray entries = QJsonArray::fromVariantList(rootMap.value(QStringLiteral("entries")).toList());
        int position = 0;
        for (const QJsonValue &value : entries) {
            if (!value.isObject())
                continue;
            const QJsonObject entry = value.toObject();
            const QString id = entry.value(QStringLiteral("id")).toString().trimmed();
            QString url = entry.value(QStringLiteral("url")).toString().trimmed();
            if (url.isEmpty() && id.isEmpty())
                continue;
            if (!url.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) &&
                !url.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
                const QString videoId = id.isEmpty() ? url : id;
                url = QStringLiteral("https://www.youtube.com/watch?v=%1").arg(videoId);
            }

            QVariantMap item;
            item["id"] = id.isEmpty() ? url : id;
            item["url"] = url;
            item["title"] = cleanTitle(entry.value(QStringLiteral("title")).toString(),
                                       QStringLiteral("VIDEO %1").arg(position + 1));
            double duration = durationFromJsonValue(entry.value(QStringLiteral("duration")));
            if (duration <= 0.0)
                duration = durationFromJsonValue(entry.value(QStringLiteral("duration_string")));
            if (duration > 0.0)
                item["duration"] = duration;
            item["index"] = position;
            items.append(item);
            ++position;
        }
    }

    QVariantMap fallbackData;
    if (items.isEmpty()) {
        fallbackData = playlistDataFromHtml(playlistUrl, kPlaylistLimit);
        items = fallbackData.value(QStringLiteral("items")).toList();
    }

    if (items.isEmpty()) {
        emit errorOccurred(error.isEmpty() ? QStringLiteral("PLAYLIST HAS NO VIDEOS") : error);
        return;
    }

    const QString title = cleanTitle(
        rootMap.value(QStringLiteral("title")).toString(),
        cleanTitle(fallbackData.value(QStringLiteral("title")).toString(),
                   QStringLiteral("YOUTUBE PLAYLIST")));
    savePlaylistCache(playlistUrl, title, items);
    emit playlistLoaded(title, items);
}

void YouTubePlaylistBackend::onSettingChanged(const QString &moduleId,
                                              const QString &key,
                                              const QVariant &value)
{
    Q_UNUSED(value)
    if (moduleId != QLatin1String(kModuleId))
        return;

    if (key == QLatin1String("playlist_input")) {
        clearPlaylistCache();
        emit authStateChanged();
        emit dynamicOptionsReady(QStringLiteral("remove_playlist_id"), playlistRemovalOptions());
        emit dynamicOptionsReady(QStringLiteral("rename_playlist_id"), playlistRemovalOptions());
    } else if (key == QLatin1String("playlists")) {
        emit authStateChanged();
        emit dynamicOptionsReady(QStringLiteral("remove_playlist_id"), playlistRemovalOptions());
        emit dynamicOptionsReady(QStringLiteral("rename_playlist_id"), playlistRemovalOptions());
    } else if (key == QLatin1String("commercial_library_updated_ms")) {
        emit dynamicOptionsReady(QStringLiteral("public_access_commercial_categories"),
                                 commercialCategoryOptions());
        emit dynamicOptionsReady(QStringLiteral("vod_commercial_categories"),
                                 commercialCategoryOptions());
    }
}
