#pragma once
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QDataStream>
#include <QJsonDocument>
#include <QJsonArray>
#include <QVariant>
inline bool imageGgufFixture(const QString &path,const QMap<QString,QVariant> &metadata,const QMap<QString,QVector<quint64>> &tensors){
    QDir().mkpath(QFileInfo(path).absolutePath());QFile file(path);if(!file.open(QIODevice::WriteOnly))return false;QDataStream stream(&file);stream.setByteOrder(QDataStream::LittleEndian);auto string=[&](const QString &value){const auto bytes=value.toUtf8();stream<<quint64(bytes.size());stream.writeRawData(bytes.constData(),bytes.size());};stream.writeRawData("GGUF",4);stream<<quint32(3)<<quint64(tensors.size())<<quint64(metadata.size());
    for(auto it=metadata.begin();it!=metadata.end();++it){string(it.key());if(it.value().userType()==QMetaType::QString){stream<<quint32(8);string(it.value().toString());}else stream<<quint32(4)<<quint32(it.value().toUInt());}
    for(auto it=tensors.begin();it!=tensors.end();++it){string(it.key());stream<<quint32(it.value().size());for(auto size:it.value())stream<<size;stream<<quint32(1)<<quint64(0);}
    file.write(QByteArray(int((32-file.pos()%32)%32)+32,'\0'));return stream.status()==QDataStream::Ok;
}
inline bool imageSafeFixture(const QString &path,const QMap<QString,QVector<quint64>> &tensors,const QJsonObject &metadata={}){
    QDir().mkpath(QFileInfo(path).absolutePath());QJsonObject header;header["__metadata__"]=metadata;for(auto it=tensors.begin();it!=tensors.end();++it){QJsonArray shape;for(auto size:it.value())shape.append(double(size));header[it.key()]=QJsonObject{{"dtype","F16"},{"shape",shape},{"data_offsets",QJsonArray{0,2}}};}auto bytes=QJsonDocument(header).toJson(QJsonDocument::Compact);QFile file(path);if(!file.open(QIODevice::WriteOnly))return false;QDataStream stream(&file);stream.setByteOrder(QDataStream::LittleEndian);stream<<quint64(bytes.size());return file.write(bytes)==bytes.size()&&file.write("00",2)==2;
}
