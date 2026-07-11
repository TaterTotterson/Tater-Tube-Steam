#include "CommercialLibrary.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

namespace {
QString cleanTitle(QString title, const QString &fallback)
{
    title = title.trimmed();
    title.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return title.isEmpty() ? fallback : title;
}
}

CommercialLibrary::CommercialLibrary(const QString &dataRoot)
    : m_dataRoot(dataRoot)
{
}

QString CommercialLibrary::rootPath() const
{
    return QDir(m_dataRoot).absoluteFilePath(QStringLiteral("commercials"));
}

bool CommercialLibrary::isVideoFile(const QFileInfo &fileInfo)
{
    if (fileInfo.exists() && !fileInfo.isFile())
        return false;

    const QString suffix = fileInfo.suffix().toLower();
    static const QSet<QString> videoSuffixes{
        QStringLiteral("mp4"), QStringLiteral("m4v"), QStringLiteral("mkv"),
        QStringLiteral("mov"), QStringLiteral("webm"), QStringLiteral("avi"),
        QStringLiteral("mpg"), QStringLiteral("mpeg"), QStringLiteral("ts")
    };
    return videoSuffixes.contains(suffix);
}

QString CommercialLibrary::titleFromFile(const QFileInfo &fileInfo, int index)
{
    QString title = fileInfo.completeBaseName().trimmed();
    title.replace(QRegularExpression(QStringLiteral("[._-]+")), QStringLiteral(" "));
    title.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return cleanTitle(title, QStringLiteral("COMMERCIAL %1").arg(index + 1));
}

int CommercialLibrary::videoCount(const QString &categoryPath)
{
    const QFileInfoList files = QDir(categoryPath).entryInfoList(
        QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    int count = 0;
    for (const QFileInfo &fileInfo : files) {
        if (isVideoFile(fileInfo))
            ++count;
    }
    return count;
}

QVariantList CommercialLibrary::categoryOptions() const
{
    QVariantList options;
    const QDir root(rootPath());
    const QFileInfoList categories = root.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable, QDir::Name | QDir::IgnoreCase);

    for (const QFileInfo &category : categories) {
        const QString id = category.fileName();
        if (id.trimmed().isEmpty())
            continue;
        const int count = videoCount(category.absoluteFilePath());
        options.append(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("label"), QStringLiteral("%1 (%2)").arg(id, QString::number(count))},
            {QStringLiteral("count"), count}
        });
    }
    return options;
}

QVariantList CommercialLibrary::videosForCategory(const QString &categoryId,
                                                  int startIndex) const
{
    QVariantList videos;
    const QString cleanCategory = QFileInfo(categoryId).fileName().trimmed();
    if (cleanCategory.isEmpty() || cleanCategory != categoryId.trimmed())
        return videos;

    const QDir categoryDir(QDir(rootPath()).absoluteFilePath(cleanCategory));
    if (!categoryDir.exists())
        return videos;

    const QFileInfoList files = categoryDir.entryInfoList(
        QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &fileInfo : files) {
        if (!isVideoFile(fileInfo))
            continue;

        QVariantMap item;
        item[QStringLiteral("id")] = QStringLiteral("%1/%2").arg(cleanCategory, fileInfo.fileName());
        item[QStringLiteral("title")] = titleFromFile(fileInfo, startIndex + videos.size());
        item[QStringLiteral("url")] = QUrl::fromLocalFile(fileInfo.absoluteFilePath()).toString();
        item[QStringLiteral("category")] = cleanCategory;
        item[QStringLiteral("duration")] = 30;
        item[QStringLiteral("commercial")] = true;
        item[QStringLiteral("local")] = true;
        videos.append(item);
    }

    return videos;
}

QVariantList CommercialLibrary::videosForSelection(const QVariantMap &selection) const
{
    QVariantList videos;
    const QDir root(rootPath());
    const QFileInfoList categories = root.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable, QDir::Name | QDir::IgnoreCase);

    for (const QFileInfo &category : categories) {
        const QString categoryId = category.fileName();
        if (categoryId.trimmed().isEmpty())
            continue;

        const QVariant selectedValue = selection.value(categoryId, true);
        if (!selectedValue.toBool())
            continue;

        const QVariantList categoryVideos = videosForCategory(categoryId, videos.size());
        for (const QVariant &video : categoryVideos)
            videos.append(video);
    }

    return videos;
}

QVariantList CommercialLibrary::videosForSetting(const QVariantMap &moduleConfig,
                                                 const QString &settingKey) const
{
    return videosForSelection(moduleConfig.value(settingKey).toMap());
}

QJsonObject CommercialLibrary::setupLibrary() const
{
    QJsonArray categories;
    const QDir root(rootPath());
    const QFileInfoList categoryInfos = root.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &categoryInfo : categoryInfos) {
        QJsonArray videos;
        const QFileInfoList fileInfos = QDir(categoryInfo.absoluteFilePath()).entryInfoList(
            QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &fileInfo : fileInfos) {
            if (!isVideoFile(fileInfo))
                continue;
            videos.append(QJsonObject{
                {"name", fileInfo.fileName()},
                {"title", titleFromFile(fileInfo, videos.size())},
                {"size", QString::number(fileInfo.size())},
                {"modified", fileInfo.lastModified().toUTC().toString(Qt::ISODate)}
            });
        }

        categories.append(QJsonObject{
            {"id", categoryInfo.fileName()},
            {"name", categoryInfo.fileName()},
            {"count", videos.size()},
            {"videos", videos}
        });
    }

    return QJsonObject{{"ok", true}, {"categories", categories}};
}
