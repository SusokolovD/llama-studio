#pragma once
#include "hardwaremonitor.h"
#include "managedprocess.h"
#include "modelperformance.h"
#include "gpupower.h"
#include <QPointer>
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
    explicit MonitorDialog(QWidget *parent=nullptr,ModelPerformance *performance=nullptr,GpuPowerService *power=nullptr);
    ~MonitorDialog() override;
private:
    SystemSampler sampler;
    QTimer poll,nvidiaTimeout,sensorsTimeout,displayTimer;
    QPointer<ModelPerformance> performance;
    QPointer<GpuPowerService> powerService;
    ManagedProcess nvidia,sensors;
    QProgressBar *cpu,*ram;
    QLabel *cpuText,*ramText,*gpuStatus,*sensorStatus;
    QTableWidget *gpus,*sensorTable;
    QCheckBox *hardwareSensors;
    MonitorHistory *history;
    QLabel *modelName,*modelState,*prefillSpeed,*generationSpeed,*modelDetails,*gpuSummary,*temperatureSummary;
    QString smi,powershell;
    int sensorTick=0;
    void sample();
    void sampleSensors();
    void updateSummary();
};
