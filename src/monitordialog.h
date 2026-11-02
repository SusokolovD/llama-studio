#pragma once
#include "hardwaremonitor.h"
#include "managedprocess.h"
#include <QDialog>
#include <QTimer>
class QProgressBar;
class QLabel;
class QTableWidget;
class QCheckBox;
class MonitorHistory;
class MonitorDialog:public QDialog{
    Q_OBJECT
public:
    explicit MonitorDialog(QWidget *parent=nullptr);
    ~MonitorDialog() override;
private:
    SystemSampler sampler;
    QTimer poll,nvidiaTimeout,sensorsTimeout;
    ManagedProcess nvidia,sensors;
    QProgressBar *cpu,*ram;
    QLabel *cpuText,*ramText,*gpuStatus,*sensorStatus;
    QTableWidget *gpus,*sensorTable;
    QCheckBox *hardwareSensors;
    MonitorHistory *history;
    QString smi,powershell;
    int sensorTick=0;
    void sample();
    void sampleSensors();
};
