#include "EmbyJellyfinBackend.h"

#include <algorithm>
#include <QCollator>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFileDevice>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSet>
#include <QStringList>
#include <QSysInfo>
#include <QUrlQuery>
#include <QUuid>

static const QString kModuleId = QStringLiteral("com.240mp.emby_jellyfin");
static const QString kAuthFile = QStringLiteral("/emby_jellyfin_auth.json");
static const QString kOtaVideoQuality = QStringLiteral("480p");

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

    if (quality == "1080p") {
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
    while (url.endsWith('/')) url.chop(1);
    if (url.isEmpty()) return {};
    if (!url.startsWith("http://", Qt::CaseInsensitive) &&
        !url.startsWith("https://", Qt::CaseInsensitive)) {
        url = "http://" + url;
    }
    return url;
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

QString EmbyJellyfinBackend::serverUrl() const {
    return loadAuth()["server_url"].toString();
}

QString EmbyJellyfinBackend::accessToken() const {
    return loadAuth()["access_token"].toString();
}

QString EmbyJellyfinBackend::userId() const {
    return loadAuth()["user_id"].toString();
}

QString EmbyJellyfinBackend::videoQuality() const {
    QJsonObject cfg = loadConfig();
    return cfg["modules"].toObject()[kModuleId].toObject()["video_quality"].toString("auto");
}

QNetworkRequest EmbyJellyfinBackend::apiRequest(const QUrl &url,
                                                const QString &token) const {
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");

    QString auth = QStringLiteral(
        "MediaBrowser Client=\"240-MP\", Device=\"%1\", DeviceId=\"%2\", Version=\"%3\"")
        .arg(QSysInfo::machineHostName().isEmpty() ? QStringLiteral("240-MP")
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

qint64 EmbyJellyfinBackend::ticksToMs(const QJsonValue &ticks) {
    return static_cast<qint64>(ticks.toDouble() / 10000.0);
}

qint64 EmbyJellyfinBackend::msToTicks(int ms) {
    return static_cast<qint64>(ms) * 10000;
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

bool EmbyJellyfinBackend::codecNeedsTranscode(const QString &codec) {
    const QString c = codec.toLower();
    return c == "av1" || c == "av01";
}

QString EmbyJellyfinBackend::get_auth_state() {
    QJsonObject auth = loadAuth();
    return auth["server_url"].toString().isEmpty() ||
           auth["access_token"].toString().isEmpty() ||
           auth["user_id"].toString().isEmpty()
        ? QStringLiteral("none")
        : QStringLiteral("authed");
}

QString EmbyJellyfinBackend::get_active_user_name() {
    return loadAuth()["username"].toString();
}

QString EmbyJellyfinBackend::get_active_server_name() {
    QJsonObject auth = loadAuth();
    QString name = auth["server_name"].toString();
    return name.isEmpty() ? auth["server_url"].toString() : name;
}

QString EmbyJellyfinBackend::get_saved_server_url() {
    return loadAuth()["server_url"].toString();
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
            emit errorOccurred("SIGN IN FAILED: " + reply->errorString());
            return;
        }

        QJsonObject data = QJsonDocument::fromJson(bytes).object();
        QString token = data["AccessToken"].toString();
        QJsonObject user = data["User"].toObject();
        QString uid = user["Id"].toString();
        if (uid.isEmpty())
            uid = data["SessionInfo"].toObject()["UserId"].toString();

        if (token.isEmpty() || uid.isEmpty()) {
            emit errorOccurred("SIGN IN FAILED: EMPTY AUTH RESPONSE");
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

void EmbyJellyfinBackend::logout() {
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
    q.addQueryItem("Fields", "MediaSources,MediaStreams,Overview,Genres,ParentId,PrimaryImageAspectRatio,UserData,RecursiveItemCount,ChildCount");
    q.addQueryItem("ImageTypeLimit", "1");
    q.addQueryItem("EnableImages", "false");
    url.setQuery(q);
    return url;
}

QVariantMap EmbyJellyfinBackend::formatItem(const QJsonObject &item) const {
    const QJsonObject userData = item["UserData"].toObject();
    const QString type = itemType(item);
    const int duration = static_cast<int>(ticksToMs(item["RunTimeTicks"]));

    return QVariantMap{
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

void EmbyJellyfinBackend::load_collections(const QString &sectionId) {
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
    load_library_all(ratingKey);
}

void EmbyJellyfinBackend::load_playlists(const QString &sectionId) {
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
        {"Name", "240-MP"},
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
        if (effectiveQuality == "1080p") {
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
    base["forceTranscode"] = (videoQuality() != "auto") || codecNeedsTranscode(videoCodec);
    return base;
}

void EmbyJellyfinBackend::load_item_detail(const QString &ratingKey) {
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

void EmbyJellyfinBackend::build_stream_url(const QString &ratingKey,
                                           const QString &partKey,
                                           const QString &sessionId) {
    requestPlaybackInfo(ratingKey, partKey, sessionId, {}, {}, 0, false);
}

void EmbyJellyfinBackend::request_transcode(const QString &ratingKey,
                                            const QString &partKey,
                                            const QString &sessionId,
                                            const QString &audioId,
                                            const QString &subtitleId,
                                            int offsetMs) {
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

void EmbyJellyfinBackend::getLibraries() {
    if (get_auth_state() != "authed") {
        emit dynamicOptionsReady("libraries", QVariantList{});
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
        QVariantMap{{"id","auto"}, {"label","AUTO"}},
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
