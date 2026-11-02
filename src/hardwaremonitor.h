#pragma once
#include <QVector>
#include <QStringList>
struct GpuReading{
    QString uuid,name;double temperature=-1,load=-1,memoryUsed=-1,memoryTotal=-1,power=-1,fan=-1;
};
QVector<GpuReading> parseNvidiaReadings(const QString &csv);
struct SystemReading{double cpuPercent=-1;quint64 memoryUsed=0,memoryTotal=0;};
class SystemSampler{
public:
    SystemReading sample();
private:
    quint64 previousIdle=0,previousKernel=0,previousUser=0;bool previousValid=false;
};
