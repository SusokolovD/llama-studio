#include "modelcache.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QRegularExpression>
namespace{
QString key(const QString &path){auto p=QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
    p=p.toCaseFolded();
#endif
    return p;
}
QString directoryKey(const QString &path,bool recursive){return key(path)+(recursive?"|recursive":"|flat");}
QJsonObject read(const QString &path){QFile file(path);if(!file.open(QIODevice::ReadOnly)||file.size()>64*1024*1024)return {};auto root=QJsonDocument::fromJson(file.readAll()).object();return root.value("schemaVersion").toInt()==1?root:QJsonObject{};}
QJsonObject infoJson(const ModelInfo &m){
    QJsonObject o{{"path",m.path},{"name",m.name},{"architecture",m.architecture},{"error",m.error},{"sizeLabel",m.sizeLabel},{"modelType",m.modelType},{"hasChatTemplate",m.hasChatTemplate}};
#define STORE(field) o[#field]=QString::number(m.field)
    STORE(fileType);STORE(shardIndex);STORE(fileBytes);STORE(trainingContext);STORE(layers);STORE(heads);STORE(kvHeads);STORE(embedding);STORE(shards);STORE(keyLength);STORE(valueLength);STORE(slidingWindow);
#undef STORE
    return o;
}
ModelInfo fromJson(const QJsonObject &o){ModelInfo m;m.path=o.value("path").toString();m.name=o.value("name").toString();m.architecture=o.value("architecture").toString();m.error=o.value("error").toString();m.sizeLabel=o.value("sizeLabel").toString();m.modelType=o.value("modelType").toString();m.hasChatTemplate=o.value("hasChatTemplate").toBool();
#define READ(field) m.field=o.value(#field).toString(QString::number(m.field)).toLongLong()
    READ(fileType);READ(shardIndex);READ(fileBytes);READ(trainingContext);READ(layers);READ(heads);READ(kvHeads);READ(embedding);READ(shards);READ(keyLength);READ(valueLength);READ(slidingWindow);
#undef READ
    return m;
}
QJsonObject fingerprint(const QFileInfo &file){return {{"sizeBytes",QString::number(file.size())},{"modifiedMs",QString::number(file.lastModified().toMSecsSinceEpoch())}};}
}
QString ModelCache::defaultPath(){return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("models-cache.json");}
ModelCache::ModelCache(const QString &path):filePath(path),data(read(path)){}
ModelInfo ModelCache::get(const QString &path,bool force,bool *hit){
    if(hit)*hit=false;QFileInfo file(path);const auto id=key(path);const auto stamp=fingerprint(file);auto models=data.value("models").toObject();const auto entry=models.value(id).toObject();
    if(!force&&file.isFile()&&entry.value("fingerprint").toObject()==stamp&&entry.value("info").isObject()){if(hit)*hit=true;auto info=fromJson(entry.value("info").toObject());info.path=file.absoluteFilePath();return info;}
    auto info=readModelInfo(file.absoluteFilePath());file.refresh();
    if(file.isFile()&&fingerprint(file)==stamp){models[id]=QJsonObject{{"fingerprint",stamp},{"info",infoJson(info)}};data["models"]=models;changedModels.insert(id);}
    else if(models.contains(id)){models.remove(id);data["models"]=models;changedModels.insert(id);}
    return info;
}
QVector<ModelInfo> ModelCache::directory(const QString &path,bool recursive,bool *found)const{
    const auto snapshots=data.value("directories").toObject();const auto id=directoryKey(path,recursive);if(found)*found=snapshots.contains(id);QVector<ModelInfo> result;
    const auto models=data.value("models").toObject();for(auto value:snapshots.value(id).toObject().value("paths").toArray()){const auto info=models.value(value.toString()).toObject().value("info").toObject();if(!info.isEmpty())result<<fromJson(info);}return result;
}
void ModelCache::setDirectory(const QString &path,bool recursive,const QVector<ModelInfo> &models){QJsonArray paths;for(const auto &info:models)paths.append(key(info.path));auto snapshots=data.value("directories").toObject();const auto id=directoryKey(path,recursive);snapshots[id]=QJsonObject{{"directory",QFileInfo(path).absoluteFilePath()},{"recursive",recursive},{"updatedAt",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},{"paths",paths}};data["directories"]=snapshots;changedDirectories.insert(id);}
QVector<ModelInfo> ModelCache::allModels()const{QVector<ModelInfo> result;const auto models=data.value("models").toObject();for(auto i=models.begin();i!=models.end();++i){const auto json=i.value().toObject().value("info").toObject();if(!json.value("path").toString().isEmpty())result<<fromJson(json);}return result;}
QStringList ModelCache::directories()const{QMap<QString,QString> paths;const auto snapshots=data.value("directories").toObject();for(auto i=snapshots.begin();i!=snapshots.end();++i){const auto path=i.value().toObject().value("directory").toString();if(!path.isEmpty())paths.insert(key(path),path);}return paths.values();}
QString ModelCache::loadPath(const ModelInfo &info){if(!info.error.isEmpty())return {};if(info.shards<=1||info.shardIndex==0)return info.path;QFileInfo file(info.path);auto first=file.fileName();first.replace(QRegularExpression("-\\d{5}-of-(\\d{5}\\.gguf)$",QRegularExpression::CaseInsensitiveOption),"-00001-of-\\1");const auto result=file.dir().filePath(first);return first!=file.fileName()&&QFileInfo(result).isFile()?result:QString();}
bool ModelCache::save(QString *error){
    if(changedModels.isEmpty()&&changedDirectories.isEmpty())return true;const auto fail=[&](const QString &message){if(error)*error=message;return false;};
    if(!QDir().mkpath(QFileInfo(filePath).absolutePath()))return fail("Не удалось создать папку JSON-кэша моделей.");
    QLockFile lock(filePath+".lock");if(!lock.tryLock(1000))return fail("JSON-кэш моделей занят другим процессом.");
    auto merged=read(filePath);auto models=merged.value("models").toObject();const auto updated=data.value("models").toObject();for(const auto &id:changedModels){if(updated.contains(id))models[id]=updated.value(id);else models.remove(id);}merged["models"]=models;
    auto snapshots=merged.value("directories").toObject();const auto changed=data.value("directories").toObject();for(const auto &id:changedDirectories)snapshots[id]=changed.value(id);merged["directories"]=snapshots;merged["schemaVersion"]=1;
    QSaveFile file(filePath);const auto bytes=QJsonDocument(merged).toJson(QJsonDocument::Indented);if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())return fail("Не удалось записать JSON-кэш: "+file.errorString());data=merged;changedModels.clear();changedDirectories.clear();return true;
}
ModelInfo cachedModelInfo(const QString &path,bool force,QString *error){ModelCache cache;auto info=cache.get(path,force);cache.save(error);return info;}
