#pragma once

#include <QObject>
#include <QProcess>
#include <QVariantList>
#include <QVariantMap>
#include <QSet>
#include <QStringList>

#ifdef Q_OS_LINUX
#include <xf86drm.h>
#include <xf86drmMode.h>

struct RetroDrmSavedState {
    uint32_t crtcId      = 0;
    uint32_t connectorId = 0;
    uint32_t fbId        = 0;
    int      x           = 0;
    int      y           = 0;
    drmModeModeInfo mode = {};
    bool     valid       = false;
};
#endif

class RetroBackend : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)

public:
    explicit RetroBackend(const QString &appRoot, const QString &dataRoot,
                          QObject *parent = nullptr);
    ~RetroBackend() override;

    bool isRunning() const;

    Q_INVOKABLE QString get_auth_state();
    Q_INVOKABLE QVariantMap get_setup_status();
    Q_INVOKABLE void mount_retronas(const QString &host,
                                    const QString &share,
                                    const QString &remotePath,
                                    const QString &username,
                                    const QString &password);
    Q_INVOKABLE void load_systems();
    Q_INVOKABLE void load_games(const QString &systemId);
    Q_INVOKABLE void launch_game(const QString &systemId, const QString &path);
    Q_INVOKABLE void stop_game();
    Q_INVOKABLE void get_retro_system_options();
    Q_INVOKABLE void refresh_game_cache();
    Q_INVOKABLE void load_core_install_status_options();
    Q_INVOKABLE void install_game_cores();
    Q_INVOKABLE QVariantList api_search_games(const QString &query, int limit);

signals:
    void authStateChanged();
    void dynamicOptionsReady(const QString &key, const QVariant &options);
    void mountFinished(bool ok, const QString &message);
    void systemsLoaded(const QVariantList &systems);
    void gamesLoaded(const QVariantList &games);
    void gameStarted(const QString &title);
    void gameFinished();
    void errorOccurred(const QString &message);
    void runningChanged(bool running);

public slots:
    void onSettingChanged(const QString &moduleId, const QString &key, const QVariant &value);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onCoreInstallFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    struct SystemDef {
        QString id;
        QString label;
        QStringList folders;
        QStringList extensions;
        QStringList coreNames;
        QString corePackage;
    };

    QString setting(const QString &key, const QString &fallback = QString()) const;
    QVariantMap moduleConfig() const;
    QString mountPoint() const;
    QString gamesRoot() const;
    QString retroarchPath() const;
    QString rclonePath() const;
    QString rcloneConfigPath() const;
    QString desktopRetroNasCacheMarkerPath() const;
    QString desktopRetroNasDownloadRoot() const;
    QString desktopRetroNasRemoteRoot() const;
    QString desktopRetroNasVirtualPath(const QString &relativePath) const;
    QString desktopRetroNasRelativePath(const QString &virtualPath) const;
    bool desktopRetroNasCacheMode() const;
    void setDesktopRetroNasCacheMode(bool enabled) const;
    bool buildDesktopRetroNasCatalog(QString *errorOut) const;
    void startDesktopRetroNasDownload(const QString &systemId,
                                      const QString &relativePath);
    void startNextDesktopRetroNasDownload();
    void finishDesktopRetroNasDownload(bool ok, const QString &error = QString());
    void queueDesktopRetroNasCompanions(const QString &relativePath,
                                        const QString &localPath);
    void pruneDesktopRetroNasDownloadCache() const;
    void launchLocalGame(const QString &systemId, const QString &path);
    bool unmountDesktopRetroNas() const;
    QString systemDirectory(const SystemDef &def) const;
    QString corePath(const SystemDef &def,
                     const QString &contentPath = QString()) const;
    QVariantList coreInstallStatusOptions() const;
    void emitCoreInstallStatus();
    QVariantList availableSystems() const;
    QVariantList gamesForSystem(const SystemDef &def) const;
    QString gameCachePath() const;
    QString gameCacheRootKey() const;
    QVariantMap loadGameCache() const;
    QVariantMap buildGameCache() const;
    bool saveGameCache(const QVariantMap &cache) const;
    bool gameCacheIsCurrent(const QVariantMap &cache) const;
    QVariantMap ensureGameCache() const;
    QVariantList cachedSystems() const;
    QVariantList cachedGamesForSystem(const QString &systemId) const;
    void clearGameCache() const;
    const SystemDef *systemById(const QString &systemId) const;
    QList<SystemDef> systemDefinitions() const;
    int gameCount(const SystemDef &def, const QString &dirPath, int limit = 9999) const;
    void seedBundledRetroarchSystemFiles(const QString &destinationRoot) const;
    QString writeRetroarchConfig();
    QString credentialsFilePath() const;
    bool writeCredentialsFile(const QString &username, const QString &password,
                              QString *pathOut, QString *errorOut) const;
    void prepareHeadlessLaunch();
    void restoreHeadlessDisplay();
    bool detectHeadlessMode() const;
    bool hasPiHeadphonesAudioDevice() const;
    int getActiveVt() const;
    int findFreeVt() const;
    int findQtDrmFd() const;
    void switchToVt(int vt);
#ifdef Q_OS_LINUX
    void saveDrmCrtcState(int fd);
    void restoreDrmCrtcState(int fd);
#endif

    QString m_appRoot;
    QString m_dataRoot;
    QProcess *m_process = nullptr;
    QProcess *m_coreInstallProcess = nullptr;
    QProcess *m_retroNasTransferProcess = nullptr;
    QStringList m_retroNasTransferQueue;
    QSet<QString> m_retroNasQueuedPaths;
    QString m_pendingRemoteSystemId;
    QString m_pendingRemotePrimaryPath;
    QString m_activeRemoteTransferPath;
    QString m_currentTitle;
    QString m_coreInstallStatus;
    bool m_headlessMode = false;
    int m_previousVt = -1;
    int m_qtDrmFd = -1;
    bool m_qtDrmMasterDropped = false;
#ifdef Q_OS_LINUX
    RetroDrmSavedState m_savedDrm = {};
#endif
};
