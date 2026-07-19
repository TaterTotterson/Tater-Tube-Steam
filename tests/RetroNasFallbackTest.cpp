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
};

QTEST_MAIN(RetroNasFallbackTest)

#include "RetroNasFallbackTest.moc"
