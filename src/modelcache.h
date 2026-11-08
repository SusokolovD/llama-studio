#pragma once
#include "advisor.h"
#include <QJsonObject>
#include <QSet>
class ModelCache{
public:
    explicit ModelCache(const QString &path=defaultPath());
    static QString defaultPath();
    ModelInfo get(const QString &path,bool force=false,bool *hit=nullptr);
    QVector<ModelInfo> directory(const QString &path,bool recursive,bool *found=nullptr)const;
    QVector<ModelInfo> allModels()const;
    QStringList directories()const;
    static QString loadPath(const ModelInfo &info);
    void setRole(const QString &path,const QString &role);
    void setDirectory(const QString &path,bool recursive,const QVector<ModelInfo> &models);
    bool save(QString *error=nullptr);
private:
    QString filePath;
    QJsonObject data;
    QSet<QString> changedModels,changedDirectories,changedRoles;
};
ModelInfo cachedModelInfo(const QString &path,bool force=false,QString *cacheError=nullptr);
