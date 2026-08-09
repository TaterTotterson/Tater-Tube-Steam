#include "AppCore.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class SteamUpdaterCapabilityTest : public QObject {
    Q_OBJECT

private slots:
    void exposesBundledUpdater();
    void sanitizesDynamicLoaderEnvironment();
};

class EnvironmentGuard {
public:
    explicit EnvironmentGuard(const char *name)
        : m_name(name), m_wasSet(qEnvironmentVariableIsSet(name)), m_value(qgetenv(name))
    {
    }

    ~EnvironmentGuard()
    {
        if (m_wasSet)
            qputenv(m_name.constData(), m_value);
        else
            qunsetenv(m_name.constData());
    }

private:
    QByteArray m_name;
    bool m_wasSet;
    QByteArray m_value;
};

void SteamUpdaterCapabilityTest::exposesBundledUpdater()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString appRoot = temporary.filePath(QStringLiteral("app"));
    const QString dataRoot = temporary.filePath(QStringLiteral("data"));
    const QString scriptsRoot = QDir(appRoot).filePath(QStringLiteral("scripts"));
    QVERIFY(QDir().mkpath(scriptsRoot));
    QVERIFY(QDir().mkpath(dataRoot));

    const QString installerPath = QDir(scriptsRoot).filePath(
        QStringLiteral("install-steam-deck.sh"));
    QFile installer(installerPath);
    QVERIFY(installer.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray script("#!/usr/bin/env bash\nexit 0\n");
    QCOMPARE(installer.write(script), script.size());
    installer.close();
    QVERIFY(QFile::setPermissions(
        installerPath,
        QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));

    AppCore appCore(appRoot, dataRoot);
    QVERIFY(appCore.isSteamBuild());
    QVERIFY(appCore.platformCapabilities().value(QStringLiteral("selfUpdate")).toBool());
    QVERIFY(appCore.getUpdateInfo().value(QStringLiteral("canInstall")).toBool());
}

void SteamUpdaterCapabilityTest::sanitizesDynamicLoaderEnvironment()
{
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());

    const QString appRoot = temporary.filePath(QStringLiteral("app"));
    const QString dataRoot = temporary.filePath(QStringLiteral("data"));
    const QString scriptsRoot = QDir(appRoot).filePath(QStringLiteral("scripts"));
    QVERIFY(QDir().mkpath(scriptsRoot));
    QVERIFY(QDir().mkpath(dataRoot));

    const QString installerPath = QDir(scriptsRoot).filePath(
        QStringLiteral("install-steam-deck.sh"));
    QFile installer(installerPath);
    QVERIFY(installer.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray script(
        "#!/usr/bin/env bash\n"
        "set -eu\n"
        "if [ -n \"${LD_LIBRARY_PATH+x}\" ] || [ -n \"${LD_PRELOAD+x}\" ]; then\n"
        "  printf dirty > \"${TATER_TUBE_UPDATE_ENV_PROBE}\"\n"
        "  exit 42\n"
        "fi\n"
        "printf clean > \"${TATER_TUBE_UPDATE_ENV_PROBE}\"\n");
    QCOMPARE(installer.write(script), script.size());
    installer.close();
    QVERIFY(QFile::setPermissions(
        installerPath,
        QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));

    EnvironmentGuard homeGuard("HOME");
    EnvironmentGuard libraryPathGuard("LD_LIBRARY_PATH");
    EnvironmentGuard preloadGuard("LD_PRELOAD");
    EnvironmentGuard probeGuard("TATER_TUBE_UPDATE_ENV_PROBE");
    const QString probePath = temporary.filePath(QStringLiteral("environment-probe.txt"));
    qputenv("HOME", QFile::encodeName(temporary.path()));
    qputenv("LD_LIBRARY_PATH", QByteArrayLiteral("/broken/portable/lib"));
    qputenv("LD_PRELOAD", QByteArrayLiteral("/broken/steam-overlay.so"));
    qputenv("TATER_TUBE_UPDATE_ENV_PROBE", QFile::encodeName(probePath));

    AppCore appCore(appRoot, dataRoot);
    QSignalSpy finishedSpy(&appCore, &AppCore::updateInstallFinished);
    appCore.installUpdate();
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() >= 2, 5000);

    const QVariantMap result = finishedSpy.constLast().at(0).toMap();
    QCOMPARE(result.value(QStringLiteral("status")).toString(), QStringLiteral("installed"));
    QCOMPARE(result.value(QStringLiteral("restarting")).toBool(), false);

    QFile probe(probePath);
    QVERIFY(probe.open(QIODevice::ReadOnly));
    QCOMPARE(probe.readAll(), QByteArrayLiteral("clean"));
}

QTEST_GUILESS_MAIN(SteamUpdaterCapabilityTest)

#include "SteamUpdaterCapabilityTest.moc"
