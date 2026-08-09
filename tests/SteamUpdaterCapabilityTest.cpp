#include "AppCore.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class SteamUpdaterCapabilityTest : public QObject {
    Q_OBJECT

private slots:
    void exposesBundledUpdater();
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

QTEST_GUILESS_MAIN(SteamUpdaterCapabilityTest)

#include "SteamUpdaterCapabilityTest.moc"
