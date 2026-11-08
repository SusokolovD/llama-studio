#pragma once
#include "config.h"
#include <QMap>
#include <QMetaType>
struct ModelInfo {
    QString path, name, architecture, error, sizeLabel, modelType, userRole;
    qint64 fileType = -1, shardIndex = 0;
    qint64 fileBytes = 0, trainingContext = 0, layers = 0, heads = 0, kvHeads = 0, embedding = 0, shards = 1;
    bool hasChatTemplate = false;
    qint64 keyLength = 0, valueLength = 0, slidingWindow = 0;
};
Q_DECLARE_METATYPE(ModelInfo)
QString modelQuantization(qint64 fileType);
QString modelRole(const ModelInfo &info);
QString modelRoleLabel(const QString &role);
struct DeviceInfo { QString id, name; qint64 freeMiB = 0, totalMiB = 0; };
struct AdviceInput {
    bool agent = false, cpuOnly = false, hasExtras = false;
    int contextPerSlot = 8192, slotCount = 1, reserveMiB = 1024, sampling = 0;
    QString cacheType = "f16";
    QVector<DeviceInfo> devices;
    ModelInfo model;
};
struct AdviceChange { QString name; QJsonValue value; bool enabled = true; };
struct Advice {
    QVector<AdviceChange> changes;
    QStringList warnings, notes;
    QString error;
    int totalContext = 0;
    double estimatedKvMiB = -1;
    bool apply(Config *config, QString *error) const;
    QString html() const;
};
ModelInfo readModelInfo(const QString &path);
QVector<DeviceInfo> parseDevices(const QString &output);
Advice makeAdvice(const AdviceInput &input);
double estimateKvMiB(const ModelInfo &model,qint64 totalTokens,const QString &cacheType);
QString memoryHtml(const AdviceInput &input);
