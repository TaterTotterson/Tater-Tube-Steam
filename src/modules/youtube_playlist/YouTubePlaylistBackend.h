#pragma once

#include <QJsonObject>
#include <QObject>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QString>

class QProcess;

class YouTubePlaylistBackend : public QObject {
    Q_OBJECT

public:
    explicit YouTubePlaylistBackend(const QString &appRoot, const QString &dataRoot,
                                    QObject *parent = nullptr);

    Q_INVOKABLE QString get_auth_state();
    Q_INVOKABLE QString get_saved_playlist_input() const;
    Q_INVOKABLE QVariantList get_saved_playlists() const;
    Q_INVOKABLE QString normalize_playlist_input(const QString &input) const;
    Q_INVOKABLE QVariantList get_commercial_categories() const;
    Q_INVOKABLE QVariantList get_commercial_videos_for_setting(const QString &settingKey) const;
    Q_INVOKABLE QVariantList get_commercial_videos_for_category(const QString &categoryId) const;
    Q_INVOKABLE QVariantMap resolve_playlist_info(const QString &input) const;
    Q_INVOKABLE QString ytdl_format_for_quality(const QString &quality) const;
    Q_INVOKABLE void resolve_video_stream(const QString &requestId, const QString &url,
                                          const QString &quality);
    Q_INVOKABLE void cancel_video_stream_resolve();
    Q_INVOKABLE void load_playlist(const QString &input);
    Q_INVOKABLE void refresh_playlist_cache();
    Q_INVOKABLE void refresh_playlist(const QString &input);
    Q_INVOKABLE void load_playlist_remove_options();
    Q_INVOKABLE void load_playlist_rename_options();
    Q_INVOKABLE void load_public_access_commercial_category_options();
    Q_INVOKABLE void load_vod_commercial_category_options();
    Q_INVOKABLE void remove_selected_playlist();
    Q_INVOKABLE void rename_selected_playlist();

signals:
    void playlistLoaded(const QString &title, const QVariantList &items);
    void errorOccurred(const QString &message);
    void authStateChanged();
    void dynamicOptionsReady(const QString &key, const QVariant &options);
    void videoStreamResolved(const QString &requestId, const QVariantMap &result);

public slots:
    void onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value);

private:
    QJsonObject loadConfig() const;
    bool saveConfig(const QJsonObject &config) const;
    QVariantMap moduleConfig() const;
    QString setting(const QString &key, const QString &fallback = QString()) const;
    QString ytDlpPath() const;
    QVariantList playlistRemovalOptions() const;
    QString playlistCachePath(const QString &playlistUrl) const;
    QVariantMap loadPlaylistCache(const QString &playlistUrl) const;
    bool savePlaylistCache(const QString &playlistUrl, const QString &title,
                           const QVariantList &items) const;
    void clearPlaylistCache() const;
    void clearPlaylistCache(const QString &playlistUrl) const;
    void fetchPlaylist(const QString &input, bool forceRefresh);
    QVariantMap inspectPlaylist(const QString &playlistUrl, int limit, QString *errorOut) const;
    QString directStreamFormatForQuality(const QString &quality) const;

    QString m_appRoot;
    QString m_dataRoot;
    QProcess *m_streamResolver = nullptr;
    QString m_streamResolveRequestId;
};
