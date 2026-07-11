#pragma once

#include <QJsonObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class QFileInfo;

class CommercialLibrary {
public:
    explicit CommercialLibrary(const QString &dataRoot);

    QString rootPath() const;
    QVariantList categoryOptions() const;
    QVariantList videosForSetting(const QVariantMap &moduleConfig,
                                  const QString &settingKey) const;
    QVariantList videosForCategory(const QString &categoryId,
                                   int startIndex = 0) const;
    QVariantList videosForSelection(const QVariantMap &selection) const;
    QJsonObject setupLibrary() const;

    static bool isVideoFile(const QFileInfo &fileInfo);
    static QString titleFromFile(const QFileInfo &fileInfo, int index);
    static int videoCount(const QString &categoryPath);

private:
    QString m_dataRoot;
};
