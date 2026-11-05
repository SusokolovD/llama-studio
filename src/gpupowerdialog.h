#pragma once
#include "gpupower.h"
#include "modelperformance.h"
#include <QDialog>
#include <QPointer>
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class GpuPowerDialog:public QDialog{
    Q_OBJECT
public:
    explicit GpuPowerDialog(QWidget *parent=nullptr,ModelPerformance *performance=nullptr,const QString &executable=nvidiaSmiPath());
private:
    GpuPowerService service;
    QPointer<ModelPerformance> performance;
    QTimer poll;
    QComboBox *gpu,*preset;
    QDoubleSpinBox *watts;
    QLabel *details,*ratio,*status,*modelStats;
    QPushButton *refresh,*apply,*restore;
    bool dirty=false,applying=false;
    const GpuPowerInfo *selected()const;
    void updateDevices();
    void updateControls();
    void updateRatio();
    void updateModelStats();
};
