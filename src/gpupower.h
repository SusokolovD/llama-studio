#pragma once
#include "hardwaremonitor.h"
#include "managedprocess.h"
#include <QObject>
#include <QTimer>
#include <QJsonObject>
#include <QMap>
#include <QSet>
class GpuPowerService:public QObject{
    Q_OBJECT
public:
    explicit GpuPowerService(QObject *parent=nullptr,const QString &executable=nvidiaSmiPath());
    ~GpuPowerService()override;
    bool busy()const{return phase!=Idle||batch!=NoBatch||profilePending||resetPending;}
    const QVector<GpuPowerInfo> &devices()const{return cards;}
    void refresh();
    void apply(const QString &uuid,double watts);
    void restoreDefault(const QString &uuid);
    void applyProfile(const QJsonObject &limits);
    void restoreAllDefaults();
    bool needsReset()const{return !touched.isEmpty()||busy();}
    QJsonObject profileLimits;
signals:
    void limitApplied(const QString &uuid,double watts);
    void profileFinished(bool success,const QString &message);
    void resetFinished(bool success,const QString &message);
    void readingsChanged();
    void busyChanged(bool busy);
    void completed(bool success,const QString &message);
private:
    enum Phase{Idle,Read,PrepareWrite,Write,Verify};
    Phase phase=Idle;
    enum Batch{NoBatch,ProfileBatch,ResetBatch};Batch batch=NoBatch;
    bool profilePending=false,resetPending=false;
    QMap<QString,double> queue;
    QSet<QString> touched;
    QMap<QString,double> uncertainOriginal;
    QStringList batchErrors;
    QString executable,target,processError;
    double requested=-1;
    bool factory=false;
    QVector<GpuPowerInfo> cards;
    ManagedProcess process;
    QTimer timeout;
    QByteArray output;
    void begin(Phase next);
    void request(const QString &uuid,double watts,bool reset);
    void finish(bool success,const QString &message);
    const GpuPowerInfo *selected()const;
    void next();
};
