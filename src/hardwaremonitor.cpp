#include "hardwaremonitor.h"
#include <QtGlobal>
#include <cmath>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace{
QStringList csvFields(const QString &line){
    QStringList fields;QString field;bool quoted=false;
    for(int i=0;i<line.size();++i){const auto c=line[i];if(c=='"'){if(quoted&&i+1<line.size()&&line[i+1]=='"'){field+='"';++i;}else quoted=!quoted;}else if(c==','&&!quoted){fields<<field.trimmed();field.clear();}else field+=c;}
    if(quoted)return {};fields<<field.trimmed();return fields;
}
double value(const QString &text){bool ok=false;double n=text.toDouble(&ok);return ok&&std::isfinite(n)&&n>=0?n:-1;}
}
QVector<GpuReading> parseNvidiaReadings(const QString &csv){
    QVector<GpuReading> result;
    for(const auto &line:csv.split('\n')){const auto f=csvFields(line.trimmed());if(f.size()!=8||!f[0].startsWith("GPU-")||f[1].isEmpty())continue;
        GpuReading r;r.uuid=f[0];r.name=f[1];r.temperature=value(f[2]);r.load=value(f[3]);if(r.load>100)r.load=-1;r.memoryUsed=value(f[4]);r.memoryTotal=value(f[5]);r.power=value(f[6]);r.fan=value(f[7]);if(r.fan>100)r.fan=-1;result<<r;
    }return result;
}
SystemReading SystemSampler::sample(){
    SystemReading result;
#ifdef Q_OS_WIN
    FILETIME idle{},kernel{},user{};auto ticks=[](FILETIME t){return (quint64(t.dwHighDateTime)<<32)|t.dwLowDateTime;};
    if(GetSystemTimes(&idle,&kernel,&user)){
        auto i=ticks(idle),k=ticks(kernel),u=ticks(user);
        if(previousValid&&i>=previousIdle&&k>=previousKernel&&u>=previousUser){auto total=(k-previousKernel)+(u-previousUser);auto rest=i-previousIdle;if(total&&rest<=total)result.cpuPercent=100.0*double(total-rest)/double(total);}
        previousIdle=i;previousKernel=k;previousUser=u;previousValid=true;
    }
    MEMORYSTATUSEX memory{};memory.dwLength=sizeof(memory);if(GlobalMemoryStatusEx(&memory)){result.memoryTotal=memory.ullTotalPhys;result.memoryUsed=memory.ullTotalPhys-memory.ullAvailPhys;}
#endif
    return result;
}
