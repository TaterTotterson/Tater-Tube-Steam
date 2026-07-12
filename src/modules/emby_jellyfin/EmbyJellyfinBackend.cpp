#include "EmbyJellyfinBackend.h"

#include <algorithm>
#include <QCollator>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFileDevice>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonValue>
#include <QScreen>
#include <QSet>
#include <QStringList>
#include <QSysInfo>
#include <QUrlQuery>
#include <QUuid>

#include <memory>

static const QString kModuleId = QStringLiteral("com.240mp.emby_jellyfin");
static const QString kAuthFile = QStringLiteral("/emby_jellyfin_auth.json");
static const QString kPlexAuthFile = QStringLiteral("/plex_auth.json");
static const QString kOtaVideoQuality = QStringLiteral("480p");
static const QString kProviderPlex = QStringLiteral("PLEX");
static const QString kProviderEmby = QStringLiteral("EMBY/JELLYFIN");

namespace {
struct PlaybackLimits {
    int maxWidth = 854;
    int maxHeight = 480;
    int videoBitRate = 2000000;
    int audioBitRate = 128000;
    int maxStreamingBitrate = 2128000;
};

PlaybackLimits playbackLimitsFor(const QString &quality, bool forceTranscode) {
    PlaybackLimits limits;
    if (!forceTranscode) {
        limits.maxWidth = 0;
        limits.maxHeight = 0;
        limits.videoBitRate = 0;
        limits.maxStreamingBitrate = 100000000;
        return limits;
    }

    if (quality == "2160p") {
        limits.maxWidth = 3840;
        limits.maxHeight = 2160;
        limits.videoBitRate = 20000000;
    } else if (quality == "1440p") {
        limits.maxWidth = 2560;
        limits.maxHeight = 1440;
        limits.videoBitRate = 12000000;
    } else if (quality == "1080p") {
        limits.maxWidth = 1920;
        limits.maxHeight = 1080;
        limits.videoBitRate = 8000000;
    } else if (quality == "720p") {
        limits.maxWidth = 1280;
        limits.maxHeight = 720;
        limits.videoBitRate = 4000000;
    }
    limits.maxStreamingBitrate = limits.videoBitRate + limits.audioBitRate;
    return limits;
}

int streamIndexFromId(const QString &streamId, int fallback = -1) {
    bool ok = false;
    const int value = streamId.toInt(&ok);
    return ok ? value : fallback;
}

QString abbreviatedNetworkBody(const QByteArray &body) {
    return QString::fromUtf8(body.left(240)).simplified();
}

QString serverMessageFromJson(const QJsonObject &data) {
    const QStringList keys = {
        QStringLiteral("Message"),
        QStringLiteral("ErrorMessage"),
        QStringLiteral("Error"),
        QStringLiteral("error"),
        QStringLiteral("message")
    };
    for (const QString &key : keys) {
        const QString value = data.value(key).toString().trimmed();
        if (!value.isEmpty())
            return value;
    }
    return {};
}

bool runningOnRaspberryPi3() {
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

QString displayAdaptiveVideoQuality() {
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen)
        return QStringLiteral("480p");

    const QSize logicalSize = screen->geometry().size();
    const qreal scale = screen->devicePixelRatio();
    const int width = qMax(1, qRound(logicalSize.width() * scale));
    const int height = qMax(1, qRound(logicalSize.height() * scale));
    const int longSide = qMax(width, height);
    const int shortSide = qMin(width, height);

    if (shortSide >= 1800 || longSide >= 3200)
        return QStringLiteral("2160p");
    if (shortSide >= 1200 || longSide >= 2200)
        return QStringLiteral("1440p");
    if (shortSide >= 900 || longSide >= 1600)
        return QStringLiteral("1080p");
    if (shortSide >= 650 || longSide >= 1100)
        return QStringLiteral("720p");
    return QStringLiteral("480p");
}

bool isSupportedVideoQuality(const QString &quality) {
    return quality == QStringLiteral("2160p") ||
           quality == QStringLiteral("1440p") ||
           quality == QStringLiteral("1080p") ||
           quality == QStringLiteral("720p") ||
           quality == QStringLiteral("480p");
}

QString musicArtistName(const QJsonObject &item) {
    QString artist = item["AlbumArtist"].toString();
    if (artist.isEmpty()) {
        const QJsonArray albumArtists = item["AlbumArtists"].toArray();
        if (!albumArtists.isEmpty())
            artist = albumArtists.at(0).toString();
    }
    if (artist.isEmpty()) {
        const QJsonArray artists = item["Artists"].toArray();
        if (!artists.isEmpty())
            artist = artists.at(0).toString();
    }
    if (artist.isEmpty()) {
        const QJsonArray artistItems = item["ArtistItems"].toArray();
        if (!artistItems.isEmpty())
            artist = artistItems.at(0).toObject()["Name"].toString();
    }
    return artist;
}

QStringList jsonStringList(const QJsonValue &value) {
    QStringList out;
    for (const QJsonValue &entry : value.toArray()) {
        const QString text = entry.toString().trimmed();
        if (!text.isEmpty())
            out.append(text);
    }
    out.removeDuplicates();
    return out;
}

QStringList plexGenresFromElement(QXmlStreamReader *xml) {
    QStringList genres;
    if (!xml || !xml->isStartElement())
        return genres;

    int depth = 1;
    while (!xml->atEnd() && depth > 0) {
        xml->readNext();
        if (xml->isStartElement()) {
            if (xml->name() == QStringLiteral("Genre")) {
                const QString tag = xml->attributes()
                    .value(QStringLiteral("tag")).toString().trimmed();
                if (!tag.isEmpty())
                    genres.append(tag);
            }
            ++depth;
        } else if (xml->isEndElement()) {
            --depth;
        }
    }

    genres.removeDuplicates();
    return genres;
}

QStringList normalizedApiTypes(const QStringList &types) {
    QSet<QString> result;
    for (QString type : types) {
        type = type.trimmed().toLower();
        type.replace('-', '_');
        type.replace(' ', '_');
        if (type == QStringLiteral("movies"))
            type = QStringLiteral("movie");
        else if (type == QStringLiteral("tv") || type == QStringLiteral("tv_show") ||
                 type == QStringLiteral("series") || type == QStringLiteral("shows"))
            type = QStringLiteral("show");
        else if (type == QStringLiteral("episodes"))
            type = QStringLiteral("episode");
        else if (type == QStringLiteral("videos"))
            type = QStringLiteral("video");

        if (type == QStringLiteral("movie") || type == QStringLiteral("show") ||
            type == QStringLiteral("episode") || type == QStringLiteral("video"))
            result.insert(type);
    }

    if (result.isEmpty()) {
        result.insert(QStringLiteral("movie"));
        result.insert(QStringLiteral("show"));
        result.insert(QStringLiteral("episode"));
        result.insert(QStringLiteral("video"));
    }

    QStringList out = result.values();
    out.sort();
    return out;
}

bool apiTypeAllowed(const QString &type, const QStringList &types) {
    return types.contains(type);
}

QString embyIncludeTypesForApi(const QStringList &types) {
    QStringList include;
    if (types.contains(QStringLiteral("movie")))
        include << QStringLiteral("Movie");
    if (types.contains(QStringLiteral("show")))
        include << QStringLiteral("Series");
    if (types.contains(QStringLiteral("episode")))
        include << QStringLiteral("Episode");
    if (types.contains(QStringLiteral("video")))
        include << QStringLiteral("Video");
    include.removeDuplicates();
    return include.join(',');
}

QVariantMap firstPlayableEpisodeFromItems(const QVariantList &items) {
    QVariantMap target;
    for (const QVariant &v : items) {
        const QVariantMap item = v.toMap();
        if (item.value(QStringLiteral("viewOffset")).toInt() > 0)
            return item;
    }
    for (const QVariant &v : items) {
        const QVariantMap item = v.toMap();
        if (item.value(QStringLiteral("viewCount")).toInt() == 0)
            return item;
    }
    if (!items.isEmpty())
        target = items.first().toMap();
    return target;
}

bool isVodMovieType(const QString &type) {
    return type == QStringLiteral("movie") || type == QStringLiteral("video");
}

bool isVodShowType(const QString &type) {
    return type == QStringLiteral("show");
}

bool isVodEpisodeType(const QString &type) {
    return type == QStringLiteral("episode");
}

int episodeSortValue(const QVariantMap &item, const QString &key) {
    return item.value(key).toInt();
}

void sortEpisodeList(QVariantList *episodes) {
    if (!episodes)
        return;
    std::sort(episodes->begin(), episodes->end(),
              [](const QVariant &a, const QVariant &b) {
        const QVariantMap am = a.toMap();
        const QVariantMap bm = b.toMap();
        const int aSeason = episodeSortValue(am, QStringLiteral("parentIndex"));
        const int bSeason = episodeSortValue(bm, QStringLiteral("parentIndex"));
        if (aSeason != bSeason)
            return aSeason < bSeason;
        const int aEpisode = episodeSortValue(am, QStringLiteral("index"));
        const int bEpisode = episodeSortValue(bm, QStringLiteral("index"));
        if (aEpisode != bEpisode)
            return aEpisode < bEpisode;
        return am.value(QStringLiteral("title")).toString() <
               bm.value(QStringLiteral("title")).toString();
    });
}

QVariantMap vodProgramFromItem(QVariantMap item) {
    item.remove(QStringLiteral("summary"));
    return item;
}

QVariantMap vodChannel(const QString &title,
                       const QString &channelType,
                       const QVariantList &programs,
                       const QString &commercialCategory = QString()) {
    QVariantMap channel{
        {QStringLiteral("title"), title.toUpper()},
        {QStringLiteral("channelType"), channelType},
        {QStringLiteral("programs"), programs}
    };
    const QString cleanCategory = commercialCategory.trimmed();
    if (!cleanCategory.isEmpty())
        channel[QStringLiteral("commercialCategory")] = cleanCategory;
    return channel;
}

QStringList vodGenres(const QVariantMap &item) {
    const QVariant raw = item.value(QStringLiteral("genres"));
    QStringList genres = raw.toStringList();
    if (genres.isEmpty()) {
        for (const QVariant &value : raw.toList()) {
            const QString text = value.toString().trimmed();
            if (!text.isEmpty())
                genres.append(text);
        }
    }
    return genres;
}

bool vodHasAnyGenre(const QVariantMap &item, const QStringList &terms) {
    const QStringList genres = vodGenres(item);
    for (const QString &genre : genres) {
        const QString normalized = genre.toLower();
        for (const QString &term : terms) {
            if (normalized.contains(term))
                return true;
        }
    }
    return false;
}

int vodYear(const QVariantMap &item) {
    bool ok = false;
    const int year = item.value(QStringLiteral("year")).toInt(&ok);
    return ok ? year : 0;
}

QVariantList filterVodPrograms(const QVariantList &programs,
                               const std::function<bool(const QVariantMap &)> &predicate) {
    QVariantList result;
    for (const QVariant &value : programs) {
        const QVariantMap item = value.toMap();
        if (predicate(item))
            result.append(item);
    }
    return result;
}

QString vodChannelSignature(const QString &channelType, const QVariantList &programs) {
    QStringList keys;
    for (const QVariant &value : programs) {
        const QVariantMap item = value.toMap();
        QString key = item.value(QStringLiteral("ratingKey")).toString();
        if (key.isEmpty())
            key = item.value(QStringLiteral("seriesKey")).toString();
        if (key.isEmpty())
            key = item.value(QStringLiteral("title")).toString();
        if (!key.isEmpty())
            keys.append(key);
    }
    keys.removeDuplicates();
    keys.sort();
    return channelType + QLatin1Char(':') + keys.join(QLatin1Char('|'));
}

bool isGenericVodLibraryTitle(const QString &title) {
    const QString normalized = title.trimmed().toUpper();
    static const QSet<QString> genericTitles = {
        QStringLiteral("MOVIE"),
        QStringLiteral("MOVIES"),
        QStringLiteral("FILM"),
        QStringLiteral("FILMS"),
        QStringLiteral("TV"),
        QStringLiteral("TV SHOWS"),
        QStringLiteral("SHOWS"),
        QStringLiteral("SERIES"),
        QStringLiteral("VIDEO"),
        QStringLiteral("VIDEOS"),
        QStringLiteral("VIDEO ON DEMAND"),
        QStringLiteral("VOD")
    };
    return normalized.isEmpty() || genericTitles.contains(normalized);
}

QString themedVodTitle(const QString &libraryTitle, const QString &channelTitle) {
    const QString cleanChannel = channelTitle.trimmed().toUpper();
    const QString cleanLibrary = libraryTitle.trimmed().toUpper();
    if (isGenericVodLibraryTitle(cleanLibrary))
        return cleanChannel;
    if (cleanChannel.startsWith(cleanLibrary))
        return cleanChannel;
    return cleanLibrary + QLatin1Char(' ') + cleanChannel;
}

void appendVodChannelIfUseful(QVariantList *channels,
                              QSet<QString> *signatures,
                              const QString &title,
                              const QString &channelType,
                              const QVariantList &programs,
                              int minCount) {
    if (!channels || !signatures || programs.size() < minCount)
        return;

    const QString signature = vodChannelSignature(channelType, programs);
    if (signature.endsWith(QLatin1Char(':')) || signatures->contains(signature))
        return;

    channels->append(vodChannel(title, channelType, programs));
    signatures->insert(signature);
}

struct VodGenreChannelRule {
    QString title;
    QStringList terms;
    int minCount = 1;
};

QString decadeChannelLabel(int decade) {
    if (decade >= 2000)
        return QString::number(decade) + QStringLiteral("S");
    return QString::number(decade).right(2) + QStringLiteral("S");
}

void appendThemedMovieChannels(QVariantList *channels,
                               QSet<QString> *signatures,
                               const QString &libraryTitle,
                               const QVariantList &movies) {
    static const QList<VodGenreChannelRule> rules = {
        {QStringLiteral("ACTION MOVIES"), {QStringLiteral("action"), QStringLiteral("adventure"), QStringLiteral("thriller")}, 2},
        {QStringLiteral("COMEDY MOVIES"), {QStringLiteral("comedy")}, 2},
        {QStringLiteral("HORROR MOVIES"), {QStringLiteral("horror")}, 2},
        {QStringLiteral("SCI-FI MOVIES"), {QStringLiteral("science fiction"), QStringLiteral("sci-fi"), QStringLiteral("sci fi")}, 2},
        {QStringLiteral("FANTASY MOVIES"), {QStringLiteral("fantasy")}, 2},
        {QStringLiteral("FAMILY MOVIES"), {QStringLiteral("family"), QStringLiteral("children"), QStringLiteral("kids")}, 2},
        {QStringLiteral("CARTOON MOVIES"), {QStringLiteral("animation"), QStringLiteral("anime")}, 2},
        {QStringLiteral("DOCUMENTARY MOVIES"), {QStringLiteral("documentary")}, 2},
        {QStringLiteral("DRAMA MOVIES"), {QStringLiteral("drama")}, 2},
        {QStringLiteral("CRIME MOVIES"), {QStringLiteral("crime"), QStringLiteral("mystery")}, 2}
    };

    for (const VodGenreChannelRule &rule : rules) {
        const QVariantList filtered = filterVodPrograms(movies, [&rule](const QVariantMap &item) {
            return vodHasAnyGenre(item, rule.terms);
        });
        appendVodChannelIfUseful(channels, signatures,
                                 themedVodTitle(libraryTitle, rule.title),
                                 QStringLiteral("movie"), filtered, rule.minCount);
    }

    const QVariantList classic = filterVodPrograms(movies, [](const QVariantMap &item) {
        const int year = vodYear(item);
        return year > 0 && year <= 1979;
    });
    appendVodChannelIfUseful(channels, signatures,
                             themedVodTitle(libraryTitle, QStringLiteral("CLASSIC MOVIES")),
                             QStringLiteral("movie"), classic, 2);

    for (int decade : {1950, 1960, 1970, 1980, 1990, 2000, 2010, 2020}) {
        const QVariantList filtered = filterVodPrograms(movies, [decade](const QVariantMap &item) {
            const int year = vodYear(item);
            return year >= decade && year < decade + 10;
        });
        appendVodChannelIfUseful(channels, signatures,
                                 themedVodTitle(libraryTitle, decadeChannelLabel(decade) + QStringLiteral(" MOVIES")),
                                 QStringLiteral("movie"), filtered, 2);
    }
}

void appendThemedTvChannels(QVariantList *channels,
                            QSet<QString> *signatures,
                            const QString &libraryTitle,
                            const QVariantList &showGroups) {
    static const QList<VodGenreChannelRule> rules = {
        {QStringLiteral("CARTOON CHANNEL"), {QStringLiteral("animation"), QStringLiteral("anime"), QStringLiteral("children"), QStringLiteral("kids")}, 1},
        {QStringLiteral("COMEDY CHANNEL"), {QStringLiteral("comedy")}, 1},
        {QStringLiteral("DRAMA CHANNEL"), {QStringLiteral("drama")}, 1},
        {QStringLiteral("SCI-FI CHANNEL"), {QStringLiteral("science fiction"), QStringLiteral("sci-fi"), QStringLiteral("sci fi")}, 1},
        {QStringLiteral("ACTION CHANNEL"), {QStringLiteral("action"), QStringLiteral("adventure")}, 1},
        {QStringLiteral("CRIME CHANNEL"), {QStringLiteral("crime"), QStringLiteral("mystery")}, 1},
        {QStringLiteral("REALITY CHANNEL"), {QStringLiteral("reality")}, 1},
        {QStringLiteral("DOCUMENTARY CHANNEL"), {QStringLiteral("documentary")}, 1}
    };

    for (const VodGenreChannelRule &rule : rules) {
        const QVariantList filtered = filterVodPrograms(showGroups, [&rule](const QVariantMap &item) {
            return vodHasAnyGenre(item, rule.terms);
        });
        appendVodChannelIfUseful(channels, signatures,
                                 themedVodTitle(libraryTitle, rule.title),
                                 QStringLiteral("tv"), filtered, rule.minCount);
    }

    const QVariantList classic = filterVodPrograms(showGroups, [](const QVariantMap &item) {
        const int year = vodYear(item);
        return year > 0 && year <= 1979;
    });
    appendVodChannelIfUseful(channels, signatures,
                             themedVodTitle(libraryTitle, QStringLiteral("CLASSIC TV")),
                             QStringLiteral("tv"), classic, 1);

    for (int decade : {1950, 1960, 1970, 1980, 1990, 2000, 2010, 2020}) {
        const QVariantList filtered = filterVodPrograms(showGroups, [decade](const QVariantMap &item) {
            const int year = vodYear(item);
            return year >= decade && year < decade + 10;
        });
        appendVodChannelIfUseful(channels, signatures,
                                 themedVodTitle(libraryTitle, decadeChannelLabel(decade) + QStringLiteral(" TV")),
                                 QStringLiteral("tv"), filtered, 1);
    }
}
}

EmbyJellyfinBackend::EmbyJellyfinBackend(const QString &appRoot,
                                         const QString &dataRoot,
                                         QObject *parent)
    : QObject(parent), m_appRoot(appRoot), m_dataRoot(dataRoot)
{
    m_nam = new QNetworkAccessManager(this);
}

QJsonObject EmbyJellyfinBackend::loadAuth() const {
    QFile f(m_dataRoot + kAuthFile);
    if (!f.open(QIODevice::ReadOnly)) return {};

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return {};
    return doc.object();
}

void EmbyJellyfinBackend::saveAuth(const QJsonObject &auth) const {
    QFile f(m_dataRoot + kAuthFile);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("[EmbyJellyfinBackend] Could not write auth file: %s",
                 qPrintable(f.errorString()));
        return;
    }
    f.write(QJsonDocument(auth).toJson(QJsonDocument::Indented));
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

QJsonObject EmbyJellyfinBackend::loadPlexAuth() const {
    QFile f(m_dataRoot + kPlexAuthFile);
    if (!f.open(QIODevice::ReadOnly)) return {};

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return {};
    return doc.object();
}

void EmbyJellyfinBackend::savePlexAuth(const QJsonObject &auth) const {
    QFile f(m_dataRoot + kPlexAuthFile);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("[EmbyJellyfinBackend] Could not write Plex auth file: %s",
                 qPrintable(f.errorString()));
        return;
    }
    f.write(QJsonDocument(auth).toJson(QJsonDocument::Indented));
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

QJsonObject EmbyJellyfinBackend::loadConfig() const {
    QFile f(m_dataRoot + "/config.json");
    if (f.open(QIODevice::ReadOnly)) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error == QJsonParseError::NoError && doc.isObject())
            return doc.object();
    }
    return QJsonObject{
        {"app", QJsonObject{{"color_scheme","Off Air"}}},
        {"modules", QJsonObject{}}
    };
}

QString EmbyJellyfinBackend::normalizeServerUrl(const QString &raw) {
    QString url = raw.trimmed();
    if (url.isEmpty()) return {};
    if (!url.startsWith("http://", Qt::CaseInsensitive) &&
        !url.startsWith("https://", Qt::CaseInsensitive)) {
        url = "http://" + url;
    }

    QUrl parsed(url);
    if (!parsed.isValid() || parsed.scheme().isEmpty() || parsed.host().isEmpty())
        return {};

    parsed.setQuery(QString());
    parsed.setFragment({});

    QString path = parsed.path();
    while (path.endsWith('/') && path != QStringLiteral("/"))
        path.chop(1);

    const QString lowerPath = path.toLower();
    int webIndex = -1;
    if (lowerPath == QStringLiteral("/web") ||
        lowerPath.startsWith(QStringLiteral("/web/"))) {
        webIndex = 0;
    } else if (lowerPath.endsWith(QStringLiteral("/web"))) {
        webIndex = path.size() - 4;
    } else {
        webIndex = lowerPath.indexOf(QStringLiteral("/web/"));
    }

    if (webIndex >= 0)
        parsed.setPath(path.left(webIndex));

    QString normalized = parsed.toString(QUrl::RemoveQuery | QUrl::RemoveFragment);
    while (normalized.endsWith('/'))
        normalized.chop(1);
    return normalized;
}

QString EmbyJellyfinBackend::clientId() const {
    if (!m_clientId.isEmpty()) return m_clientId;

    QJsonObject auth = loadAuth();
    QString id = auth["client_identifier"].toString();
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        auth["client_identifier"] = id;
        saveAuth(auth);
    }
    m_clientId = id;
    return m_clientId;
}

QString EmbyJellyfinBackend::plexClientId() const {
    if (!m_plexClientId.isEmpty()) return m_plexClientId;

    QJsonObject auth = loadPlexAuth();
    QString id = auth["client_identifier"].toString();
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        auth["client_identifier"] = id;
        savePlexAuth(auth);
    }
    m_plexClientId = id;
    return m_plexClientId;
}

QString EmbyJellyfinBackend::mediaProvider() const {
    const QString raw = loadConfig()["modules"].toObject()
        [kModuleId].toObject()["media_provider"].toString(kProviderEmby);
    return raw.trimmed().toUpper() == kProviderPlex ? kProviderPlex : kProviderEmby;
}

QString EmbyJellyfinBackend::serverUrl() const {
    return loadAuth()["server_url"].toString();
}

QString EmbyJellyfinBackend::plexServerUrl() const {
    return loadPlexAuth()["server_url"].toString();
}

QString EmbyJellyfinBackend::accessToken() const {
    return loadAuth()["access_token"].toString();
}

QString EmbyJellyfinBackend::plexToken() const {
    return loadPlexAuth()["access_token"].toString();
}

QString EmbyJellyfinBackend::userId() const {
    return loadAuth()["user_id"].toString();
}

QString EmbyJellyfinBackend::videoQuality() const {
    if (runningOnRaspberryPi3())
        return QStringLiteral("480p");

    QJsonObject cfg = loadConfig();
    const QString configured = cfg["modules"].toObject()[kModuleId].toObject()["video_quality"]
        .toString(QStringLiteral("auto"))
        .trimmed()
        .toLower();
    if (configured.isEmpty() || configured == QStringLiteral("auto"))
        return displayAdaptiveVideoQuality();
    if (isSupportedVideoQuality(configured))
        return configured;
    return displayAdaptiveVideoQuality();
}

QNetworkRequest EmbyJellyfinBackend::apiRequest(const QUrl &url,
                                                const QString &token) const {
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");

    QString auth = QStringLiteral(
        "MediaBrowser Client=\"Tater Tube\", Device=\"%1\", DeviceId=\"%2\", Version=\"%3\"")
        .arg(QSysInfo::machineHostName().isEmpty() ? QStringLiteral("Tater Tube")
                                                   : QSysInfo::machineHostName(),
             clientId(),
             QCoreApplication::applicationVersion());
    QString tok = token.isEmpty() ? accessToken() : token;
    if (!tok.isEmpty())
        auth += QStringLiteral(", Token=\"%1\"").arg(tok);

    req.setRawHeader("X-Emby-Authorization", auth.toUtf8());
    if (!tok.isEmpty()) {
        req.setRawHeader("X-Emby-Token", tok.toUtf8());
        req.setRawHeader("X-MediaBrowser-Token", tok.toUtf8());
    }
    return req;
}

QNetworkReply *EmbyJellyfinBackend::apiGet(const QUrl &url, const QString &token) {
    return m_nam->get(apiRequest(url, token));
}

QNetworkReply *EmbyJellyfinBackend::apiPostJson(const QUrl &url, const QString &token,
                                                const QByteArray &body) {
    QNetworkRequest req = apiRequest(url, token);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    return m_nam->post(req, body);
}

QNetworkReply *EmbyJellyfinBackend::apiPostJson(const QUrl &url,
                                                const QByteArray &body) {
    return apiPostJson(url, {}, body);
}

QUrl EmbyJellyfinBackend::apiUrl(const QString &path) const {
    QString base = serverUrl();
    if (base.isEmpty()) return {};
    return QUrl(base + path);
}

QNetworkRequest EmbyJellyfinBackend::plexTvRequest(const QUrl &url,
                                                   const QString &token) const {
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("X-Plex-Product", "Tater Tube");
    req.setRawHeader("X-Plex-Version", QCoreApplication::applicationVersion().toUtf8());
    req.setRawHeader("X-Plex-Client-Identifier", plexClientId().toUtf8());
    req.setRawHeader("X-Plex-Platform", "Qt");
    req.setRawHeader("X-Plex-Device", "Tater Tube");
    req.setRawHeader("X-Plex-Device-Name",
                     QSysInfo::machineHostName().isEmpty()
                         ? QByteArray("Tater Tube")
                         : QSysInfo::machineHostName().toUtf8());
    req.setRawHeader("X-Plex-Provides", "player");
    if (!token.isEmpty())
        req.setRawHeader("X-Plex-Token", token.toUtf8());
    return req;
}

QNetworkReply *EmbyJellyfinBackend::plexTvGet(const QUrl &url, const QString &token) {
    return m_nam->get(plexTvRequest(url, token));
}

QNetworkReply *EmbyJellyfinBackend::plexTvPostForm(const QUrl &url,
                                                   const QByteArray &body,
                                                   const QString &token) {
    QNetworkRequest req = plexTvRequest(url, token);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    return m_nam->post(req, body);
}

QNetworkRequest EmbyJellyfinBackend::plexServerRequest(const QUrl &url) const {
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/xml");
    req.setRawHeader("X-Plex-Product", "Tater Tube");
    req.setRawHeader("X-Plex-Version", QCoreApplication::applicationVersion().toUtf8());
    req.setRawHeader("X-Plex-Client-Identifier", plexClientId().toUtf8());
    req.setRawHeader("X-Plex-Token", plexToken().toUtf8());
    return req;
}

QNetworkReply *EmbyJellyfinBackend::plexServerGet(const QUrl &url) {
    return m_nam->get(plexServerRequest(plexUrlWithToken(url)));
}

QUrl EmbyJellyfinBackend::plexApiUrl(const QString &path) const {
    const QString base = plexServerUrl();
    if (base.isEmpty()) return {};
    return QUrl(base + path);
}

QUrl EmbyJellyfinBackend::plexUrlWithToken(QUrl url) const {
    const QString token = plexToken();
    if (token.isEmpty()) return url;
    QUrlQuery q(url);
    if (!q.hasQueryItem("X-Plex-Token"))
        q.addQueryItem("X-Plex-Token", token);
    url.setQuery(q);
    return url;
}

QString EmbyJellyfinBackend::plexAbsoluteUrl(const QString &pathOrUrl) const {
    if (pathOrUrl.startsWith("http://", Qt::CaseInsensitive) ||
        pathOrUrl.startsWith("https://", Qt::CaseInsensitive))
        return plexUrlWithToken(QUrl(pathOrUrl)).toString();
    return plexUrlWithToken(plexApiUrl(pathOrUrl)).toString();
}

qint64 EmbyJellyfinBackend::ticksToMs(const QJsonValue &ticks) {
    return static_cast<qint64>(ticks.toDouble() / 10000.0);
}

qint64 EmbyJellyfinBackend::msToTicks(int ms) {
    return static_cast<qint64>(ms) * 10000;
}

QString EmbyJellyfinBackend::plexAttr(const QXmlStreamAttributes &attrs,
                                      const QString &name) {
    return attrs.value(name).toString();
}

int EmbyJellyfinBackend::plexIntAttr(const QXmlStreamAttributes &attrs,
                                     const QString &name,
                                     int fallback) {
    bool ok = false;
    const int value = attrs.value(name).toInt(&ok);
    return ok ? value : fallback;
}

QString EmbyJellyfinBackend::msToDisplay(int ms) {
    int sec = ms / 1000;
    int h = sec / 3600;
    int m = (sec % 3600) / 60;
    if (h > 0) return QString("%1HR:%2MIN").arg(h).arg(m, 2, 10, QChar('0'));
    return QString("%1MIN").arg(m);
}

QString EmbyJellyfinBackend::itemType(const QJsonObject &item) {
    const QString type = item["Type"].toString();
    if (type == "Movie") return "movie";
    if (type == "Series") return "show";
    if (type == "Season") return "season";
    if (type == "Episode") return "episode";
    if (type == "BoxSet" || type == "CollectionFolder") return "collection";
    if (type == "Playlist") return "playlist";
    return "video";
}

QString EmbyJellyfinBackend::get_auth_state() {
    if (mediaProvider() == kProviderPlex) {
        QJsonObject auth = loadPlexAuth();
        return auth["server_url"].toString().isEmpty() ||
               auth["access_token"].toString().isEmpty()
            ? QStringLiteral("none")
            : QStringLiteral("authed");
    }

    QJsonObject auth = loadAuth();
    return auth["server_url"].toString().isEmpty() ||
           auth["access_token"].toString().isEmpty() ||
           auth["user_id"].toString().isEmpty()
        ? QStringLiteral("none")
        : QStringLiteral("authed");
}

QString EmbyJellyfinBackend::get_active_user_name() {
    if (mediaProvider() == kProviderPlex)
        return loadPlexAuth()["username"].toString();
    return loadAuth()["username"].toString();
}

QString EmbyJellyfinBackend::get_active_server_name() {
    if (mediaProvider() == kProviderPlex) {
        QJsonObject auth = loadPlexAuth();
        QString name = auth["server_name"].toString();
        return name.isEmpty() ? auth["server_url"].toString() : name;
    }

    QJsonObject auth = loadAuth();
    QString name = auth["server_name"].toString();
    return name.isEmpty() ? auth["server_url"].toString() : name;
}

QString EmbyJellyfinBackend::get_saved_server_url() {
    if (mediaProvider() == kProviderPlex)
        return loadPlexAuth()["server_url"].toString();
    return loadAuth()["server_url"].toString();
}

QString EmbyJellyfinBackend::get_media_provider() {
    return mediaProvider();
}

void EmbyJellyfinBackend::onSettingChanged(const QString &moduleId,
                                           const QString &key,
                                           const QVariant &value) {
    Q_UNUSED(value)
    if (moduleId != kModuleId || key != QLatin1String("media_provider"))
        return;

    m_plexPinId = 0;
    m_plexPinCode.clear();
    m_pendingPlexToken.clear();
    m_pendingPlexServers.clear();
    emit authStateChanged();
}

void EmbyJellyfinBackend::login(const QString &rawServerUrl,
                                const QString &username,
                                const QString &password) {
    const QString normalized = normalizeServerUrl(rawServerUrl);
    if (normalized.isEmpty() || username.trimmed().isEmpty()) {
        emit errorOccurred("SERVER URL AND USERNAME ARE REQUIRED");
        return;
    }

    QJsonObject body{
        {"Username", username.trimmed()},
        {"Pw", password}
    };

    QJsonObject auth = loadAuth();
    auth["server_url"] = normalized;
    auth.remove("access_token");
    auth.remove("user_id");
    auth.remove("username");
    auth.remove("server_name");
    auth.remove("server_product");
    saveAuth(auth);

    QUrl url(normalized + "/Users/AuthenticateByName");
    auto *reply = apiPostJson(url, {}, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, normalized]() {
        reply->deleteLater();
        QByteArray bytes = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            QString message = "SIGN IN FAILED: " + reply->errorString();
            const QString body = abbreviatedNetworkBody(bytes);
            if (!body.isEmpty())
                message += " - " + body;
            emit errorOccurred(message);
            return;
        }

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            QString message = "SIGN IN FAILED: INVALID AUTH RESPONSE";
            const QString body = abbreviatedNetworkBody(bytes);
            if (!body.isEmpty())
                message += " - " + body;
            qWarning("[EmbyJellyfinBackend] Invalid auth response from %s: %s",
                     qPrintable(normalized),
                     qPrintable(body));
            emit errorOccurred(message);
            return;
        }

        QJsonObject data = doc.object();
        QString token = data["AccessToken"].toString();
        QJsonObject user = data["User"].toObject();
        QString uid = user["Id"].toString();
        if (uid.isEmpty())
            uid = data["SessionInfo"].toObject()["UserId"].toString();

        if (token.isEmpty() || uid.isEmpty()) {
            QString message = "SIGN IN FAILED: EMPTY AUTH RESPONSE";
            const QString serverMessage = serverMessageFromJson(data);
            if (!serverMessage.isEmpty())
                message += " - " + serverMessage;
            qWarning("[EmbyJellyfinBackend] Empty auth response from %s. token=%s uid=%s keys=%s",
                     qPrintable(normalized),
                     token.isEmpty() ? "missing" : "present",
                     uid.isEmpty() ? "missing" : "present",
                     qPrintable(data.keys().join(',')));
            emit errorOccurred(message);
            return;
        }

        QJsonObject auth = loadAuth();
        auth["server_url"] = normalized;
        auth["access_token"] = token;
        auth["user_id"] = uid;
        auth["username"] = user["Name"].toString();
        saveAuth(auth);
        fetchServerInfoThenFinishLogin(auth);
    });
}

void EmbyJellyfinBackend::fetchServerInfoThenFinishLogin(QJsonObject auth) {
    auto *reply = apiGet(apiUrl("/System/Info"), auth["access_token"].toString());
    connect(reply, &QNetworkReply::finished, this, [this, reply, auth]() mutable {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            QJsonObject info = QJsonDocument::fromJson(reply->readAll()).object();
            QString serverName = info["ServerName"].toString();
            if (serverName.isEmpty()) serverName = info["LocalAddress"].toString();
            if (!serverName.isEmpty()) auth["server_name"] = serverName;
            QString product = info["ProductName"].toString();
            if (!product.isEmpty()) auth["server_product"] = product;
            saveAuth(auth);
        }

        emit authSuccess();
        emit authStateChanged();
    });
}

void EmbyJellyfinBackend::start_plex_pin_login() {
    m_plexPinId = 0;
    m_plexPinCode.clear();
    m_pendingPlexToken.clear();
    m_pendingPlexServers.clear();

    QUrlQuery form;
    form.addQueryItem("strong", "false");
    auto *reply = plexTvPostForm(QUrl("https://plex.tv/api/v2/pins"),
                                 form.query(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QByteArray bytes = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("PLEX SIGN IN FAILED: " + reply->errorString());
            return;
        }

        const QJsonObject data = QJsonDocument::fromJson(bytes).object();
        m_plexPinId = data["id"].toInt();
        m_plexPinCode = data["code"].toString();
        if (m_plexPinId <= 0 || m_plexPinCode.isEmpty()) {
            emit errorOccurred("PLEX SIGN IN FAILED: EMPTY PIN RESPONSE");
            return;
        }

        emit plexPinReady(m_plexPinCode, QStringLiteral("https://plex.tv/link"));
    });
}

void EmbyJellyfinBackend::poll_plex_pin_login() {
    if (m_plexPinId <= 0 || m_plexPinCode.isEmpty())
        return;

    QUrl url(QStringLiteral("https://plex.tv/api/v2/pins/%1").arg(m_plexPinId));
    QUrlQuery q;
    q.addQueryItem("code", m_plexPinCode);
    url.setQuery(q);

    auto *reply = plexTvGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QByteArray bytes = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("PLEX SIGN IN CHECK FAILED: " + reply->errorString());
            return;
        }

        const QJsonObject data = QJsonDocument::fromJson(bytes).object();
        const QString token = data["authToken"].toString();
        if (token.isEmpty())
            return;

        m_plexPinId = 0;
        m_plexPinCode.clear();
        finishPlexLogin(token);
    });
}

void EmbyJellyfinBackend::finishPlexLogin(const QString &token) {
    if (token.isEmpty()) {
        emit errorOccurred("PLEX SIGN IN FAILED: EMPTY TOKEN");
        return;
    }

    QJsonObject auth = loadPlexAuth();
    auth["access_token"] = token;
    auth.remove("server_url");
    auth.remove("server_name");
    auth.remove("machine_identifier");
    savePlexAuth(auth);

    auto *reply = plexTvGet(QUrl("https://plex.tv/api/v2/user"), token);
    connect(reply, &QNetworkReply::finished, this, [this, reply, token]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            const QJsonObject user = QJsonDocument::fromJson(reply->readAll()).object();
            QJsonObject auth = loadPlexAuth();
            auth["access_token"] = token;
            auth["username"] = user["username"].toString(user["email"].toString());
            savePlexAuth(auth);
        }

        fetchPlexServers(token);
    });
}

void EmbyJellyfinBackend::fetchPlexServers(const QString &token) {
    QUrl url("https://plex.tv/api/v2/resources");
    QUrlQuery q;
    q.addQueryItem("includeHttps", "1");
    q.addQueryItem("includeRelay", "0");
    url.setQuery(q);

    auto *reply = plexTvGet(url, token);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QByteArray bytes = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("PLEX SERVER DISCOVERY FAILED: " + reply->errorString());
            return;
        }

        const QJsonArray resources = QJsonDocument::fromJson(bytes).array();
        QVariantList servers;
        for (const auto &v : resources) {
            const QJsonObject resource = v.toObject();
            const QStringList provides = resource["provides"].toString().split(',', Qt::SkipEmptyParts);
            bool providesServer = false;
            for (const QString &part : provides) {
                if (part.trimmed() == QLatin1String("server")) {
                    providesServer = true;
                    break;
                }
            }
            if (!providesServer)
                continue;
            if (!resource["owned"].toBool(false))
                continue;

            const QString name = resource["name"].toString(resource["product"].toString());
            const QString machineId = resource["clientIdentifier"].toString();
            const QJsonArray connections = resource["connections"].toArray();
            QVariantList candidates;
            for (const auto &cv : connections) {
                const QJsonObject c = cv.toObject();
                const QString uri = c["uri"].toString();
                if (uri.isEmpty())
                    continue;
                candidates.append(QVariantMap{
                    {"uri", uri},
                    {"local", c["local"].toBool(false)},
                    {"relay", c["relay"].toBool(false)}
                });
            }

            std::sort(candidates.begin(), candidates.end(), [](const QVariant &a, const QVariant &b) {
                const QVariantMap left = a.toMap();
                const QVariantMap right = b.toMap();
                if (left["local"].toBool() != right["local"].toBool())
                    return left["local"].toBool();
                if (left["relay"].toBool() != right["relay"].toBool())
                    return !left["relay"].toBool();
                return left["uri"].toString() < right["uri"].toString();
            });

            if (candidates.isEmpty())
                continue;

            const QVariantMap best = candidates.first().toMap();
            servers.append(QVariantMap{
                {"machineIdentifier", machineId},
                {"name", name.toUpper()},
                {"url", best["uri"].toString()},
                {"local", best["local"].toBool()},
                {"relay", best["relay"].toBool()}
            });
        }

        if (servers.isEmpty()) {
            emit errorOccurred("NO OWNED PLEX SERVERS FOUND");
            return;
        }

        m_pendingPlexServers = servers;
        if (servers.size() == 1) {
            saveSelectedPlexServer(servers.first().toMap());
            emit authSuccess();
            emit authStateChanged();
            return;
        }

        emit plexServersLoaded(servers);
    });
}

void EmbyJellyfinBackend::select_plex_server(const QString &machineIdentifier) {
    for (const auto &v : m_pendingPlexServers) {
        const QVariantMap server = v.toMap();
        if (server["machineIdentifier"].toString() == machineIdentifier) {
            saveSelectedPlexServer(server);
            emit authSuccess();
            emit authStateChanged();
            return;
        }
    }
    emit errorOccurred("PLEX SERVER WAS NOT FOUND");
}

void EmbyJellyfinBackend::saveSelectedPlexServer(const QVariantMap &server) {
    QJsonObject auth = loadPlexAuth();
    auth["server_url"] = normalizeServerUrl(server["url"].toString());
    auth["server_name"] = server["name"].toString();
    auth["machine_identifier"] = server["machineIdentifier"].toString();
    savePlexAuth(auth);
    m_pendingPlexServers.clear();
}

void EmbyJellyfinBackend::logout() {
    if (mediaProvider() == kProviderPlex) {
        QFile::remove(m_dataRoot + kPlexAuthFile);
        m_plexClientId.clear();
        m_plexPinId = 0;
        m_plexPinCode.clear();
        m_pendingPlexToken.clear();
        m_pendingPlexServers.clear();
        emit logoutComplete();
        emit authStateChanged();
        return;
    }

    QString token = accessToken();
    if (!token.isEmpty()) {
        auto *reply = apiPostJson(apiUrl("/Sessions/Logout"), token, QByteArray("{}"));
        connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    }

    QFile::remove(m_dataRoot + kAuthFile);
    m_clientId.clear();
    emit logoutComplete();
    emit authStateChanged();
}

QUrl EmbyJellyfinBackend::itemListUrl(const QString &parentId,
                                      const QString &includeTypes,
                                      bool recursive) const {
    QUrl url = apiUrl("/Users/" + userId() + "/Items");
    QUrlQuery q;
    if (!parentId.isEmpty()) q.addQueryItem("ParentId", parentId);
    if (!includeTypes.isEmpty()) q.addQueryItem("IncludeItemTypes", includeTypes);
    q.addQueryItem("Recursive", recursive ? "true" : "false");
    q.addQueryItem("SortBy", "SortName");
    q.addQueryItem("SortOrder", "Ascending");
    q.addQueryItem("Fields", "MediaSources,MediaStreams,Overview,Genres,ParentId,PrimaryImageAspectRatio,UserData,RecursiveItemCount,ChildCount,Album,AlbumArtist,AlbumArtists,ArtistItems,Artists");
    q.addQueryItem("ImageTypeLimit", "1");
    q.addQueryItem("EnableImages", "false");
    url.setQuery(q);
    return url;
}

QVariantMap EmbyJellyfinBackend::formatItem(const QJsonObject &item) const {
    const QJsonObject userData = item["UserData"].toObject();
    const QString type = itemType(item);
    const int duration = static_cast<int>(ticksToMs(item["RunTimeTicks"]));

    QVariantMap result{
        {"ratingKey", item["Id"].toString()},
        {"title", item["Name"].toString().toUpper()},
        {"type", type},
        {"year", item["ProductionYear"].toVariant()},
        {"duration", duration},
        {"durationDisplay", msToDisplay(duration)},
        {"summary", item["Overview"].toString()},
        {"viewOffset", static_cast<int>(ticksToMs(userData["PlaybackPositionTicks"]))},
        {"viewCount", userData["Played"].toBool() ? 1 : 0},
        {"leafCount", item["RecursiveItemCount"].toInt(item["ChildCount"].toInt())},
        {"viewedLeafCount", userData["Played"].toBool() ? item["ChildCount"].toInt() : 0},
        {"index", item["IndexNumber"].toInt()},
        {"parentIndex", item["ParentIndexNumber"].toInt()},
        {"parentRatingKey", item["SeasonId"].toString(item["ParentId"].toString())},
        {"grandparentRatingKey", item["SeriesId"].toString()},
        {"grandparentTitle", item["SeriesName"].toString()},
        {"parentTitle", item["SeasonName"].toString()},
        {"originallyAvailableAt", item["PremiereDate"].toString()},
    };

    const QStringList genres = jsonStringList(item.value(QStringLiteral("Genres")));
    if (!genres.isEmpty())
        result[QStringLiteral("genres")] = genres;

    return result;
}

QVariantMap EmbyJellyfinBackend::formatMusicAlbum(const QJsonObject &item) const {
    const int duration = static_cast<int>(ticksToMs(item["RunTimeTicks"]));
    const QString artist = musicArtistName(item).toUpper();

    return QVariantMap{
        {"ratingKey", item["Id"].toString()},
        {"key", item["Id"].toString()},
        {"title", item["Name"].toString().toUpper()},
        {"artist", artist.isEmpty() ? QStringLiteral("UNKNOWN ARTIST") : artist},
        {"type", "album"},
        {"year", item["ProductionYear"].toVariant()},
        {"duration", duration},
        {"durationDisplay", duration > 0 ? msToDisplay(duration) : QString{}},
        {"leafCount", item["RecursiveItemCount"].toInt(item["ChildCount"].toInt())}
    };
}

QVariantMap EmbyJellyfinBackend::formatMusicTrack(const QJsonObject &item) const {
    const int duration = static_cast<int>(ticksToMs(item["RunTimeTicks"]));
    QString artist = musicArtistName(item);

    const QJsonArray mediaSources = item["MediaSources"].toArray();
    const QJsonObject mediaSource = mediaSources.isEmpty()
        ? QJsonObject{}
        : mediaSources.first().toObject();
    QString mediaSourceId = mediaSource["Id"].toString();
    if (mediaSourceId.isEmpty())
        mediaSourceId = item["Id"].toString();

    return QVariantMap{
        {"ratingKey", item["Id"].toString()},
        {"partKey", mediaSourceId},
        {"title", item["Name"].toString().toUpper()},
        {"artist", artist.toUpper()},
        {"album", item["Album"].toString().toUpper()},
        {"type", "track"},
        {"index", item["IndexNumber"].toInt()},
        {"parentIndex", item["ParentIndexNumber"].toInt()},
        {"duration", duration},
        {"durationDisplay", msToDisplay(duration)}
    };
}

QVariantList EmbyJellyfinBackend::formatItems(const QJsonArray &items) const {
    QVariantList result;
    for (const auto &v : items) {
        QJsonObject item = v.toObject();
        if (!item["Id"].toString().isEmpty())
            result.append(formatItem(item));
    }
    return result;
}

QVariantMap EmbyJellyfinBackend::formatApiMediaResult(const QVariantMap &item) const {
    const QString ratingKey = item.value(QStringLiteral("ratingKey")).toString();
    const QString kind = item.value(QStringLiteral("type")).toString();
    QVariantMap result;
    result[QStringLiteral("id")] = QStringLiteral("vod:%1:%2").arg(
        kind,
        QString::fromLatin1(QUrl::toPercentEncoding(ratingKey)));
    result[QStringLiteral("module")] = QStringLiteral("video_on_demand");
    result[QStringLiteral("provider")] = mediaProvider().toLower();
    result[QStringLiteral("kind")] = kind;
    result[QStringLiteral("title")] = item.value(QStringLiteral("title")).toString();
    result[QStringLiteral("rating_key")] = ratingKey;
    if (item.contains(QStringLiteral("year")))
        result[QStringLiteral("year")] = item.value(QStringLiteral("year"));
    if (item.contains(QStringLiteral("durationDisplay")))
        result[QStringLiteral("duration")] = item.value(QStringLiteral("durationDisplay"));
    if (item.contains(QStringLiteral("grandparentTitle")) &&
        !item.value(QStringLiteral("grandparentTitle")).toString().isEmpty())
        result[QStringLiteral("series")] = item.value(QStringLiteral("grandparentTitle"));
    if (item.contains(QStringLiteral("parentTitle")) &&
        !item.value(QStringLiteral("parentTitle")).toString().isEmpty())
        result[QStringLiteral("season")] = item.value(QStringLiteral("parentTitle"));
    return result;
}

QVariantList EmbyJellyfinBackend::apiFilterMediaResults(const QVariantList &items,
                                                        const QStringList &types,
                                                        int limit) const {
    QVariantList result;
    const int maxResults = std::max(1, std::min(limit <= 0 ? 10 : limit, 50));
    for (const QVariant &value : items) {
        const QVariantMap item = value.toMap();
        const QString kind = item.value(QStringLiteral("type")).toString();
        const QString ratingKey = item.value(QStringLiteral("ratingKey")).toString();
        if (ratingKey.isEmpty() || !apiTypeAllowed(kind, types))
            continue;
        result.append(formatApiMediaResult(item));
        if (result.size() >= maxResults)
            break;
    }
    return result;
}

QVariantMap EmbyJellyfinBackend::formatPlexItem(const QXmlStreamAttributes &attrs) const {
    const QString rawType = plexAttr(attrs, "type");
    QString type = rawType;
    if (rawType == "movie") type = "movie";
    else if (rawType == "show") type = "show";
    else if (rawType == "season") type = "season";
    else if (rawType == "episode") type = "episode";
    else if (rawType == "album") type = "album";
    else if (rawType == "track") type = "track";

    const int duration = plexIntAttr(attrs, "duration");
    const int viewOffset = plexIntAttr(attrs, "viewOffset");
    const int leafCount = plexIntAttr(attrs, "leafCount", plexIntAttr(attrs, "childCount"));
    const int viewedLeafCount = plexIntAttr(attrs, "viewedLeafCount");

    return QVariantMap{
        {"ratingKey", plexAttr(attrs, "ratingKey")},
        {"key", plexAttr(attrs, "key")},
        {"title", plexAttr(attrs, "title").toUpper()},
        {"type", type},
        {"year", plexIntAttr(attrs, "year")},
        {"duration", duration},
        {"durationDisplay", msToDisplay(duration)},
        {"summary", plexAttr(attrs, "summary")},
        {"viewOffset", viewOffset},
        {"viewCount", plexIntAttr(attrs, "viewCount")},
        {"leafCount", leafCount},
        {"viewedLeafCount", viewedLeafCount},
        {"index", plexIntAttr(attrs, "index")},
        {"parentIndex", plexIntAttr(attrs, "parentIndex")},
        {"parentRatingKey", plexAttr(attrs, "parentRatingKey")},
        {"grandparentRatingKey", plexAttr(attrs, "grandparentRatingKey")},
        {"grandparentTitle", plexAttr(attrs, "grandparentTitle").toUpper()},
        {"parentTitle", plexAttr(attrs, "parentTitle").toUpper()},
        {"originallyAvailableAt", plexAttr(attrs, "originallyAvailableAt")}
    };
}

QVariantMap EmbyJellyfinBackend::formatPlexMusicAlbum(const QXmlStreamAttributes &attrs) const {
    return QVariantMap{
        {"ratingKey", plexAttr(attrs, "ratingKey")},
        {"key", plexAttr(attrs, "ratingKey")},
        {"title", plexAttr(attrs, "title").toUpper()},
        {"artist", plexAttr(attrs, "parentTitle").toUpper()},
        {"type", "album"},
        {"year", plexIntAttr(attrs, "year")},
        {"duration", plexIntAttr(attrs, "duration")},
        {"durationDisplay", msToDisplay(plexIntAttr(attrs, "duration"))},
        {"leafCount", plexIntAttr(attrs, "leafCount", plexIntAttr(attrs, "childCount"))}
    };
}

QVariantList EmbyJellyfinBackend::parsePlexLibraries(const QByteArray &body,
                                                     bool musicOnly) const {
    QVariantList result;
    QXmlStreamReader xml(body);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() != QStringLiteral("Directory"))
            continue;

        const QXmlStreamAttributes attrs = xml.attributes();
        const QString key = plexAttr(attrs, "key");
        const QString type = plexAttr(attrs, "type");
        if (key.isEmpty())
            continue;

        if (musicOnly) {
            if (type != "artist")
                continue;
        } else if (type != "movie" && type != "show" && type != "other") {
            continue;
        }

        result.append(QVariantMap{
            {"key", key},
            {"title", plexAttr(attrs, "title").toUpper()},
            {"sectionId", key},
            {"sectionType", type == "artist" ? QStringLiteral("music") : type}
        });
    }
    return result;
}

QVariantList EmbyJellyfinBackend::parsePlexItems(const QByteArray &body) const {
    QVariantList result;
    QXmlStreamReader xml(body);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement())
            continue;

        const auto name = xml.name();
        if (name == QStringLiteral("Video") || name == QStringLiteral("Directory")) {
            QVariantMap item = formatPlexItem(xml.attributes());
            const QStringList genres = plexGenresFromElement(&xml);
            if (!genres.isEmpty())
                item[QStringLiteral("genres")] = genres;
            if (!item["ratingKey"].toString().isEmpty())
                result.append(item);
        }
    }
    return result;
}

QVariantMap EmbyJellyfinBackend::parsePlexDetail(const QByteArray &body) const {
    QVariantMap detail;
    QVariantList audioStreams;
    QVariantList subtitleStreams{
        QVariantMap{{"id","0"}, {"displayTitle","OFF"}, {"language",""}, {"subUrl",""}}
    };

    QXmlStreamReader xml(body);
    bool inTargetMedia = false;
    bool havePart = false;
    QString selectedAudioId;
    QString selectedSubId = "0";

    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement())
            continue;

        const auto name = xml.name();
        const QXmlStreamAttributes attrs = xml.attributes();
        if ((name == QStringLiteral("Video") || name == QStringLiteral("Track")) &&
            detail.isEmpty()) {
            detail = formatPlexItem(attrs);
            detail["partKey"] = QString{};
            detail["partId"] = QString{};
            detail["forceTranscode"] = true;
            detail["audioStreams"] = QVariantList{};
            detail["subtitleStreams"] = subtitleStreams;
            inTargetMedia = true;
        } else if (name == QStringLiteral("Media") && inTargetMedia) {
            detail["mediaSourceId"] = plexAttr(attrs, "id");
        } else if (name == QStringLiteral("Part") && inTargetMedia && !havePart) {
            const QString partId = plexAttr(attrs, "id");
            const QString partKey = plexAttr(attrs, "key");
            detail["partId"] = partId;
            detail["partKey"] = partKey;
            havePart = true;
        } else if (name == QStringLiteral("Stream") && inTargetMedia && havePart) {
            const QString streamType = plexAttr(attrs, "streamType");
            const QString id = plexAttr(attrs, "id");
            const QString language = plexAttr(attrs, "languageCode").isEmpty()
                ? plexAttr(attrs, "language")
                : plexAttr(attrs, "languageCode");
            QString display = plexAttr(attrs, "displayTitle");
            if (display.isEmpty())
                display = plexAttr(attrs, "codec").toUpper();
            if (display.isEmpty())
                display = streamType == "2" ? QStringLiteral("AUDIO") : QStringLiteral("SUBTITLE");

            if (streamType == "2") {
                if (selectedAudioId.isEmpty() || plexAttr(attrs, "selected") == "1")
                    selectedAudioId = id;
                audioStreams.append(QVariantMap{
                    {"id", id},
                    {"displayTitle", display.toUpper()},
                    {"language", language},
                    {"codec", plexAttr(attrs, "codec")}
                });
            } else if (streamType == "3") {
                QString subUrl;
                const QString key = plexAttr(attrs, "key");
                if (!key.isEmpty())
                    subUrl = plexAbsoluteUrl(key);
                if (plexAttr(attrs, "selected") == "1")
                    selectedSubId = id;
                subtitleStreams.append(QVariantMap{
                    {"id", id},
                    {"displayTitle", display.toUpper()},
                    {"language", language},
                    {"codec", plexAttr(attrs, "codec")},
                    {"subUrl", subUrl},
                    {"imageSubtitle", false}
                });
            }
        }
    }

    if (!detail.isEmpty()) {
        detail["audioStreams"] = audioStreams;
        detail["subtitleStreams"] = subtitleStreams;
        detail["selectedAudioId"] = selectedAudioId;
        detail["selectedSubtitleId"] = selectedSubId;
    }
    return detail;
}

void EmbyJellyfinBackend::plexLoadLibraries() {
    auto *reply = plexServerGet(plexApiUrl("/library/sections"));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD PLEX LIBRARIES FAILED: " + reply->errorString());
            return;
        }

        QVariantList items = parsePlexLibraries(reply->readAll(), false);
        const QJsonObject enabled = loadConfig()["modules"].toObject()
                                    [kModuleId].toObject()["libraries"].toObject();
        if (!enabled.isEmpty()) {
            QVariantList filtered;
            for (const auto &v : items) {
                const QVariantMap item = v.toMap();
                const QString id = item["sectionId"].toString();
                if (enabled[id].toBool(true))
                    filtered.append(item);
            }
            items = filtered;
        }
        emit librariesLoaded(items);
    });
}

void EmbyJellyfinBackend::plexLoadMusicLibraries() {
    auto *reply = plexServerGet(plexApiUrl("/library/sections"));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD PLEX MUSIC LIBRARIES FAILED: " + reply->errorString());
            return;
        }
        emit musicLibrariesLoaded(parsePlexLibraries(reply->readAll(), true));
    });
}

void EmbyJellyfinBackend::plexLoadMusicAlbums(const QString &sectionId) {
    auto *reply = plexServerGet(plexApiUrl(QStringLiteral("/library/sections/%1/albums").arg(sectionId)));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD PLEX ALBUMS FAILED: " + reply->errorString());
            return;
        }

        QVariantList albums;
        QXmlStreamReader xml(reply->readAll());
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == QStringLiteral("Directory")) {
                const QVariantMap album = formatPlexMusicAlbum(xml.attributes());
                if (!album["ratingKey"].toString().isEmpty())
                    albums.append(album);
            }
        }
        emit musicAlbumsLoaded(albums);
    });
}

void EmbyJellyfinBackend::plexLoadMusicTracks(const QString &albumId) {
    auto *reply = plexServerGet(plexApiUrl(QStringLiteral("/library/metadata/%1/children").arg(albumId)));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD PLEX TRACKS FAILED: " + reply->errorString());
            return;
        }

        QVariantList tracks;
        QXmlStreamReader xml(reply->readAll());
        QXmlStreamAttributes currentTrack;
        bool inTrack = false;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == QStringLiteral("Track")) {
                currentTrack = xml.attributes();
                inTrack = true;
            } else if (inTrack && xml.isStartElement() && xml.name() == QStringLiteral("Part")) {
                const QString ratingKey = plexAttr(currentTrack, "ratingKey");
                if (!ratingKey.isEmpty()) {
                    const int duration = plexIntAttr(currentTrack, "duration");
                    tracks.append(QVariantMap{
                        {"ratingKey", ratingKey},
                        {"partKey", xml.attributes().value("key").toString()},
                        {"title", plexAttr(currentTrack, "title").toUpper()},
                        {"artist", plexAttr(currentTrack, "grandparentTitle").toUpper()},
                        {"album", plexAttr(currentTrack, "parentTitle").toUpper()},
                        {"type", "track"},
                        {"index", plexIntAttr(currentTrack, "index")},
                        {"parentIndex", plexIntAttr(currentTrack, "parentIndex")},
                        {"duration", duration},
                        {"durationDisplay", msToDisplay(duration)}
                    });
                }
                inTrack = false;
            } else if (xml.isEndElement() && xml.name() == QStringLiteral("Track")) {
                inTrack = false;
            }
        }
        emit musicTracksLoaded(tracks);
    });
}

void EmbyJellyfinBackend::plexLoadLibraryAll(const QString &sectionId) {
    auto *reply = plexServerGet(plexApiUrl(QStringLiteral("/library/sections/%1/all").arg(sectionId)));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD PLEX ITEMS FAILED: " + reply->errorString());
            return;
        }
        emit itemsLoaded(parsePlexItems(reply->readAll()));
    });
}

void EmbyJellyfinBackend::plexLoadChildren(const QString &ratingKey) {
    auto *reply = plexServerGet(plexApiUrl(QStringLiteral("/library/metadata/%1/children").arg(ratingKey)));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD PLEX CHILDREN FAILED: " + reply->errorString());
            return;
        }
        emit childrenLoaded(parsePlexItems(reply->readAll()));
    });
}

void EmbyJellyfinBackend::plexLoadItemDetail(const QString &ratingKey) {
    auto *reply = plexServerGet(plexApiUrl(QStringLiteral("/library/metadata/%1").arg(ratingKey)));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD PLEX ITEM FAILED: " + reply->errorString());
            return;
        }
        const QVariantMap detail = parsePlexDetail(reply->readAll());
        if (detail.isEmpty()) {
            emit errorOccurred("LOAD PLEX ITEM FAILED: EMPTY DETAIL");
            return;
        }
        emit itemLoaded(detail);
    });
}

void EmbyJellyfinBackend::plexBuildStreamUrl(const QString &ratingKey,
                                             const QString &partKey) {
    if (partKey.isEmpty()) {
        emit errorOccurred("PLEX STREAM FAILED: EMPTY PART KEY");
        return;
    }
    plexRequestTranscodeUrl(ratingKey, partKey, QUuid::createUuid().toString(QUuid::WithoutBraces),
                            {}, {}, 0,
                            [this](const QString &url, const QString &httpHeaderFields) {
        emit streamUrlReady(url, httpHeaderFields);
    }, [this](const QString &message) {
        emit errorOccurred(message);
    });
}

void EmbyJellyfinBackend::plexBuildAudioStreamUrl(const QString &ratingKey,
                                                  const QString &partKey) {
    if (partKey.isEmpty()) {
        emit errorOccurred("PLEX AUDIO STREAM FAILED: EMPTY PART KEY");
        return;
    }
    emit audioStreamUrlReady(ratingKey, plexAbsoluteUrl(partKey), {});
}

QString EmbyJellyfinBackend::plexTranscodeQuality() const {
    const QString quality = videoQuality();
    if (isSupportedVideoQuality(quality))
        return quality;
    return displayAdaptiveVideoQuality();
}

QString EmbyJellyfinBackend::plexHttpHeaderFields() const {
    const QString token = plexToken();
    return token.isEmpty() ? QString{} : QStringLiteral("X-Plex-Token: %1").arg(token);
}

void EmbyJellyfinBackend::plexRequestTranscodeUrl(
        const QString &ratingKey,
        const QString &partKey,
        const QString &sessionId,
        const QString &audioId,
        const QString &subtitleId,
        int offsetMs,
        std::function<void(const QString &url, const QString &httpHeaderFields)> onReady,
        std::function<void(const QString &message)> onError) {
    Q_UNUSED(partKey);
    if (ratingKey.isEmpty()) {
        onError(QStringLiteral("PLEX TRANSCODE FAILED: EMPTY ITEM ID"));
        return;
    }

    const QString effectiveSessionId = sessionId.isEmpty()
        ? QUuid::createUuid().toString(QUuid::WithoutBraces)
        : sessionId;
    const PlaybackLimits limits = playbackLimitsFor(plexTranscodeQuality(), true);

    QUrl url = plexApiUrl(QStringLiteral("/video/:/transcode/universal/start.m3u8"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("hasMDE"), QStringLiteral("1"));
    q.addQueryItem(QStringLiteral("path"), QStringLiteral("/library/metadata/") + ratingKey);
    q.addQueryItem(QStringLiteral("mediaIndex"), QStringLiteral("0"));
    q.addQueryItem(QStringLiteral("partIndex"), QStringLiteral("0"));
    q.addQueryItem(QStringLiteral("protocol"), QStringLiteral("hls"));
    q.addQueryItem(QStringLiteral("fastSeek"), QStringLiteral("1"));
    q.addQueryItem(QStringLiteral("copyts"), QStringLiteral("1"));
    q.addQueryItem(QStringLiteral("directPlay"), QStringLiteral("0"));
    q.addQueryItem(QStringLiteral("directStream"), QStringLiteral("0"));
    q.addQueryItem(QStringLiteral("maxVideoBitrate"),
                   QString::number(qMax(1, limits.videoBitRate / 1000)));
    q.addQueryItem(QStringLiteral("subtitleSize"), QStringLiteral("100"));
    q.addQueryItem(QStringLiteral("audioBoost"), QStringLiteral("100"));
    q.addQueryItem(QStringLiteral("session"), effectiveSessionId);
    q.addQueryItem(QStringLiteral("X-Plex-Platform"), QStringLiteral("Chrome"));
    q.addQueryItem(QStringLiteral("X-Plex-Client-Identifier"), plexClientId());
    if (offsetMs > 0)
        q.addQueryItem(QStringLiteral("offset"), QString::number(offsetMs / 1000));
    if (!audioId.isEmpty())
        q.addQueryItem(QStringLiteral("audioStreamID"), audioId);
    if (!subtitleId.isEmpty() && subtitleId != QStringLiteral("0") &&
        subtitleId != QStringLiteral("-1")) {
        q.addQueryItem(QStringLiteral("subtitleStreamID"), subtitleId);
        q.addQueryItem(QStringLiteral("subtitles"), QStringLiteral("burn"));
    }
    url.setQuery(q);

    QNetworkRequest req = plexServerRequest(plexUrlWithToken(url));
    req.setRawHeader("Accept", "application/x-mpegURL, application/vnd.apple.mpegurl, */*");
    req.setRawHeader("X-Plex-Platform", "Chrome");

    qInfo("[EmbyJellyfinBackend] Plex transcode requested at %s",
          qPrintable(plexTranscodeQuality()));
    auto *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, onReady, onError]() {
        reply->deleteLater();
        const QByteArray bytes = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            QString message = QStringLiteral("PLEX TRANSCODE FAILED: ") + reply->errorString();
            const QString body = abbreviatedNetworkBody(bytes);
            if (!body.isEmpty())
                message += QStringLiteral(" - ") + body;
            onError(message);
            return;
        }

        QString variantPath;
        const QStringList lines = QString::fromUtf8(bytes).split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            const QString trimmed = line.trimmed();
            if (!trimmed.isEmpty() && !trimmed.startsWith(QLatin1Char('#'))) {
                variantPath = trimmed;
                break;
            }
        }

        QString streamUrl;
        if (!variantPath.isEmpty()) {
            QUrl base = reply->url();
            base.setQuery(QString{});
            streamUrl = base.resolved(QUrl(variantPath)).toString();
        } else {
            streamUrl = reply->url().toString();
        }

        if (streamUrl.isEmpty()) {
            onError(QStringLiteral("PLEX TRANSCODE FAILED: EMPTY STREAM URL"));
            return;
        }

        onReady(streamUrl, plexHttpHeaderFields());
    });
}

void EmbyJellyfinBackend::plexLoadNextEpisode(const QString &currentRatingKey) {
    auto *detailReply = plexServerGet(plexApiUrl(QStringLiteral("/library/metadata/%1").arg(currentRatingKey)));
    connect(detailReply, &QNetworkReply::finished, this, [this, detailReply]() {
        detailReply->deleteLater();
        if (detailReply->error() != QNetworkReply::NoError) {
            emit nextEpisodeReady(QVariantMap{});
            return;
        }
        const QVariantMap current = parsePlexDetail(detailReply->readAll());
        const QString seasonKey = current["parentRatingKey"].toString();
        const int currentIndex = current["index"].toInt();
        if (seasonKey.isEmpty()) {
            emit nextEpisodeReady(QVariantMap{});
            return;
        }

        auto *seasonReply = plexServerGet(plexApiUrl(QStringLiteral("/library/metadata/%1/children").arg(seasonKey)));
        connect(seasonReply, &QNetworkReply::finished, this, [this, seasonReply, currentIndex]() {
            seasonReply->deleteLater();
            if (seasonReply->error() != QNetworkReply::NoError) {
                emit nextEpisodeReady(QVariantMap{});
                return;
            }
            const QVariantList episodes = parsePlexItems(seasonReply->readAll());
            for (const auto &v : episodes) {
                const QVariantMap ep = v.toMap();
                if (ep["index"].toInt() > currentIndex) {
                    auto *nextReply = plexServerGet(plexApiUrl(QStringLiteral("/library/metadata/%1")
                                                               .arg(ep["ratingKey"].toString())));
                    connect(nextReply, &QNetworkReply::finished, this, [this, nextReply]() {
                        nextReply->deleteLater();
                        if (nextReply->error() != QNetworkReply::NoError) {
                            emit nextEpisodeReady(QVariantMap{});
                            return;
                        }
                        emit nextEpisodeReady(parsePlexDetail(nextReply->readAll()));
                    });
                    return;
                }
            }
            emit nextEpisodeReady(QVariantMap{});
        });
    });
}

void EmbyJellyfinBackend::load_music_libraries() {
    if (mediaProvider() == kProviderPlex) {
        if (get_auth_state() != "authed") {
            emit errorOccurred("NOT SIGNED IN");
            return;
        }
        plexLoadMusicLibraries();
        return;
    }

    if (get_auth_state() != "authed") {
        emit errorOccurred("NOT SIGNED IN");
        return;
    }

    auto *reply = apiGet(apiUrl("/Users/" + userId() + "/Views"));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD MUSIC LIBRARIES FAILED: " + reply->errorString());
            return;
        }

        QVariantList libraries;
        const QJsonArray views = QJsonDocument::fromJson(reply->readAll())
                                 .object()["Items"].toArray();
        for (const auto &v : views) {
            const QJsonObject view = v.toObject();
            const QString id = view["Id"].toString();
            const QString collectionType = view["CollectionType"].toString();
            if (id.isEmpty() || collectionType != "music")
                continue;

            libraries.append(QVariantMap{
                {"key", id},
                {"title", view["Name"].toString().toUpper()},
                {"sectionId", id},
                {"sectionType", collectionType}
            });
        }

        emit musicLibrariesLoaded(libraries);
    });
}

void EmbyJellyfinBackend::load_music_albums(const QString &sectionId) {
    if (mediaProvider() == kProviderPlex) {
        if (get_auth_state() != "authed") {
            emit errorOccurred("NOT SIGNED IN");
            return;
        }
        plexLoadMusicAlbums(sectionId);
        return;
    }

    if (get_auth_state() != "authed") {
        emit errorOccurred("NOT SIGNED IN");
        return;
    }

    auto *reply = apiGet(itemListUrl(sectionId, "MusicAlbum", true));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD MUSIC ALBUMS FAILED: " + reply->errorString());
            return;
        }

        QVariantList albums;
        const QJsonArray items = QJsonDocument::fromJson(reply->readAll())
                                 .object()["Items"].toArray();
        for (const auto &v : items) {
            const QJsonObject item = v.toObject();
            if (!item["Id"].toString().isEmpty())
                albums.append(formatMusicAlbum(item));
        }

        emit musicAlbumsLoaded(albums);
    });
}

void EmbyJellyfinBackend::load_music_tracks(const QString &sectionId) {
    if (mediaProvider() == kProviderPlex) {
        if (get_auth_state() != "authed") {
            emit errorOccurred("NOT SIGNED IN");
            return;
        }
        plexLoadMusicTracks(sectionId);
        return;
    }

    if (get_auth_state() != "authed") {
        emit errorOccurred("NOT SIGNED IN");
        return;
    }

    QUrl url = itemListUrl(sectionId, "Audio", true);
    QUrlQuery q(url);
    q.removeAllQueryItems("SortBy");
    q.addQueryItem("SortBy", "ParentIndexNumber,IndexNumber,SortName");
    url.setQuery(q);

    auto *reply = apiGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD MUSIC TRACKS FAILED: " + reply->errorString());
            return;
        }

        QVariantList tracks;
        const QJsonArray items = QJsonDocument::fromJson(reply->readAll())
                                 .object()["Items"].toArray();
        for (const auto &v : items) {
            const QJsonObject item = v.toObject();
            if (!item["Id"].toString().isEmpty())
                tracks.append(formatMusicTrack(item));
        }

        emit musicTracksLoaded(tracks);
    });
}

void EmbyJellyfinBackend::load_live_tv_channels() {
    if (get_auth_state() != "authed") {
        emit errorOccurred("NOT SIGNED IN");
        return;
    }

    QUrl url = apiUrl("/LiveTv/Channels");
    QUrlQuery q;
    q.addQueryItem("UserId", userId());
    q.addQueryItem("EnableUserData", "false");
    q.addQueryItem("EnableImages", "false");
    q.addQueryItem("SortBy", "SortName");
    q.addQueryItem("SortOrder", "Ascending");
    url.setQuery(q);

    auto *reply = apiGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QByteArray bytes = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            QString message = "LOAD OTA CHANNELS FAILED: " + reply->errorString();
            const QString body = abbreviatedNetworkBody(bytes);
            if (!body.isEmpty())
                message += " - " + body;
            emit errorOccurred(message);
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            emit errorOccurred("LOAD OTA CHANNELS FAILED: INVALID SERVER RESPONSE");
            return;
        }

        const QJsonArray items = doc.object()["Items"].toArray();
        QVariantList channels;
        for (const auto &value : items) {
            const QJsonObject item = value.toObject();
            const QString id = item["Id"].toString();
            if (id.isEmpty())
                continue;

            const QString number = item["Number"].toString();
            const QString name = item["Name"].toString();
            QString display = number;
            if (!name.isEmpty()) {
                display = display.isEmpty()
                    ? name
                    : QStringLiteral("%1  %2").arg(display, name);
            }

            channels.append(QVariantMap{
                {"id", id},
                {"number", number},
                {"name", name.toUpper()},
                {"title", display.toUpper()},
                {"isHd", item["IsHD"].toBool(false)}
            });
        }

        QCollator collator;
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        std::sort(channels.begin(), channels.end(),
                  [&collator](const QVariant &left, const QVariant &right) {
            const QVariantMap a = left.toMap();
            const QVariantMap b = right.toMap();
            const QString aKey = a["number"].toString().isEmpty()
                ? a["title"].toString()
                : a["number"].toString();
            const QString bKey = b["number"].toString().isEmpty()
                ? b["title"].toString()
                : b["number"].toString();
            return collator.compare(aKey, bKey) < 0;
        });

        emit liveTvChannelsLoaded(channels);
    });
}

void EmbyJellyfinBackend::load_libraries() {
    if (mediaProvider() == kProviderPlex) {
        if (get_auth_state() != "authed") {
            emit errorOccurred("NOT SIGNED IN");
            return;
        }
        plexLoadLibraries();
        return;
    }

    if (get_auth_state() != "authed") {
        emit errorOccurred("NOT SIGNED IN");
        return;
    }

    QUrl resumeUrl = apiUrl("/Users/" + userId() + "/Items/Resume");
    QUrlQuery rq;
    rq.addQueryItem("MediaTypes", "Video");
    rq.addQueryItem("Limit", "1");
    rq.addQueryItem("Fields", "UserData");
    resumeUrl.setQuery(rq);

    auto *resumeReply = apiGet(resumeUrl);
    connect(resumeReply, &QNetworkReply::finished, this, [this, resumeReply]() {
        resumeReply->deleteLater();
        bool hasContinueWatching = false;
        if (resumeReply->error() == QNetworkReply::NoError) {
            QJsonObject data = QJsonDocument::fromJson(resumeReply->readAll()).object();
            hasContinueWatching = !data["Items"].toArray().isEmpty();
        }

        auto *viewsReply = apiGet(apiUrl("/Users/" + userId() + "/Views"));
        connect(viewsReply, &QNetworkReply::finished, this, [this, viewsReply, hasContinueWatching]() {
            viewsReply->deleteLater();
            if (viewsReply->error() != QNetworkReply::NoError) {
                emit errorOccurred("LOAD LIBRARIES FAILED: " + viewsReply->errorString());
                return;
            }

            QJsonArray views = QJsonDocument::fromJson(viewsReply->readAll())
                               .object()["Items"].toArray();
            QJsonObject enabled = loadConfig()["modules"].toObject()
                                  [kModuleId].toObject()["libraries"].toObject();

            QVariantList items;
            if (hasContinueWatching) {
                items.append(QVariantMap{{"key","continue_watching"},
                                         {"title","CONTINUE WATCHING"},
                                         {"sectionId",QVariant()},
                                         {"sectionType",QVariant()}});
            }

            static const QSet<QString> kVideoCollections = {
                "movies", "tvshows", "mixed", "homevideos", "boxsets"
            };
            for (const auto &v : views) {
                QJsonObject view = v.toObject();
                QString id = view["Id"].toString();
                QString collectionType = view["CollectionType"].toString();
                QString type = view["Type"].toString();
                if (id.isEmpty()) continue;
                if (!collectionType.isEmpty() && !kVideoCollections.contains(collectionType))
                    continue;
                if (collectionType.isEmpty() && type != "CollectionFolder")
                    continue;
                if (!enabled.isEmpty() && !enabled[id].toBool(true)) continue;

                items.append(QVariantMap{
                    {"key", id},
                    {"title", view["Name"].toString().toUpper()},
                    {"sectionId", id},
                    {"sectionType", collectionType.isEmpty() ? QStringLiteral("mixed") : collectionType},
                });
            }
            emit librariesLoaded(items);
        });
    });
}

void EmbyJellyfinBackend::load_continue_watching() {
    if (mediaProvider() == kProviderPlex) {
        emit continueWatchingLoaded(QVariantList{});
        return;
    }

    QUrl url = apiUrl("/Users/" + userId() + "/Items/Resume");
    QUrlQuery q;
    q.addQueryItem("MediaTypes", "Video");
    q.addQueryItem("Limit", "50");
    q.addQueryItem("Fields", "MediaSources,MediaStreams,Overview,ParentId,UserData");
    url.setQuery(q);

    auto *reply = apiGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("CONTINUE WATCHING FAILED: " + reply->errorString());
            return;
        }
        emit continueWatchingLoaded(formatItems(QJsonDocument::fromJson(reply->readAll())
                                                .object()["Items"].toArray()));
    });
}

void EmbyJellyfinBackend::check_section_capabilities(const QString &sectionId) {
    Q_UNUSED(sectionId)
    if (mediaProvider() == kProviderPlex) {
        emit capabilitiesLoaded(QVariantMap{
            {"recommended", false},
            {"collections", false},
            {"playlists", false},
            {"categories", false}
        });
        return;
    }

    emit capabilitiesLoaded(QVariantMap{
        {"recommended", false},
        {"collections", true},
        {"playlists", true},
        {"categories", false}
    });
}

void EmbyJellyfinBackend::load_section_hubs(const QString &sectionId) {
    Q_UNUSED(sectionId)
    emit hubsLoaded(QVariantList{});
}

void EmbyJellyfinBackend::load_items_for_hub(const QString &hubKey) {
    Q_UNUSED(hubKey)
    emit itemsLoaded(QVariantList{});
}

void EmbyJellyfinBackend::load_library_all(const QString &sectionId) {
    if (mediaProvider() == kProviderPlex) {
        plexLoadLibraryAll(sectionId);
        return;
    }

    auto *reply = apiGet(itemListUrl(sectionId, "Movie,Series,Video", true));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD LIBRARY FAILED: " + reply->errorString());
            return;
        }
        emit itemsLoaded(formatItems(QJsonDocument::fromJson(reply->readAll())
                                     .object()["Items"].toArray()));
    });
}

void EmbyJellyfinBackend::api_search_media(const QString &requestId,
                                           const QString &query,
                                           const QStringList &types,
                                           int limit) {
    const QString needle = query.trimmed();
    if (needle.isEmpty()) {
        emit apiSearchResultsReady(requestId, QVariantList{});
        return;
    }
    if (get_auth_state() != QStringLiteral("authed")) {
        emit apiSearchResultsReady(requestId, QVariantList{});
        return;
    }

    const QStringList wanted = normalizedApiTypes(types);
    const int maxResults = std::max(1, std::min(limit <= 0 ? 10 : limit, 50));

    if (mediaProvider() == kProviderPlex) {
        QUrl url = plexApiUrl(QStringLiteral("/search"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("query"), needle);
        url.setQuery(q);

        auto *reply = plexServerGet(url);
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, requestId, wanted, maxResults]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                emit apiRequestFailed(requestId, QStringLiteral("PLEX SEARCH FAILED: ") + reply->errorString());
                return;
            }
            emit apiSearchResultsReady(requestId,
                                       apiFilterMediaResults(parsePlexItems(reply->readAll()),
                                                             wanted, maxResults));
        });
        return;
    }

    QUrl url = apiUrl(QStringLiteral("/Users/") + userId() + QStringLiteral("/Items"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("SearchTerm"), needle);
    q.addQueryItem(QStringLiteral("Recursive"), QStringLiteral("true"));
    q.addQueryItem(QStringLiteral("IncludeItemTypes"), embyIncludeTypesForApi(wanted));
    q.addQueryItem(QStringLiteral("Limit"), QString::number(maxResults));
    q.addQueryItem(QStringLiteral("Fields"),
                   QStringLiteral("MediaSources,MediaStreams,Overview,Genres,ParentId,PrimaryImageAspectRatio,UserData,RecursiveItemCount,ChildCount,Album,AlbumArtist,AlbumArtists,ArtistItems,Artists"));
    q.addQueryItem(QStringLiteral("ImageTypeLimit"), QStringLiteral("1"));
    q.addQueryItem(QStringLiteral("EnableImages"), QStringLiteral("false"));
    url.setQuery(q);

    auto *reply = apiGet(url);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, requestId, wanted, maxResults]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit apiRequestFailed(requestId, QStringLiteral("MEDIA SEARCH FAILED: ") + reply->errorString());
            return;
        }
        const QVariantList items = formatItems(QJsonDocument::fromJson(reply->readAll())
                                               .object()[QStringLiteral("Items")].toArray());
        emit apiSearchResultsReady(requestId,
                                   apiFilterMediaResults(items, wanted, maxResults));
    });
}

void EmbyJellyfinBackend::load_collections(const QString &sectionId) {
    if (mediaProvider() == kProviderPlex) {
        Q_UNUSED(sectionId)
        emit collectionsLoaded(QVariantList{});
        return;
    }

    Q_UNUSED(sectionId)
    auto *reply = apiGet(itemListUrl({}, "BoxSet", true));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD COLLECTIONS FAILED: " + reply->errorString());
            return;
        }

        QVariantList items;
        QJsonArray raw = QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray();
        for (const auto &v : raw) {
            QJsonObject item = v.toObject();
            items.append(QVariantMap{{"ratingKey", item["Id"].toString()},
                                     {"title", item["Name"].toString().toUpper()},
                                     {"type", "collection"}});
        }
        emit collectionsLoaded(items);
    });
}

void EmbyJellyfinBackend::load_collection_items(const QString &ratingKey) {
    if (mediaProvider() == kProviderPlex) {
        Q_UNUSED(ratingKey)
        emit itemsLoaded(QVariantList{});
        return;
    }
    load_library_all(ratingKey);
}

void EmbyJellyfinBackend::load_playlists(const QString &sectionId) {
    if (mediaProvider() == kProviderPlex) {
        Q_UNUSED(sectionId)
        emit playlistsLoaded(QVariantList{});
        return;
    }

    Q_UNUSED(sectionId)
    auto *reply = apiGet(itemListUrl({}, "Playlist", true));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD PLAYLISTS FAILED: " + reply->errorString());
            return;
        }

        QVariantList items;
        QJsonArray raw = QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray();
        for (const auto &v : raw) {
            QJsonObject item = v.toObject();
            items.append(QVariantMap{{"ratingKey", item["Id"].toString()},
                                     {"title", item["Name"].toString().toUpper()},
                                     {"type", "playlist"}});
        }
        emit playlistsLoaded(items);
    });
}

void EmbyJellyfinBackend::load_playlist_items(const QString &ratingKey) {
    if (mediaProvider() == kProviderPlex) {
        Q_UNUSED(ratingKey)
        emit itemsLoaded(QVariantList{});
        return;
    }
    load_library_all(ratingKey);
}

void EmbyJellyfinBackend::load_categories(const QString &sectionId) {
    Q_UNUSED(sectionId)
    emit categoriesLoaded(QVariantList{});
}

void EmbyJellyfinBackend::load_category_items(const QString &sectionId,
                                              const QString &filterKey) {
    Q_UNUSED(sectionId)
    Q_UNUSED(filterKey)
    emit itemsLoaded(QVariantList{});
}

void EmbyJellyfinBackend::load_children(const QString &ratingKey) {
    if (mediaProvider() == kProviderPlex) {
        plexLoadChildren(ratingKey);
        return;
    }

    auto *reply = apiGet(itemListUrl(ratingKey, {}, false));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD CHILDREN FAILED: " + reply->errorString());
            return;
        }
        emit childrenLoaded(formatItems(QJsonDocument::fromJson(reply->readAll())
                                        .object()["Items"].toArray()));
    });
}

QString EmbyJellyfinBackend::subtitleUrlFor(const QString &itemId,
                                            const QString &mediaSourceId,
                                            int streamIndex,
                                            const QString &codec) const {
    QString ext = codec.toLower();
    if (ext == "subrip") ext = "srt";
    if (ext == "ass" || ext == "ssa") ext = "ass";
    if (ext.isEmpty()) ext = "srt";

    QUrl url = apiUrl(QString("/Videos/%1/%2/Subtitles/%3/Stream.%4")
                      .arg(itemId, mediaSourceId).arg(streamIndex).arg(ext));
    QUrlQuery q;
    q.addQueryItem("api_key", accessToken());
    url.setQuery(q);
    return url.toString();
}

QString EmbyJellyfinBackend::absoluteMediaUrl(const QString &pathOrUrl) const {
    const QString raw = pathOrUrl.trimmed();
    if (raw.isEmpty()) return {};

    if (raw.startsWith("//")) {
        const QUrl base(serverUrl());
        return base.scheme() + ":" + raw;
    }

    QUrl url(raw);
    if (url.isRelative()) {
        QUrl base(serverUrl() + "/");
        url = base.resolved(url);
    }
    return url.toString();
}

QString EmbyJellyfinBackend::withAccessToken(const QString &rawUrl) const {
    if (rawUrl.isEmpty() || accessToken().isEmpty()) return rawUrl;

    QUrl url(rawUrl);
    QUrlQuery q(url);
    if (!q.hasQueryItem("api_key") &&
        !q.hasQueryItem("ApiKey") &&
        !q.hasQueryItem("X-Emby-Token") &&
        !q.hasQueryItem("X-MediaBrowser-Token")) {
        q.addQueryItem("api_key", accessToken());
        url.setQuery(q);
    }
    return url.toString();
}

QString EmbyJellyfinBackend::httpHeaderFieldsFor(const QJsonObject &mediaSource) const {
    QStringList fields;
    const QJsonObject headers = mediaSource["RequiredHttpHeaders"].toObject();
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        const QString value = it.value().toString();
        if (!it.key().isEmpty() && !value.isEmpty())
            fields << QString("%1: %2").arg(it.key(), value);
    }
    return fields.join(",");
}

QJsonObject EmbyJellyfinBackend::playbackDeviceProfile(bool forceTranscode,
                                                       const QString &quality) const {
    const QString effectiveQuality = quality.isEmpty() ? videoQuality() : quality;
    const PlaybackLimits limits = playbackLimitsFor(effectiveQuality, forceTranscode);

    QJsonArray directPlayProfiles;
    if (!forceTranscode) {
        directPlayProfiles.append(QJsonObject{
            {"Container", "mp4,m4v,mov,mkv,webm,ts,avi"},
            {"Type", "Video"}
        });
    }

    QJsonArray transcodingProfiles;
    transcodingProfiles.append(QJsonObject{
        {"Container", "ts"},
        {"Type", "Video"},
        {"VideoCodec", "h264"},
        {"AudioCodec", "aac"},
        {"Protocol", "hls"},
        {"Context", "Streaming"},
        {"MaxAudioChannels", "2"}
    });

    QJsonArray subtitleProfiles;
    subtitleProfiles.append(QJsonObject{{"Format", "srt"}, {"Method", "External"}});
    subtitleProfiles.append(QJsonObject{{"Format", "ass"}, {"Method", "External"}});
    subtitleProfiles.append(QJsonObject{{"Format", "ssa"}, {"Method", "External"}});
    subtitleProfiles.append(QJsonObject{{"Format", "vtt"}, {"Method", "External"}});
    subtitleProfiles.append(QJsonObject{{"Format", "pgs"}, {"Method", "Encode"}});
    subtitleProfiles.append(QJsonObject{{"Format", "dvdsub"}, {"Method", "Encode"}});
    subtitleProfiles.append(QJsonObject{{"Format", "dvbsub"}, {"Method", "Encode"}});

    return QJsonObject{
        {"Name", "Tater Tube"},
        {"MaxStreamingBitrate", limits.maxStreamingBitrate},
        {"MaxStaticBitrate", 100000000},
        {"DirectPlayProfiles", directPlayProfiles},
        {"TranscodingProfiles", transcodingProfiles},
        {"ContainerProfiles", QJsonArray{}},
        {"CodecProfiles", QJsonArray{}},
        {"ResponseProfiles", QJsonArray{}},
        {"SubtitleProfiles", subtitleProfiles}
    };
}

QJsonObject EmbyJellyfinBackend::playbackInfoPayload(const QString &partKey,
                                                     const QString &audioId,
                                                     const QString &subtitleId,
                                                     int offsetMs,
                                                     bool forceTranscode,
                                                     const QString &quality) const {
    const QString effectiveQuality = quality.isEmpty() ? videoQuality() : quality;
    const PlaybackLimits limits = playbackLimitsFor(effectiveQuality, forceTranscode);
    QJsonObject body{
        {"UserId", userId()},
        {"StartTimeTicks", static_cast<double>(msToTicks(offsetMs))},
        {"MaxAudioChannels", 2},
        {"MaxStreamingBitrate", limits.maxStreamingBitrate},
        {"AutoOpenLiveStream", true},
        {"EnableDirectPlay", !forceTranscode},
        {"EnableDirectStream", !forceTranscode},
        {"EnableTranscoding", true},
        {"AllowVideoStreamCopy", !forceTranscode},
        {"AllowAudioStreamCopy", !forceTranscode},
        {"DeviceProfile", playbackDeviceProfile(forceTranscode, effectiveQuality)}
    };

    if (!partKey.isEmpty())
        body["MediaSourceId"] = partKey;

    const int audioIndex = streamIndexFromId(audioId);
    if (audioIndex >= 0)
        body["AudioStreamIndex"] = audioIndex;

    if (!subtitleId.isEmpty()) {
        const int subtitleIndex = streamIndexFromId(subtitleId);
        body["SubtitleStreamIndex"] = subtitleIndex >= 0 ? subtitleIndex : -1;
        body["AlwaysBurnInSubtitleWhenTranscoding"] = forceTranscode && subtitleIndex >= 0;
    }

    if (forceTranscode) {
        body["AudioBitRate"] = limits.audioBitRate;
        body["VideoBitRate"] = limits.videoBitRate;
        body["MaxWidth"] = limits.maxWidth;
        body["MaxHeight"] = limits.maxHeight;
    }

    return body;
}

QString EmbyJellyfinBackend::playbackUrlFromInfo(const QJsonObject &info,
                                                 const QString &ratingKey,
                                                 const QString &partKey,
                                                 const QString &sessionId,
                                                 const QString &audioId,
                                                 const QString &subtitleId,
                                                 bool forceTranscode,
                                                 QJsonObject *selectedSource,
                                                 const QString &quality) const {
    const QJsonArray sources = info["MediaSources"].toArray();
    if (sources.isEmpty()) return {};

    QJsonObject mediaSource = sources.first().toObject();
    for (const auto &sourceValue : sources) {
        const QJsonObject source = sourceValue.toObject();
        if (source["Id"].toString().compare(partKey, Qt::CaseInsensitive) == 0) {
            mediaSource = source;
            break;
        }
    }

    if (selectedSource)
        *selectedSource = mediaSource;

    QString playSessionId = info["PlaySessionId"].toString();
    if (playSessionId.isEmpty())
        playSessionId = sessionId;

    QString mediaSourceId = mediaSource["Id"].toString();
    if (mediaSourceId.isEmpty())
        mediaSourceId = partKey;

    const QString transcodingUrl = mediaSource["TranscodingUrl"].toString();
    const bool directPlay = mediaSource["SupportsDirectPlay"].toBool(false);
    if (!transcodingUrl.isEmpty() && (forceTranscode || !directPlay))
        return withAccessToken(absoluteMediaUrl(transcodingUrl));

    const QString directStreamUrl = mediaSource["DirectStreamUrl"].toString();
    if (!forceTranscode && !directStreamUrl.isEmpty())
        return withAccessToken(absoluteMediaUrl(directStreamUrl));

    if (forceTranscode) {
        qWarning("[EmbyJellyfinBackend] PlaybackInfo did not include TranscodingUrl; using legacy HLS URL fallback");
        return streamUrlFor(ratingKey, mediaSourceId, playSessionId,
                            audioId, subtitleId, true, quality);
    }

    return streamUrlFor(ratingKey, mediaSourceId, playSessionId);
}

QString EmbyJellyfinBackend::streamUrlFor(const QString &itemId,
                                          const QString &mediaSourceId,
                                          const QString &playSessionId,
                                          const QString &audioIndex,
                                          const QString &subtitleIndex,
                                          bool transcode,
                                          const QString &quality) const {
    QUrl url = transcode
        ? apiUrl("/Videos/" + itemId + "/master.m3u8")
        : apiUrl("/Videos/" + itemId + "/stream");

    QUrlQuery q;
    q.addQueryItem("api_key", accessToken());
    if (!mediaSourceId.isEmpty()) q.addQueryItem("MediaSourceId", mediaSourceId);
    if (!playSessionId.isEmpty()) q.addQueryItem("PlaySessionId", playSessionId);
    if (!audioIndex.isEmpty()) q.addQueryItem("AudioStreamIndex", audioIndex);
    if (!subtitleIndex.isEmpty() && subtitleIndex != "-1") {
        q.addQueryItem("SubtitleStreamIndex", subtitleIndex);
        if (transcode)
            q.addQueryItem("SubtitleMethod", "Encode");
    }

    if (transcode) {
        q.addQueryItem("VideoCodec", "h264");
        q.addQueryItem("AudioCodec", "aac");
        q.addQueryItem("SegmentContainer", "ts");
        q.addQueryItem("TranscodingContainer", "ts");
        q.addQueryItem("TranscodingProtocol", "hls");
        q.addQueryItem("EnableAutoStreamCopy", "false");
        q.addQueryItem("AllowVideoStreamCopy", "false");
        q.addQueryItem("RequireAvc", "true");
        q.addQueryItem("MaxAudioChannels", "2");
        q.addQueryItem("AudioBitRate", "128000");

        const QString effectiveQuality = quality.isEmpty() ? videoQuality() : quality;
        int maxWidth = 854;
        int maxHeight = 480;
        int videoBitRate = 2000000;
        if (effectiveQuality == "2160p") {
            maxWidth = 3840;
            maxHeight = 2160;
            videoBitRate = 20000000;
        } else if (effectiveQuality == "1440p") {
            maxWidth = 2560;
            maxHeight = 1440;
            videoBitRate = 12000000;
        } else if (effectiveQuality == "1080p") {
            maxWidth = 1920;
            maxHeight = 1080;
            videoBitRate = 8000000;
        } else if (effectiveQuality == "720p") {
            maxWidth = 1280;
            maxHeight = 720;
            videoBitRate = 4000000;
        }

        q.addQueryItem("MaxWidth", QString::number(maxWidth));
        q.addQueryItem("MaxHeight", QString::number(maxHeight));
        q.addQueryItem("VideoBitRate", QString::number(videoBitRate));
        // Emby honors MaxStreamingBitrate on its HLS endpoints; Jellyfin's
        // current video HLS route uses VideoBitRate/MaxWidth/MaxHeight above.
        q.addQueryItem("MaxStreamingBitrate", QString::number(videoBitRate + 128000));
    } else {
        q.addQueryItem("Static", "true");
    }

    url.setQuery(q);
    return url.toString();
}

QVariantMap EmbyJellyfinBackend::buildItemDetail(const QJsonObject &item) const {
    QJsonArray mediaSources = item["MediaSources"].toArray();
    QJsonObject mediaSource = mediaSources.isEmpty() ? QJsonObject{} : mediaSources.first().toObject();
    QString mediaSourceId = mediaSource["Id"].toString();
    if (mediaSourceId.isEmpty()) mediaSourceId = item["Id"].toString();

    QJsonArray streams = mediaSource["MediaStreams"].toArray();
    if (streams.isEmpty()) streams = item["MediaStreams"].toArray();

    QVariantList audioStreams;
    QVariantList subtitleStreams;
    subtitleStreams.append(QVariantMap{{"id","-1"},{"displayTitle","OFF"},
                                       {"language",""},{"selected",false},
                                       {"imageSubtitle",false}});

    QString selectedAudio;
    QString selectedSubtitle = "-1";
    QString videoCodec;
    const int defaultAudio = mediaSource["DefaultAudioStreamIndex"].toInt(-1);
    const int defaultSubtitle = mediaSource["DefaultSubtitleStreamIndex"].toInt(-1);

    static const QSet<QString> kImageSubCodecs = {
        "pgssub", "dvdsub", "dvbsub", "hdmv_pgs_subtitle", "dvd_subtitle"
    };

    for (const auto &sv : streams) {
        QJsonObject s = sv.toObject();
        QString type = s["Type"].toString();
        int index = s["Index"].toInt(-1);
        if (index < 0) continue;

        QString id = QString::number(index);
        QString lang = s["Language"].toString().toLower();
        QString display = s["DisplayTitle"].toString();
        if (display.isEmpty()) display = s["Title"].toString();
        if (display.isEmpty()) display = lang.isEmpty() ? QStringLiteral("UNKNOWN") : lang;
        QString codec = s["Codec"].toString().toLower();

        if (type == "Audio") {
            audioStreams.append(QVariantMap{{"id", id},
                                            {"displayTitle", display.toUpper()},
                                            {"language", lang}});
            if (index == defaultAudio && selectedAudio.isEmpty())
                selectedAudio = id;
        } else if (type == "Video") {
            if (videoCodec.isEmpty())
                videoCodec = codec;
        } else if (type == "Subtitle") {
            bool isImage = kImageSubCodecs.contains(codec);
            QString delivery = s["DeliveryUrl"].toString();
            QString subUrl = delivery.isEmpty()
                ? subtitleUrlFor(item["Id"].toString(), mediaSourceId, index, codec)
                : (delivery.startsWith("http") ? delivery : serverUrl() + delivery);
            if (!subUrl.contains("api_key=")) {
                QUrl u(subUrl);
                QUrlQuery q(u);
                q.addQueryItem("api_key", accessToken());
                u.setQuery(q);
                subUrl = u.toString();
            }
            subtitleStreams.append(QVariantMap{{"id", id},
                                               {"displayTitle", display.toUpper()},
                                               {"language", lang},
                                               {"imageSubtitle", isImage},
                                               {"subUrl", subUrl}});
            if (index == defaultSubtitle)
                selectedSubtitle = id;
        }
    }

    if (selectedAudio.isEmpty() && !audioStreams.isEmpty())
        selectedAudio = audioStreams[0].toMap()["id"].toString();

    QVariantMap base = formatItem(item);
    base["partKey"] = mediaSourceId;
    base["partId"] = mediaSourceId;
    base["audioStreams"] = audioStreams;
    base["subtitleStreams"] = subtitleStreams;
    base["selectedAudioId"] = selectedAudio;
    base["selectedSubtitleId"] = selectedSubtitle;
    base["videoCodec"] = videoCodec;
    base["forceTranscode"] = true;
    return base;
}

void EmbyJellyfinBackend::load_item_detail(const QString &ratingKey) {
    if (mediaProvider() == kProviderPlex) {
        plexLoadItemDetail(ratingKey);
        return;
    }

    QUrl url = apiUrl("/Users/" + userId() + "/Items/" + ratingKey);
    QUrlQuery q;
    q.addQueryItem("Fields", "MediaSources,MediaStreams,Overview,Genres,ParentId,UserData");
    url.setQuery(q);

    auto *reply = apiGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred("LOAD DETAIL FAILED: " + reply->errorString());
            return;
        }
        QJsonObject item = QJsonDocument::fromJson(reply->readAll()).object();
        QVariantMap detail = buildItemDetail(item);
        if (detail["partKey"].toString().isEmpty()) {
            emit errorOccurred("LOAD DETAIL FAILED: NO MEDIA SOURCE");
            return;
        }
        emit itemLoaded(detail);
    });
}

void EmbyJellyfinBackend::api_prepare_media_launch(const QString &requestId,
                                                   const QString &ratingKey,
                                                   const QString &kind) {
    const QString cleanRatingKey = ratingKey.trimmed();
    if (cleanRatingKey.isEmpty()) {
        emit apiRequestFailed(requestId, QStringLiteral("MEDIA LAUNCH FAILED: EMPTY ITEM ID"));
        return;
    }

    QString cleanKind = kind.trimmed().toLower();
    if (cleanKind == QStringLiteral("series") || cleanKind == QStringLiteral("tv") ||
        cleanKind == QStringLiteral("tv_show"))
        cleanKind = QStringLiteral("show");

    if (cleanKind == QStringLiteral("show")) {
        apiPrepareShowPlayback(requestId, cleanRatingKey);
        return;
    }

    if (mediaProvider() == kProviderPlex) {
        auto *reply = plexServerGet(plexApiUrl(QStringLiteral("/library/metadata/%1").arg(cleanRatingKey)));
        connect(reply, &QNetworkReply::finished, this, [this, reply, requestId]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                emit apiRequestFailed(requestId, QStringLiteral("LOAD PLEX ITEM FAILED: ") + reply->errorString());
                return;
            }
            apiPrepareDetailPlayback(requestId, parsePlexDetail(reply->readAll()));
        });
        return;
    }

    QUrl url = apiUrl(QStringLiteral("/Users/") + userId() + QStringLiteral("/Items/") + cleanRatingKey);
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("Fields"),
                   QStringLiteral("MediaSources,MediaStreams,Overview,Genres,ParentId,UserData"));
    url.setQuery(q);

    auto *reply = apiGet(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit apiRequestFailed(requestId, QStringLiteral("LOAD MEDIA ITEM FAILED: ") + reply->errorString());
            return;
        }
        apiPrepareDetailPlayback(requestId,
                                 buildItemDetail(QJsonDocument::fromJson(reply->readAll()).object()));
    });
}

void EmbyJellyfinBackend::apiPrepareDetailPlayback(const QString &requestId,
                                                   const QVariantMap &detail) {
    const QString ratingKey = detail.value(QStringLiteral("ratingKey")).toString();
    const QString partKey = detail.value(QStringLiteral("partKey")).toString();
    if (ratingKey.isEmpty() || partKey.isEmpty()) {
        emit apiRequestFailed(requestId, QStringLiteral("MEDIA LAUNCH FAILED: NO PLAYABLE STREAM"));
        return;
    }

    const QString kind = detail.value(QStringLiteral("type")).toString();
    QVariantMap launch;
    launch[QStringLiteral("id")] = QStringLiteral("vod:%1:%2").arg(
        kind,
        QString::fromLatin1(QUrl::toPercentEncoding(ratingKey)));
    launch[QStringLiteral("module")] = QStringLiteral("video_on_demand");
    launch[QStringLiteral("provider")] = mediaProvider().toLower();
    launch[QStringLiteral("kind")] = kind;
    launch[QStringLiteral("title")] = detail.value(QStringLiteral("title")).toString();
    launch[QStringLiteral("rating_key")] = ratingKey;
    launch[QStringLiteral("part_key")] = partKey;
    launch[QStringLiteral("view_offset_ms")] = detail.value(QStringLiteral("viewOffset")).toInt();
    if (detail.contains(QStringLiteral("grandparentTitle")) &&
        !detail.value(QStringLiteral("grandparentTitle")).toString().isEmpty())
        launch[QStringLiteral("series")] = detail.value(QStringLiteral("grandparentTitle"));

    if (mediaProvider() == kProviderPlex) {
        const QString sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString audioId = detail.value(QStringLiteral("selectedAudioId")).toString();
        plexRequestTranscodeUrl(ratingKey, partKey, sessionId, audioId, {}, 0,
                                [this, requestId, launch](const QString &url,
                                                          const QString &httpHeaderFields) {
            emit apiLaunchStreamReady(requestId, launch, url, httpHeaderFields);
        }, [this, requestId](const QString &message) {
            emit apiRequestFailed(requestId, message);
        });
        return;
    }

    const QString sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString audioId = detail.value(QStringLiteral("selectedAudioId")).toString();
    const bool forceTranscode = detail.value(QStringLiteral("forceTranscode")).toBool();
    const QJsonObject payload = playbackInfoPayload(partKey, audioId, {}, 0, forceTranscode);
    auto *reply = apiPostJson(apiUrl(QStringLiteral("/Items/") + ratingKey + QStringLiteral("/PlaybackInfo")),
                              QJsonDocument(payload).toJson(QJsonDocument::Compact));

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, requestId, launch, ratingKey, partKey, sessionId, audioId, forceTranscode]() {
        reply->deleteLater();
        const QByteArray bytes = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            QString message = QStringLiteral("PLAYBACK INFO FAILED: ") + reply->errorString();
            const QString body = abbreviatedNetworkBody(bytes);
            if (!body.isEmpty())
                message += QStringLiteral(" - ") + body;
            emit apiRequestFailed(requestId, message);
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            emit apiRequestFailed(requestId, QStringLiteral("PLAYBACK INFO FAILED: INVALID SERVER RESPONSE"));
            return;
        }

        const QJsonObject info = doc.object();
        const QString errorCode = info[QStringLiteral("ErrorCode")].toString();
        if (!errorCode.isEmpty()) {
            emit apiRequestFailed(requestId, QStringLiteral("PLAYBACK INFO FAILED: ") + errorCode);
            return;
        }

        QJsonObject mediaSource;
        const QString url = playbackUrlFromInfo(info, ratingKey, partKey, sessionId,
                                                audioId, {}, forceTranscode, &mediaSource);
        if (url.isEmpty()) {
            emit apiRequestFailed(requestId, QStringLiteral("PLAYBACK INFO FAILED: NO PLAYABLE STREAM"));
            return;
        }

        emit apiLaunchStreamReady(requestId, launch, url, httpHeaderFieldsFor(mediaSource));
    });
}

void EmbyJellyfinBackend::apiPrepareShowPlayback(const QString &requestId,
                                                 const QString &ratingKey) {
    if (mediaProvider() == kProviderPlex) {
        apiPreparePlexShowPlayback(requestId, ratingKey);
        return;
    }

    fetchEpisodesForSeries(ratingKey, [this, requestId](QJsonArray episodes) {
        if (episodes.isEmpty()) {
            emit apiRequestFailed(requestId, QStringLiteral("SHOW LAUNCH FAILED: NO EPISODES"));
            return;
        }

        QJsonObject target;
        for (const auto &v : episodes) {
            const QJsonObject ep = v.toObject();
            if (ticksToMs(ep[QStringLiteral("UserData")].toObject()
                              [QStringLiteral("PlaybackPositionTicks")]) > 0) {
                target = ep;
                break;
            }
        }
        if (target.isEmpty()) {
            for (const auto &v : episodes) {
                const QJsonObject ep = v.toObject();
                if (!ep[QStringLiteral("UserData")].toObject()
                        [QStringLiteral("Played")].toBool()) {
                    target = ep;
                    break;
                }
            }
        }
        if (target.isEmpty())
            target = episodes.first().toObject();

        apiPrepareDetailPlayback(requestId, buildItemDetail(target));
    });
}

void EmbyJellyfinBackend::apiPreparePlexShowPlayback(const QString &requestId,
                                                     const QString &ratingKey) {
    auto *seasonsReply = plexServerGet(plexApiUrl(QStringLiteral("/library/metadata/%1/children").arg(ratingKey)));
    connect(seasonsReply, &QNetworkReply::finished, this,
            [this, seasonsReply, requestId]() {
        seasonsReply->deleteLater();
        if (seasonsReply->error() != QNetworkReply::NoError) {
            emit apiRequestFailed(requestId, QStringLiteral("LOAD PLEX SEASONS FAILED: ") + seasonsReply->errorString());
            return;
        }

        const QVariantList seasons = parsePlexItems(seasonsReply->readAll());
        if (seasons.isEmpty()) {
            emit apiRequestFailed(requestId, QStringLiteral("SHOW LAUNCH FAILED: NO SEASONS"));
            return;
        }

        QVariantMap targetSeason;
        for (const QVariant &v : seasons) {
            const QVariantMap season = v.toMap();
            if (season.value(QStringLiteral("viewedLeafCount")).toInt() <
                season.value(QStringLiteral("leafCount")).toInt()) {
                targetSeason = season;
                break;
            }
        }
        if (targetSeason.isEmpty())
            targetSeason = seasons.first().toMap();

        const QString seasonKey = targetSeason.value(QStringLiteral("ratingKey")).toString();
        auto *episodesReply = plexServerGet(plexApiUrl(QStringLiteral("/library/metadata/%1/children").arg(seasonKey)));
        connect(episodesReply, &QNetworkReply::finished, this,
                [this, episodesReply, requestId]() {
            episodesReply->deleteLater();
            if (episodesReply->error() != QNetworkReply::NoError) {
                emit apiRequestFailed(requestId, QStringLiteral("LOAD PLEX EPISODES FAILED: ") + episodesReply->errorString());
                return;
            }

            const QVariantMap episode = firstPlayableEpisodeFromItems(parsePlexItems(episodesReply->readAll()));
            const QString episodeKey = episode.value(QStringLiteral("ratingKey")).toString();
            if (episodeKey.isEmpty()) {
                emit apiRequestFailed(requestId, QStringLiteral("SHOW LAUNCH FAILED: NO EPISODES"));
                return;
            }
            api_prepare_media_launch(requestId, episodeKey, QStringLiteral("episode"));
        });
    });
}

void EmbyJellyfinBackend::build_stream_url(const QString &ratingKey,
                                           const QString &partKey,
                                           const QString &sessionId) {
    if (mediaProvider() == kProviderPlex) {
        plexRequestTranscodeUrl(ratingKey, partKey, sessionId, {}, {}, 0,
                                [this](const QString &url, const QString &httpHeaderFields) {
            emit streamUrlReady(url, httpHeaderFields);
        }, [this](const QString &message) {
            emit errorOccurred(message);
        });
        return;
    }

    requestPlaybackInfo(ratingKey, partKey, sessionId, {}, {}, 0, false);
}

void EmbyJellyfinBackend::build_audio_stream_url(const QString &ratingKey,
                                                 const QString &mediaSourceId) {
    if (mediaProvider() == kProviderPlex) {
        plexBuildAudioStreamUrl(ratingKey, mediaSourceId);
        return;
    }

    if (ratingKey.isEmpty()) {
        emit errorOccurred("AUDIO STREAM FAILED: EMPTY ITEM ID");
        return;
    }

    QUrl url = apiUrl("/Audio/" + ratingKey + "/stream");
    QUrlQuery q;
    q.addQueryItem("api_key", accessToken());
    q.addQueryItem("Static", "true");
    if (!mediaSourceId.isEmpty())
        q.addQueryItem("MediaSourceId", mediaSourceId);
    url.setQuery(q);

    emit audioStreamUrlReady(ratingKey, url.toString(), {});
}

void EmbyJellyfinBackend::request_transcode(const QString &ratingKey,
                                            const QString &partKey,
                                            const QString &sessionId,
                                            const QString &audioId,
                                            const QString &subtitleId,
                                            int offsetMs) {
    if (mediaProvider() == kProviderPlex) {
        plexRequestTranscodeUrl(ratingKey, partKey, sessionId, audioId, subtitleId, offsetMs,
                                [this](const QString &url, const QString &httpHeaderFields) {
            emit streamUrlReady(url, httpHeaderFields);
        }, [this](const QString &message) {
            emit errorOccurred(message);
        });
        return;
    }

    requestPlaybackInfo(ratingKey, partKey, sessionId,
                        audioId, subtitleId, offsetMs, true);
}

void EmbyJellyfinBackend::request_live_tv_stream(const QString &channelId,
                                                 const QString &sessionId,
                                                 bool forceTranscode) {
    if (channelId.isEmpty()) {
        emit errorOccurred("OTA PLAYBACK FAILED: EMPTY CHANNEL ID");
        return;
    }

    closeActiveLiveTvStream(false);
    const int requestSerial = ++m_liveTvRequestSerial;

    const QString liveTvQuality = forceTranscode ? kOtaVideoQuality : QString{};
    QJsonObject payload = playbackInfoPayload({}, {}, {}, 0,
                                              forceTranscode, liveTvQuality);
    auto *reply = apiPostJson(apiUrl("/Items/" + channelId + "/PlaybackInfo"),
                              QJsonDocument(payload).toJson(QJsonDocument::Compact));

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, channelId, sessionId, forceTranscode, liveTvQuality, requestSerial]() {
        reply->deleteLater();
        const QByteArray bytes = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            QString message = "OTA PLAYBACK FAILED: " + reply->errorString();
            const QString body = abbreviatedNetworkBody(bytes);
            if (!body.isEmpty())
                message += " - " + body;
            emit errorOccurred(message);
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            emit errorOccurred("OTA PLAYBACK FAILED: INVALID SERVER RESPONSE");
            return;
        }

        const QJsonObject info = doc.object();
        const QString errorCode = info["ErrorCode"].toString();
        if (!errorCode.isEmpty()) {
            emit errorOccurred("OTA PLAYBACK FAILED: " + errorCode);
            return;
        }

        QJsonObject mediaSource;
        const QString url = playbackUrlFromInfo(info, channelId, {}, sessionId,
                                                {}, {}, forceTranscode, &mediaSource,
                                                liveTvQuality);
        QString playSessionId = info["PlaySessionId"].toString();
        if (playSessionId.isEmpty())
            playSessionId = sessionId;
        QString mediaSourceId = mediaSource["Id"].toString();
        if (mediaSourceId.isEmpty())
            mediaSourceId = channelId;
        const QString liveStreamId = mediaSource["LiveStreamId"].toString();

        if (url.isEmpty()) {
            closeLiveTvStream(channelId, mediaSourceId, liveStreamId, playSessionId, true);
            emit errorOccurred("OTA PLAYBACK FAILED: NO PLAYABLE STREAM");
            return;
        }

        if (requestSerial != m_liveTvRequestSerial) {
            closeLiveTvStream(channelId, mediaSourceId, liveStreamId, playSessionId, true);
            return;
        }

        m_liveTvItemId = channelId;
        m_liveTvMediaSourceId = mediaSourceId;
        m_liveTvLiveStreamId = liveStreamId;
        m_liveTvPlaySessionId = playSessionId;

        const QString method = mediaSource["TranscodingUrl"].toString().isEmpty()
            ? QStringLiteral("direct")
            : QStringLiteral("transcode");
        qInfo("[EmbyJellyfinBackend] Live TV selected %s media source %s",
              qPrintable(method),
              qPrintable(mediaSource["Id"].toString()));

        emit liveTvStreamReady(channelId, url, httpHeaderFieldsFor(mediaSource));
    });
}

void EmbyJellyfinBackend::stop_live_tv_stream(bool failed) {
    ++m_liveTvRequestSerial;
    closeActiveLiveTvStream(failed);
}

void EmbyJellyfinBackend::closeActiveLiveTvStream(bool failed) {
    if (m_liveTvItemId.isEmpty() &&
        m_liveTvMediaSourceId.isEmpty() &&
        m_liveTvLiveStreamId.isEmpty() &&
        m_liveTvPlaySessionId.isEmpty()) {
        return;
    }

    const QString itemId = m_liveTvItemId;
    const QString mediaSourceId = m_liveTvMediaSourceId;
    const QString liveStreamId = m_liveTvLiveStreamId;
    const QString playSessionId = m_liveTvPlaySessionId;

    m_liveTvItemId.clear();
    m_liveTvMediaSourceId.clear();
    m_liveTvLiveStreamId.clear();
    m_liveTvPlaySessionId.clear();

    closeLiveTvStream(itemId, mediaSourceId, liveStreamId, playSessionId, failed);
}

void EmbyJellyfinBackend::closeLiveTvStream(const QString &itemId,
                                            const QString &mediaSourceId,
                                            const QString &liveStreamId,
                                            const QString &playSessionId,
                                            bool failed) {
    if (itemId.isEmpty() &&
        mediaSourceId.isEmpty() &&
        liveStreamId.isEmpty() &&
        playSessionId.isEmpty()) {
        return;
    }

    if (!itemId.isEmpty()) {
        QJsonObject body{
            {"ItemId", itemId},
            {"PositionTicks", 0},
            {"Failed", failed}
        };
        if (!mediaSourceId.isEmpty())
            body["MediaSourceId"] = mediaSourceId;
        if (!liveStreamId.isEmpty())
            body["LiveStreamId"] = liveStreamId;
        if (!playSessionId.isEmpty())
            body["PlaySessionId"] = playSessionId;

        auto *reply = apiPostJson(apiUrl("/Sessions/Playing/Stopped"),
                                  QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(reply, &QNetworkReply::finished, this, [reply]() {
            const QByteArray bytes = reply->readAll();
            if (reply->error() != QNetworkReply::NoError) {
                qWarning("[EmbyJellyfinBackend] Live TV playback stop failed: %s %s",
                         qPrintable(reply->errorString()),
                         qPrintable(abbreviatedNetworkBody(bytes)));
            }
            reply->deleteLater();
        });
    }

    if (!liveStreamId.isEmpty()) {
        QUrl closeUrl = apiUrl("/LiveStreams/Close");
        QUrlQuery q;
        q.addQueryItem("liveStreamId", liveStreamId);
        closeUrl.setQuery(q);

        auto *reply = apiPostJson(closeUrl, QByteArray("{}"));
        connect(reply, &QNetworkReply::finished, this, [reply, liveStreamId]() {
            const QByteArray bytes = reply->readAll();
            if (reply->error() != QNetworkReply::NoError) {
                qWarning("[EmbyJellyfinBackend] Live TV stream close failed for %s: %s %s",
                         qPrintable(liveStreamId),
                         qPrintable(reply->errorString()),
                         qPrintable(abbreviatedNetworkBody(bytes)));
            } else {
                qInfo("[EmbyJellyfinBackend] Closed Live TV stream %s",
                      qPrintable(liveStreamId));
            }
            reply->deleteLater();
        });
    }
}

void EmbyJellyfinBackend::requestPlaybackInfo(const QString &ratingKey,
                                              const QString &partKey,
                                              const QString &sessionId,
                                              const QString &audioId,
                                              const QString &subtitleId,
                                              int offsetMs,
                                              bool forceTranscode) {
    if (ratingKey.isEmpty()) {
        emit errorOccurred("PLAYBACK INFO FAILED: EMPTY ITEM ID");
        return;
    }

    QJsonObject payload = playbackInfoPayload(partKey, audioId, subtitleId,
                                              offsetMs, forceTranscode);
    auto *reply = apiPostJson(apiUrl("/Items/" + ratingKey + "/PlaybackInfo"),
                              QJsonDocument(payload).toJson(QJsonDocument::Compact));

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, ratingKey, partKey, sessionId, audioId, subtitleId, forceTranscode]() {
        reply->deleteLater();
        const QByteArray bytes = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            QString message = "PLAYBACK INFO FAILED: " + reply->errorString();
            const QString body = abbreviatedNetworkBody(bytes);
            if (!body.isEmpty())
                message += " - " + body;
            emit errorOccurred(message);
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            emit errorOccurred("PLAYBACK INFO FAILED: INVALID SERVER RESPONSE");
            return;
        }

        const QJsonObject info = doc.object();
        const QString errorCode = info["ErrorCode"].toString();
        if (!errorCode.isEmpty()) {
            emit errorOccurred("PLAYBACK INFO FAILED: " + errorCode);
            return;
        }

        QJsonObject mediaSource;
        const QString url = playbackUrlFromInfo(info, ratingKey, partKey, sessionId,
                                                audioId, subtitleId,
                                                forceTranscode, &mediaSource);
        if (url.isEmpty()) {
            emit errorOccurred("PLAYBACK INFO FAILED: NO PLAYABLE STREAM");
            return;
        }

        const QString method = mediaSource["TranscodingUrl"].toString().isEmpty()
            ? QStringLiteral("direct")
            : QStringLiteral("transcode");
        qInfo("[EmbyJellyfinBackend] PlaybackInfo selected %s media source %s",
              qPrintable(method),
              qPrintable(mediaSource["Id"].toString(partKey)));

        emit streamUrlReady(url, httpHeaderFieldsFor(mediaSource));
    });
}

void EmbyJellyfinBackend::update_timeline(const QString &ratingKey,
                                          const QString &partKey,
                                          const QString &state,
                                          int timeMs,
                                          int durationMs) {
    Q_UNUSED(durationMs)
    if (mediaProvider() == kProviderPlex) {
        if (ratingKey.isEmpty() || plexToken().isEmpty()) return;
        QUrl url = plexApiUrl("/:/timeline");
        QUrlQuery q;
        q.addQueryItem("ratingKey", ratingKey);
        q.addQueryItem("key", "/library/metadata/" + ratingKey);
        q.addQueryItem("state", state);
        q.addQueryItem("time", QString::number(std::max(0, timeMs)));
        q.addQueryItem("duration", QString::number(std::max(0, durationMs)));
        url.setQuery(q);
        auto *reply = plexServerGet(url);
        connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
        return;
    }

    if (ratingKey.isEmpty() || accessToken().isEmpty()) return;

    QString endpoint = (state == "stopped")
        ? QStringLiteral("/Sessions/Playing/Stopped")
        : QStringLiteral("/Sessions/Playing/Progress");

    QJsonObject body{
        {"ItemId", ratingKey},
        {"MediaSourceId", partKey},
        {"PositionTicks", static_cast<double>(msToTicks(timeMs))},
        {"IsPaused", false},
        {"CanSeek", true}
    };

    auto *reply = apiPostJson(apiUrl(endpoint), QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
}

void EmbyJellyfinBackend::set_audio_stream(const QString &streamId,
                                           const QString &partId) {
    Q_UNUSED(streamId)
    Q_UNUSED(partId)
}

void EmbyJellyfinBackend::set_subtitle_stream(const QString &streamId,
                                              const QString &partId) {
    Q_UNUSED(streamId)
    Q_UNUSED(partId)
}

void EmbyJellyfinBackend::fetchEpisodesForSeries(
    const QString &seriesId,
    std::function<void(QJsonArray)> callback) {
    QUrl url = apiUrl("/Shows/" + seriesId + "/Episodes");
    QUrlQuery q;
    q.addQueryItem("UserId", userId());
    q.addQueryItem("Fields", "MediaSources,MediaStreams,Overview,ParentId,UserData");
    q.addQueryItem("SortBy", "ParentIndexNumber,IndexNumber");
    q.addQueryItem("SortOrder", "Ascending");
    url.setQuery(q);

    auto *reply = apiGet(url);
    connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            callback({});
            return;
        }
        callback(QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray());
    });
}

void EmbyJellyfinBackend::load_on_deck_for(const QString &ratingKey) {
    if (mediaProvider() == kProviderPlex) {
        Q_UNUSED(ratingKey)
        emit inProgressEpisodeLoaded(QVariantMap{});
        return;
    }

    fetchEpisodesForSeries(ratingKey, [this](QJsonArray episodes) {
        for (const auto &v : episodes) {
            QJsonObject ep = v.toObject();
            if (ticksToMs(ep["UserData"].toObject()["PlaybackPositionTicks"]) > 0) {
                emit inProgressEpisodeLoaded(formatItem(ep));
                return;
            }
        }
        emit inProgressEpisodeLoaded(QVariantMap{});
    });
}

void EmbyJellyfinBackend::load_next_episode(const QString &currentRatingKey) {
    if (mediaProvider() == kProviderPlex) {
        plexLoadNextEpisode(currentRatingKey);
        return;
    }

    QUrl detailUrl = apiUrl("/Users/" + userId() + "/Items/" + currentRatingKey);
    QUrlQuery dq;
    dq.addQueryItem("Fields", "MediaSources,MediaStreams,Overview,ParentId,UserData");
    detailUrl.setQuery(dq);

    auto *reply = apiGet(detailUrl);
    connect(reply, &QNetworkReply::finished, this, [this, reply, currentRatingKey]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit nextEpisodeReady(QVariantMap{});
            return;
        }

        QJsonObject current = QJsonDocument::fromJson(reply->readAll()).object();
        QString seriesId = current["SeriesId"].toString();
        if (seriesId.isEmpty()) {
            emit nextEpisodeReady(QVariantMap{});
            return;
        }

        fetchEpisodesForSeries(seriesId, [this, currentRatingKey](QJsonArray episodes) {
            bool foundCurrent = false;
            for (const auto &v : episodes) {
                QJsonObject ep = v.toObject();
                if (foundCurrent) {
                    QVariantMap detail = buildItemDetail(ep);
                    if (!detail["partKey"].toString().isEmpty()) {
                        emit nextEpisodeReady(detail);
                        return;
                    }
                }
                if (ep["Id"].toString() == currentRatingKey)
                    foundCurrent = true;
            }
            emit nextEpisodeReady(QVariantMap{});
        });
    });
}

QJsonObject EmbyJellyfinBackend::vodTvCacheIdentity() const {
    const QJsonObject moduleConfig = loadConfig().value(QStringLiteral("modules")).toObject()
        .value(kModuleId).toObject();
    QJsonObject identity{
        {QStringLiteral("provider"), mediaProvider()},
        {QStringLiteral("schema"), QStringLiteral("vod-tv-themed-channels-v3")},
        {QStringLiteral("customChannels"),
         moduleConfig.value(QStringLiteral("custom_vod_tv_channels")).toArray()}
    };
    if (mediaProvider() == kProviderPlex) {
        const QJsonObject auth = loadPlexAuth();
        identity[QStringLiteral("server")] = plexServerUrl();
        identity[QStringLiteral("machineIdentifier")] =
            auth.value(QStringLiteral("machine_identifier")).toString();
    } else {
        identity[QStringLiteral("server")] = serverUrl();
        identity[QStringLiteral("userId")] = userId();
    }
    return identity;
}

QString EmbyJellyfinBackend::vodTvCachePath() const {
    return m_dataRoot + QStringLiteral("/vod_tv_cache.json");
}

bool EmbyJellyfinBackend::emitVodTvChannelsFromCache() {
    QFile file(vodTvCachePath());
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    const QJsonObject cache = doc.object();
    if (cache.value(QStringLiteral("identity")).toObject() != vodTvCacheIdentity())
        return false;

    const QVariantList channels =
        cache.value(QStringLiteral("channels")).toArray().toVariantList();
    if (channels.isEmpty())
        return false;

    emit vodTvChannelsLoaded(channels);
    return true;
}

void EmbyJellyfinBackend::saveVodTvChannelsCache(const QVariantList &channels) const {
    QFile file(vodTvCachePath());
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning("[EmbyJellyfinBackend] Could not write VoD TV cache: %s",
                 qPrintable(file.errorString()));
        return;
    }

    const QJsonObject cache{
        {QStringLiteral("identity"), vodTvCacheIdentity()},
        {QStringLiteral("updatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("channels"), QJsonArray::fromVariantList(channels)}
    };
    file.write(QJsonDocument(cache).toJson(QJsonDocument::Indented));
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

void EmbyJellyfinBackend::load_vod_tv_channels(bool refresh) {
    if (get_auth_state() != QStringLiteral("authed")) {
        emit errorOccurred(QStringLiteral("NOT SIGNED IN"));
        emit vodTvChannelsLoaded(QVariantList{});
        return;
    }

    if (!refresh && emitVodTvChannelsFromCache())
        return;

    buildVodTvChannels(false);
}

void EmbyJellyfinBackend::refresh_vod_tv_cache() {
    if (get_auth_state() != QStringLiteral("authed")) {
        emit errorOccurred(QStringLiteral("NOT SIGNED IN"));
        return;
    }
    buildVodTvChannels(true);
}

void EmbyJellyfinBackend::buildVodTvChannels(bool notifyRefresh) {
    const QJsonObject enabled = loadConfig()[QStringLiteral("modules")].toObject()
        [kModuleId].toObject()[QStringLiteral("libraries")].toObject();

    if (mediaProvider() == kProviderPlex) {
        auto *reply = plexServerGet(plexApiUrl(QStringLiteral("/library/sections")));
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, enabled, notifyRefresh]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                emit errorOccurred(QStringLiteral("LOAD VOD TV LIBRARIES FAILED: ") +
                                   reply->errorString());
                emit vodTvChannelsLoaded(QVariantList{});
                return;
            }

            QVariantList libraries = parsePlexLibraries(reply->readAll(), false);
            if (!enabled.isEmpty()) {
                QVariantList filtered;
                for (const QVariant &value : libraries) {
                    const QVariantMap lib = value.toMap();
                    const QString id = lib.value(QStringLiteral("sectionId")).toString();
                    if (enabled.value(id).toBool(true))
                        filtered.append(lib);
                }
                libraries = filtered;
            }
            buildVodTvChannelsFromLibraries(libraries, notifyRefresh);
        });
        return;
    }

    auto *reply = apiGet(apiUrl(QStringLiteral("/Users/") + userId() +
                                QStringLiteral("/Views")));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, enabled, notifyRefresh]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred(QStringLiteral("LOAD VOD TV LIBRARIES FAILED: ") +
                               reply->errorString());
            emit vodTvChannelsLoaded(QVariantList{});
            return;
        }

        static const QSet<QString> kVideoCollections = {
            QStringLiteral("movies"),
            QStringLiteral("tvshows"),
            QStringLiteral("mixed"),
            QStringLiteral("homevideos"),
            QStringLiteral("boxsets")
        };

        QVariantList libraries;
        const QJsonArray views = QJsonDocument::fromJson(reply->readAll())
            .object()[QStringLiteral("Items")].toArray();
        for (const QJsonValue &value : views) {
            const QJsonObject view = value.toObject();
            const QString id = view.value(QStringLiteral("Id")).toString();
            const QString collectionType =
                view.value(QStringLiteral("CollectionType")).toString();
            const QString type = view.value(QStringLiteral("Type")).toString();
            if (id.isEmpty())
                continue;
            if (!collectionType.isEmpty() && !kVideoCollections.contains(collectionType))
                continue;
            if (collectionType.isEmpty() && type != QStringLiteral("CollectionFolder"))
                continue;
            if (!enabled.isEmpty() && !enabled.value(id).toBool(true))
                continue;

            libraries.append(QVariantMap{
                {QStringLiteral("key"), id},
                {QStringLiteral("title"), view.value(QStringLiteral("Name")).toString().toUpper()},
                {QStringLiteral("sectionId"), id},
                {QStringLiteral("sectionType"), collectionType.isEmpty()
                    ? QStringLiteral("mixed")
                    : collectionType}
            });
        }

        buildVodTvChannelsFromLibraries(libraries, notifyRefresh);
    });
}

void EmbyJellyfinBackend::buildVodTvChannelsFromLibraries(const QVariantList &libraries,
                                                          bool notifyRefresh) {
    auto channels = std::make_shared<QVariantList>();
    auto index = std::make_shared<int>(0);
    auto processNext = std::make_shared<std::function<void()>>();

    *processNext = [this, libraries, notifyRefresh, channels, index, processNext]() {
        if (*index >= libraries.size()) {
            finishVodTvChannels(*channels, notifyRefresh);
            return;
        }

        const QVariantMap lib = libraries.at(*index).toMap();
        const QString sectionId = lib.value(QStringLiteral("sectionId")).toString();
        if (sectionId.isEmpty()) {
            ++(*index);
            (*processNext)();
            return;
        }

        const auto finishItems = [this, lib, channels, index, processNext]
                                 (const QVariantList &items) {
            QVariantList movies;
            QVariantList shows;
            for (const QVariant &value : items) {
                const QVariantMap item = value.toMap();
                const QString ratingKey =
                    item.value(QStringLiteral("ratingKey")).toString();
                const QString type = item.value(QStringLiteral("type")).toString();
                if (ratingKey.isEmpty())
                    continue;
                if (isVodMovieType(type)) {
                    movies.append(vodProgramFromItem(item));
                } else if (isVodShowType(type)) {
                    shows.append(vodProgramFromItem(item));
                }
            }

            const auto finishLibrary = [lib, movies, channels, index, processNext]
                                       (const QVariantList &showGroups) {
                const QString title =
                    lib.value(QStringLiteral("title")).toString().toUpper();
                const bool hasMovies = !movies.isEmpty();
                const bool hasShows = !showGroups.isEmpty();
                QSet<QString> channelSignatures;

                if (hasMovies) {
                    QString movieTitle = title;
                    if (hasShows)
                        movieTitle += QStringLiteral(" MOVIES");
                    appendVodChannelIfUseful(channels.get(), &channelSignatures,
                                             movieTitle, QStringLiteral("movie"), movies, 1);
                    appendThemedMovieChannels(channels.get(), &channelSignatures,
                                              title, movies);
                }
                if (hasShows) {
                    QString tvTitle = title;
                    if (hasMovies)
                        tvTitle += QStringLiteral(" TV");
                    appendVodChannelIfUseful(channels.get(), &channelSignatures,
                                             tvTitle, QStringLiteral("tv"), showGroups, 1);
                    appendThemedTvChannels(channels.get(), &channelSignatures,
                                           title, showGroups);
                }

                ++(*index);
                (*processNext)();
            };

            if (shows.isEmpty()) {
                finishLibrary(QVariantList{});
                return;
            }
            fetchVodTvShowGroups(shows, finishLibrary);
        };

        if (mediaProvider() == kProviderPlex) {
            auto *reply = plexServerGet(plexApiUrl(
                QStringLiteral("/library/sections/%1/all").arg(sectionId)));
            connect(reply, &QNetworkReply::finished, this,
                    [this, reply, finishItems, index, processNext]() {
                reply->deleteLater();
                if (reply->error() != QNetworkReply::NoError) {
                    qWarning("[EmbyJellyfinBackend] LOAD VOD TV ITEMS FAILED: %s",
                             qPrintable(reply->errorString()));
                    ++(*index);
                    (*processNext)();
                    return;
                }
                finishItems(parsePlexItems(reply->readAll()));
            });
            return;
        }

        auto *reply = apiGet(itemListUrl(sectionId,
                                         QStringLiteral("Movie,Series,Video"),
                                         true));
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, finishItems, index, processNext]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                qWarning("[EmbyJellyfinBackend] LOAD VOD TV ITEMS FAILED: %s",
                         qPrintable(reply->errorString()));
                ++(*index);
                (*processNext)();
                return;
            }
            const QJsonArray items = QJsonDocument::fromJson(reply->readAll())
                .object()[QStringLiteral("Items")].toArray();
            finishItems(formatItems(items));
        });
    };

    (*processNext)();
}

QVariantList EmbyJellyfinBackend::customVodTvChannelDefinitions() const
{
    const QJsonObject moduleConfig = loadConfig().value(QStringLiteral("modules")).toObject()
        .value(kModuleId).toObject();
    QVariantList result;
    const QJsonArray raw = moduleConfig.value(QStringLiteral("custom_vod_tv_channels")).toArray();
    for (const QJsonValue &value : raw) {
        const QVariantMap channel = value.toObject().toVariantMap();
        const QString title = channel.value(QStringLiteral("title"),
                                            channel.value(QStringLiteral("name"))).toString().trimmed();
        const QVariantList items = channel.value(QStringLiteral("items")).toList();
        if (title.isEmpty() || items.isEmpty())
            continue;
        result.append(channel);
    }
    return result;
}

void EmbyJellyfinBackend::finishVodTvChannels(const QVariantList &autoChannels,
                                              bool notifyRefresh)
{
    const QVariantList definitions = customVodTvChannelDefinitions();
    if (definitions.isEmpty()) {
        saveVodTvChannelsCache(autoChannels);
        if (notifyRefresh) {
            qInfo("[EmbyJellyfinBackend] VoD TV cache refreshed with %d channels",
                  int(autoChannels.size()));
        }
        emit vodTvChannelsLoaded(autoChannels);
        return;
    }

    buildCustomVodTvChannels(definitions, [this, autoChannels, notifyRefresh](QVariantList customChannels) {
        QVariantList channels = customChannels;
        for (const QVariant &channel : autoChannels)
            channels.append(channel);

        saveVodTvChannelsCache(channels);
        if (notifyRefresh) {
            qInfo("[EmbyJellyfinBackend] VoD TV cache refreshed with %d channels",
                  int(channels.size()));
        }
        emit vodTvChannelsLoaded(channels);
    });
}

void EmbyJellyfinBackend::buildCustomVodTvChannels(
    const QVariantList &definitions,
    std::function<void(QVariantList)> callback)
{
    auto channels = std::make_shared<QVariantList>();
    auto index = std::make_shared<int>(0);
    auto processNext = std::make_shared<std::function<void()>>();

    *processNext = [this, definitions, channels, index, processNext, callback]() {
        if (*index >= definitions.size()) {
            callback(*channels);
            return;
        }

        const QVariantMap definition = definitions.at(*index).toMap();
        const QString title = definition.value(QStringLiteral("title"),
                                               definition.value(QStringLiteral("name"))).toString().trimmed();
        const QVariantList savedItems = definition.value(QStringLiteral("items")).toList();
        QVariantList movies;
        QVariantList shows;
        QVariantMap selectedEpisodeGroups;
        for (const QVariant &value : savedItems) {
            QVariantMap item = value.toMap();
            QString ratingKey = item.value(QStringLiteral("ratingKey")).toString().trimmed();
            if (ratingKey.isEmpty())
                ratingKey = item.value(QStringLiteral("rating_key")).toString().trimmed();
            QString type = item.value(QStringLiteral("type"),
                                      item.value(QStringLiteral("kind"))).toString().trimmed().toLower();
            if (ratingKey.isEmpty())
                continue;
            if (type == QStringLiteral("series") || type == QStringLiteral("tv") ||
                type == QStringLiteral("tv_show")) {
                type = QStringLiteral("show");
            }
            item[QStringLiteral("ratingKey")] = ratingKey;
            item[QStringLiteral("type")] = type;
            item[QStringLiteral("title")] = item.value(QStringLiteral("title")).toString().toUpper();
            if (isVodMovieType(type)) {
                movies.append(vodProgramFromItem(item));
            } else if (isVodShowType(type)) {
                shows.append(vodProgramFromItem(item));
            } else if (isVodEpisodeType(type)) {
                QString groupTitle = item.value(QStringLiteral("grandparentTitle")).toString().trimmed();
                if (groupTitle.isEmpty())
                    groupTitle = title;
                if (groupTitle.isEmpty())
                    groupTitle = QStringLiteral("SELECTED EPISODES");
                groupTitle = groupTitle.toUpper();
                QVariantList episodes = selectedEpisodeGroups.value(groupTitle).toList();
                episodes.append(vodProgramFromItem(item));
                selectedEpisodeGroups[groupTitle] = episodes;
            }
        }

        const QString commercialCategory = definition.value(QStringLiteral("commercialCategory")).toString().trimmed();
        QVariantList selectedEpisodeSeries;
        for (auto it = selectedEpisodeGroups.constBegin(); it != selectedEpisodeGroups.constEnd(); ++it) {
            QVariantList episodes = it.value().toList();
            sortEpisodeList(&episodes);
            if (!episodes.isEmpty()) {
                selectedEpisodeSeries.append(QVariantMap{
                    {QStringLiteral("type"), QStringLiteral("series")},
                    {QStringLiteral("title"), it.key()},
                    {QStringLiteral("episodes"), episodes}
                });
            }
        }

        const auto finishOne = [title, movies, selectedEpisodeSeries, commercialCategory, channels, index, processNext](const QVariantList &showGroups) {
            QVariantList programs = movies;
            for (const QVariant &group : selectedEpisodeSeries)
                programs.append(group);
            for (const QVariant &group : showGroups)
                programs.append(group);

            if (!title.isEmpty() && !programs.isEmpty())
                channels->append(vodChannel(title, QStringLiteral("custom"), programs, commercialCategory));

            ++(*index);
            (*processNext)();
        };

        if (shows.isEmpty()) {
            finishOne(QVariantList{});
            return;
        }

        fetchVodTvShowGroups(shows, finishOne);
    };

    (*processNext)();
}

void EmbyJellyfinBackend::fetchVodTvShowGroups(
    const QVariantList &shows,
    std::function<void(QVariantList)> callback) {
    auto groups = std::make_shared<QVariantList>();
    auto index = std::make_shared<int>(0);
    auto processNext = std::make_shared<std::function<void()>>();

    *processNext = [this, shows, groups, index, processNext, callback]() {
        if (*index >= shows.size()) {
            callback(*groups);
            return;
        }

        const QVariantMap show = shows.at(*index).toMap();
        const QString seriesId = show.value(QStringLiteral("ratingKey")).toString();
        if (seriesId.isEmpty()) {
            ++(*index);
            (*processNext)();
            return;
        }

        const auto finishEpisodes = [show, seriesId, groups, index, processNext]
                                    (QVariantList episodes) {
            QVariantList playableEpisodes;
            for (const QVariant &value : episodes) {
                QVariantMap episode = value.toMap();
                if (episode.value(QStringLiteral("ratingKey")).toString().isEmpty())
                    continue;
                if (!isVodEpisodeType(episode.value(QStringLiteral("type")).toString()))
                    continue;
                playableEpisodes.append(vodProgramFromItem(episode));
            }
            sortEpisodeList(&playableEpisodes);

            if (!playableEpisodes.isEmpty()) {
                groups->append(QVariantMap{
                    {QStringLiteral("type"), QStringLiteral("series")},
                    {QStringLiteral("title"), show.value(QStringLiteral("title")).toString()},
                    {QStringLiteral("seriesKey"), seriesId},
                    {QStringLiteral("year"), show.value(QStringLiteral("year"))},
                    {QStringLiteral("genres"), show.value(QStringLiteral("genres"))},
                    {QStringLiteral("episodes"), playableEpisodes}
                });
            }

            ++(*index);
            (*processNext)();
        };

        if (mediaProvider() == kProviderPlex) {
            fetchPlexEpisodesForSeries(seriesId, finishEpisodes);
        } else {
            fetchEpisodesForSeries(seriesId, [this, finishEpisodes](QJsonArray episodes) {
                finishEpisodes(formatItems(episodes));
            });
        }
    };

    (*processNext)();
}

void EmbyJellyfinBackend::fetchPlexEpisodesForSeries(
    const QString &seriesId,
    std::function<void(QVariantList)> callback) {
    auto *reply = plexServerGet(plexApiUrl(
        QStringLiteral("/library/metadata/%1/allLeaves").arg(seriesId)));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, callback]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            callback(QVariantList{});
            return;
        }

        QVariantList episodes = parsePlexItems(reply->readAll());
        sortEpisodeList(&episodes);
        callback(episodes);
    });
}

void EmbyJellyfinBackend::prepare_vod_tv_stream(const QString &requestId,
                                                const QVariantMap &item) {
    const QString ratingKey = item.value(QStringLiteral("ratingKey")).toString();
    if (requestId.isEmpty() || ratingKey.isEmpty()) {
        emit vodTvStreamFailed(requestId,
                               QStringLiteral("VOD TV PLAYBACK FAILED: EMPTY ITEM"));
        return;
    }

    if (mediaProvider() == kProviderPlex) {
        auto *reply = plexServerGet(plexApiUrl(
            QStringLiteral("/library/metadata/%1").arg(ratingKey)));
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, requestId, item]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                emit vodTvStreamFailed(requestId,
                                       QStringLiteral("LOAD PLEX ITEM FAILED: ") +
                                       reply->errorString());
                return;
            }
            prepareVodTvDetailStream(requestId, item, parsePlexDetail(reply->readAll()));
        });
        return;
    }

    QUrl url = apiUrl(QStringLiteral("/Users/") + userId() +
                      QStringLiteral("/Items/") + ratingKey);
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("Fields"),
                   QStringLiteral("MediaSources,MediaStreams,Overview,Genres,ParentId,UserData"));
    url.setQuery(q);

    auto *reply = apiGet(url);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, requestId, item]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit vodTvStreamFailed(requestId,
                                   QStringLiteral("LOAD MEDIA ITEM FAILED: ") +
                                   reply->errorString());
            return;
        }
        prepareVodTvDetailStream(
            requestId,
            item,
            buildItemDetail(QJsonDocument::fromJson(reply->readAll()).object()));
    });
}

void EmbyJellyfinBackend::prepareVodTvDetailStream(const QString &requestId,
                                                   const QVariantMap &item,
                                                   const QVariantMap &detail) {
    const QString ratingKey = detail.value(QStringLiteral("ratingKey")).toString();
    const QString partKey = detail.value(QStringLiteral("partKey")).toString();
    if (ratingKey.isEmpty() || partKey.isEmpty()) {
        emit vodTvStreamFailed(requestId,
                               QStringLiteral("VOD TV PLAYBACK FAILED: NO PLAYABLE STREAM"));
        return;
    }

    QVariantMap playbackItem = item;
    playbackItem[QStringLiteral("title")] =
        detail.value(QStringLiteral("title"),
                     item.value(QStringLiteral("title")));
    playbackItem[QStringLiteral("type")] =
        detail.value(QStringLiteral("type"),
                     item.value(QStringLiteral("type")));
    if (detail.contains(QStringLiteral("grandparentTitle")))
        playbackItem[QStringLiteral("grandparentTitle")] =
            detail.value(QStringLiteral("grandparentTitle"));

    const QString sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString audioId = detail.value(QStringLiteral("selectedAudioId")).toString();

    if (mediaProvider() == kProviderPlex) {
        plexRequestTranscodeUrl(ratingKey, partKey, sessionId, audioId, {}, 0,
                                [this, requestId, playbackItem]
                                (const QString &url,
                                 const QString &httpHeaderFields) {
            emit vodTvStreamReady(requestId, playbackItem, url, httpHeaderFields);
        }, [this, requestId](const QString &message) {
            emit vodTvStreamFailed(requestId, message);
        });
        return;
    }

    const bool forceTranscode =
        detail.value(QStringLiteral("forceTranscode"), true).toBool();
    const QJsonObject payload = playbackInfoPayload(partKey, audioId, {}, 0,
                                                    forceTranscode);
    auto *reply = apiPostJson(apiUrl(QStringLiteral("/Items/") + ratingKey +
                                     QStringLiteral("/PlaybackInfo")),
                              QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, requestId, playbackItem, ratingKey, partKey, sessionId,
             audioId, forceTranscode]() {
        reply->deleteLater();
        const QByteArray bytes = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            QString message = QStringLiteral("PLAYBACK INFO FAILED: ") +
                              reply->errorString();
            const QString body = abbreviatedNetworkBody(bytes);
            if (!body.isEmpty())
                message += QStringLiteral(" - ") + body;
            emit vodTvStreamFailed(requestId, message);
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            emit vodTvStreamFailed(
                requestId,
                QStringLiteral("PLAYBACK INFO FAILED: INVALID SERVER RESPONSE"));
            return;
        }

        const QJsonObject info = doc.object();
        const QString errorCode = info.value(QStringLiteral("ErrorCode")).toString();
        if (!errorCode.isEmpty()) {
            emit vodTvStreamFailed(requestId,
                                   QStringLiteral("PLAYBACK INFO FAILED: ") + errorCode);
            return;
        }

        QJsonObject mediaSource;
        const QString url = playbackUrlFromInfo(info, ratingKey, partKey, sessionId,
                                                audioId, {}, forceTranscode,
                                                &mediaSource);
        if (url.isEmpty()) {
            emit vodTvStreamFailed(
                requestId,
                QStringLiteral("PLAYBACK INFO FAILED: NO PLAYABLE STREAM"));
            return;
        }

        emit vodTvStreamReady(requestId, playbackItem, url,
                              httpHeaderFieldsFor(mediaSource));
    });
}

void EmbyJellyfinBackend::getLibraries() {
    if (get_auth_state() != "authed") {
        emit dynamicOptionsReady("libraries", QVariantList{});
        return;
    }

    if (mediaProvider() == kProviderPlex) {
        auto *reply = plexServerGet(plexApiUrl("/library/sections"));
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            reply->deleteLater();
            QVariantList opts;
            if (reply->error() == QNetworkReply::NoError) {
                const QVariantList libraries = parsePlexLibraries(reply->readAll(), false);
                for (const auto &v : libraries) {
                    const QVariantMap lib = v.toMap();
                    opts.append(QVariantMap{{"id", lib["sectionId"].toString()},
                                            {"label", lib["title"].toString()}});
                }
            }
            emit dynamicOptionsReady("libraries", opts);
        });
        return;
    }

    auto *reply = apiGet(apiUrl("/Users/" + userId() + "/Views"));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        QVariantList opts;
        if (reply->error() == QNetworkReply::NoError) {
            QJsonArray views = QJsonDocument::fromJson(reply->readAll()).object()["Items"].toArray();
            static const QSet<QString> kVideoCollections = {
                "movies", "tvshows", "mixed", "homevideos", "boxsets"
            };
            for (const auto &v : views) {
                QJsonObject view = v.toObject();
                QString id = view["Id"].toString();
                QString collectionType = view["CollectionType"].toString();
                if (id.isEmpty()) continue;
                if (!collectionType.isEmpty() && !kVideoCollections.contains(collectionType))
                    continue;
                opts.append(QVariantMap{{"id", id},
                                        {"label", view["Name"].toString().toUpper()}});
            }
        }
        emit dynamicOptionsReady("libraries", opts);
    });
}

void EmbyJellyfinBackend::getVideoQualities() {
    emit dynamicOptionsReady("video_quality", QVariantList{
        QVariantMap{{"id","auto"}, {"label","AUTO DISPLAY"}},
        QVariantMap{{"id","2160p"}, {"label","TRANSCODE 4K"}},
        QVariantMap{{"id","1440p"}, {"label","TRANSCODE 1440P"}},
        QVariantMap{{"id","1080p"}, {"label","TRANSCODE 1080P"}},
        QVariantMap{{"id","720p"}, {"label","TRANSCODE 720P"}},
        QVariantMap{{"id","480p"}, {"label","TRANSCODE 480P"}},
    });
}

void EmbyJellyfinBackend::get_resume_playback_options() {
    emit dynamicOptionsReady("resume_playback", QVariantList{
        QVariantMap{{"id","ask"}, {"label","ASK"}},
        QVariantMap{{"id","always"}, {"label","ALWAYS"}},
        QVariantMap{{"id","never"}, {"label","NEVER"}},
    });
}
