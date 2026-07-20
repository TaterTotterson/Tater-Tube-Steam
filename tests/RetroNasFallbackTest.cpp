#include "modules/retro/RetroBackend.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class RetroNasFallbackTest : public QObject {
    Q_OBJECT

private:
    static void writeFile(const QString &path, const QByteArray &contents,
                          QFile::Permissions permissions = QFile::ReadOwner
                              | QFile::WriteOwner)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
                 qPrintable(file.errorString()));
        QCOMPARE(file.write(contents), contents.size());
        file.close();
        QVERIFY(QFile::setPermissions(path, permissions));
    }

    static QVariantMap systemById(const QVariantList &systems,
                                  const QString &systemId)
    {
        for (const QVariant &system : systems) {
            const QVariantMap values = system.toMap();
            if (values.value("id").toString() == systemId)
                return values;
        }
        return {};
    }

private slots:
    void catalogsRemoteGamesWithoutFuse()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString appRoot = QDir(temporary.path()).absoluteFilePath("app");
        const QString dataRoot = QDir(temporary.path()).absoluteFilePath("data");

        const QJsonObject retroConfig{
            {"retronas_host", "retronas.local"},
            {"retronas_share", "mister"},
            {"retronas_path", "games"},
            {"retronas_username", "guest"}
        };
        const QJsonObject config{
            {"modules", QJsonObject{{"com.240mp.retro", retroConfig}}}
        };
        writeFile(QDir(dataRoot).absoluteFilePath("config.json"),
                  QJsonDocument(config).toJson());

        writeFile(
            QDir(appRoot).absoluteFilePath("vendor/rclone/bin/rclone"),
            QByteArrayLiteral(
                "#!/bin/sh\n"
                "case \"$1\" in\n"
                "  obscure) printf 'protected-password\\n' ;;\n"
                "  lsjson) printf '[{\"Path\":\"NES/Test Game.nes\"}]\\n' ;;\n"
                "  mount) printf 'FUSE unavailable in test\\n' >&2; exit 1 ;;\n"
                "  *) printf 'unexpected rclone command\\n' >&2; exit 2 ;;\n"
                "esac\n"),
            QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        writeFile(
            QDir(appRoot).absoluteFilePath(
                "vendor/retroarch/cores/nestopia_libretro.so"),
            QByteArrayLiteral("test core"));

        RetroBackend backend(appRoot, dataRoot);
        QSignalSpy mountSpy(&backend, &RetroBackend::mountFinished);
        backend.mount_retronas("retronas.local", "mister", "games", "guest", "");

        QCOMPARE(mountSpy.size(), 1);
        const QList<QVariant> mountResult = mountSpy.takeFirst();
        QVERIFY(mountResult.at(0).toBool());
        QVERIFY(mountResult.at(1).toString().contains("DOWNLOAD CACHE"));

        QSignalSpy systemsSpy(&backend, &RetroBackend::systemsLoaded);
        backend.load_systems();
        QCOMPARE(systemsSpy.size(), 1);
        const QVariantList systems = systemsSpy.takeFirst().at(0).toList();
        QCOMPARE(systems.size(), 1);
        QCOMPARE(systems.first().toMap().value("id").toString(), QString("nes"));
        QCOMPARE(systems.first().toMap().value("gameCount").toInt(), 1);

        QSignalSpy gamesSpy(&backend, &RetroBackend::gamesLoaded);
        backend.load_games("nes");
        QCOMPARE(gamesSpy.size(), 1);
        const QVariantList games = gamesSpy.takeFirst().at(0).toList();
        QCOMPARE(games.size(), 1);
        QCOMPARE(games.first().toMap().value("title").toString(),
                 QString("Test Game"));
        QVERIFY(games.first().toMap().value("path").toString().startsWith(
            "retronas-cache:/"));
    }

    void prefersSteamReplacementCores()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString appRoot = QDir(temporary.path()).absoluteFilePath("app");
        const QString dataRoot = QDir(temporary.path()).absoluteFilePath("data");
        const QString gamesRoot = QDir(temporary.path()).absoluteFilePath("games");

        const QJsonObject config{
            {"modules",
             QJsonObject{{"com.240mp.retro",
                          QJsonObject{{"local_path", gamesRoot}}}}}
        };
        writeFile(QDir(dataRoot).absoluteFilePath("config.json"),
                  QJsonDocument(config).toJson());

        writeFile(QDir(gamesRoot).absoluteFilePath("Genesis/Test.md"), "game");
        writeFile(QDir(gamesRoot).absoluteFilePath("SNES/Test.sfc"), "game");
        writeFile(QDir(gamesRoot).absoluteFilePath("SegaCD/Test.cue"), "game");

        const QString coreRoot = QDir(appRoot).absoluteFilePath(
            "vendor/retroarch/cores");
        writeFile(QDir(coreRoot).absoluteFilePath("blastem_libretro.so"), "core");
        writeFile(QDir(coreRoot).absoluteFilePath("genesis_plus_gx_libretro.so"),
                  "core");
        writeFile(QDir(coreRoot).absoluteFilePath("bsnes_libretro.so"), "core");
        writeFile(QDir(coreRoot).absoluteFilePath("snes9x_libretro.so"), "core");

        RetroBackend backend(appRoot, dataRoot);
        QSignalSpy systemsSpy(&backend, &RetroBackend::systemsLoaded);
        backend.load_systems();
        QCOMPARE(systemsSpy.size(), 1);
        const QVariantList systems = systemsSpy.takeFirst().at(0).toList();

        const QVariantMap genesis = systemById(systems, "genesis");
        QVERIFY(!genesis.isEmpty());
        QVERIFY(genesis.value("core").toString().endsWith(
            "/blastem_libretro.so"));
        QCOMPARE(genesis.value("corePackage").toString(), QString("blastem"));

        const QVariantMap snes = systemById(systems, "snes");
        QVERIFY(!snes.isEmpty());
        QVERIFY(snes.value("core").toString().endsWith("/bsnes_libretro.so"));
        QCOMPARE(snes.value("corePackage").toString(), QString("bsnes"));

        // A locally installed legacy core can still service Sega CD. The
        // bundled BlastEm replacement must not claim that unsupported system.
        const QVariantMap segaCd = systemById(systems, "segacd");
        QVERIFY(!segaCd.isEmpty());
        QVERIFY(segaCd.value("core").toString().endsWith(
            "/genesis_plus_gx_libretro.so"));

        QVERIFY(QFile::remove(
            QDir(coreRoot).absoluteFilePath("genesis_plus_gx_libretro.so")));
        const QString cleanDataRoot = QDir(temporary.path()).absoluteFilePath(
            "clean-data");
        writeFile(QDir(cleanDataRoot).absoluteFilePath("config.json"),
                  QJsonDocument(config).toJson());
        RetroBackend cleanBackend(appRoot, cleanDataRoot);
        QSignalSpy cleanSystemsSpy(&cleanBackend,
                                   &RetroBackend::systemsLoaded);
        cleanBackend.load_systems();
        QCOMPARE(cleanSystemsSpy.size(), 1);
        const QVariantList cleanSystems =
            cleanSystemsSpy.takeFirst().at(0).toList();
        QVERIFY(systemById(cleanSystems, "segacd").isEmpty());
    }

    void selectsNeoGeoCoreByContentFormat()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString appRoot = QDir(temporary.path()).absoluteFilePath("app");
        const QString dataRoot = QDir(temporary.path()).absoluteFilePath("data");
        const QString gamesRoot = QDir(temporary.path()).absoluteFilePath("games");
        const QString neoRoot = QDir(gamesRoot).absoluteFilePath("NeoGeo");

        const QJsonObject config{
            {"modules",
             QJsonObject{{"com.240mp.retro",
                          QJsonObject{{"local_path", gamesRoot}}}}}
        };
        writeFile(QDir(dataRoot).absoluteFilePath("config.json"),
                  QJsonDocument(config).toJson());
        writeFile(QDir(neoRoot).absoluteFilePath("Cartridge.neo"), "game");
        writeFile(QDir(neoRoot).absoluteFilePath("Arcade.zip"), "game");

        const QString vendorRoot = QDir(appRoot).absoluteFilePath(
            "vendor/retroarch");
        writeFile(QDir(vendorRoot).absoluteFilePath("cores/geolith_libretro.so"),
                  "core");
        writeFile(QDir(vendorRoot).absoluteFilePath("cores/mame_libretro.so"),
                  "core");
        writeFile(QDir(vendorRoot).absoluteFilePath(
                      "system/open-support/license.txt"),
                  "redistributable support file");
        writeFile(
            QDir(vendorRoot).absoluteFilePath("bin/retroarch"),
            QByteArrayLiteral(
                "#!/bin/sh\n"
                "printf '%s\\n' \"$@\" > \"$(dirname \"$0\")/args.txt\"\n"),
            QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

        const QString argsPath = QDir(vendorRoot).absoluteFilePath("bin/args.txt");
        const auto launchAndCheckCore = [&](const QString &gamePath,
                                             const QString &coreName) {
            QFile::remove(argsPath);
            RetroBackend backend(appRoot, dataRoot);
            QSignalSpy finishedSpy(&backend, &RetroBackend::gameFinished);
            QSignalSpy errorSpy(&backend, &RetroBackend::errorOccurred);
            backend.launch_game("neogeo", gamePath);
            QTRY_COMPARE(finishedSpy.size(), 1);
            QCOMPARE(errorSpy.size(), 0);

            QFile argsFile(argsPath);
            QVERIFY(argsFile.open(QIODevice::ReadOnly));
            const QStringList args = QString::fromUtf8(argsFile.readAll())
                                         .split('\n', Qt::SkipEmptyParts);
            const int coreOption = args.indexOf("-L");
            QVERIFY(coreOption >= 0);
            QVERIFY(coreOption + 1 < args.size());
            QVERIFY(args.at(coreOption + 1).endsWith("/" + coreName));
        };

        launchAndCheckCore(QDir(neoRoot).absoluteFilePath("Cartridge.neo"),
                           "geolith_libretro.so");
        QVERIFY(QFileInfo::exists(QDir(dataRoot).absoluteFilePath(
            "retroarch/system/open-support/license.txt")));
        launchAndCheckCore(QDir(neoRoot).absoluteFilePath("Arcade.zip"),
                           "mame_libretro.so");
    }
};

QTEST_MAIN(RetroNasFallbackTest)

#include "RetroNasFallbackTest.moc"
