#pragma once
#include <QJsonObject>
#include <QStringList>
#include <QVector>
#include <QMetaType>
#include <functional>

struct ImageAsset {
    QString path,kind="unknown",family,evidence,error;
    qint64 bytes=0;
    bool trusted=false;
    QJsonObject json() const;
    static ImageAsset fromJson(const QJsonObject &json);
};
Q_DECLARE_METATYPE(ImageAsset)
Q_DECLARE_METATYPE(QVector<ImageAsset>)
ImageAsset inspectImageAsset(const QString &path);
QString imageFamilyLabel(const QString &family);
QString imageKindLabel(const QString &kind);
QStringList imageDependencies(const QString &family);
QVector<ImageAsset> imageCandidates(const QVector<ImageAsset> &assets,const QString &family);
class ImageCatalog {
public:
    explicit ImageCatalog(const QString &path=defaultPath());
    static QString defaultPath();
    QVector<ImageAsset> cached(const QString &directory,bool *found=nullptr) const;
    QVector<ImageAsset> scan(const QString &directory,const std::function<bool()> &cancel,const std::function<void(int,const QString &)> &progress,QString *error=nullptr);
private:
    QString filePath;
};
struct ImageFit {
    QString backend,maxVram;
    QStringList notes;
    qint64 weightBytes=0;
};
ImageFit makeImageFit(const QVector<ImageAsset> &components,const QStringList &devices,int reserveMiB,qint64 availableRamBytes,int width,int height);
QStringList parseImageDevices(const QString &output);
