#pragma once
#include "advisor.h"
#include <QDialog>
#include <QProcess>
class QLineEdit; class QComboBox; class QSpinBox; class QTableWidget; class QTextBrowser; class QLabel; class QCheckBox; class QPushButton;
class AdvisorDialog : public QDialog {
    Q_OBJECT
public:
    explicit AdvisorDialog(const Config &config, QWidget *parent = nullptr);
    ~AdvisorDialog() override;
    Advice recommendation;
    bool runFit = false;
    QJsonObject options() const;
private:
    const Config &config;
    ModelInfo model;
    QProcess detector;
    QByteArray detectorOutput;
    quint64 detectionRun = 0;
    QLineEdit *path;
    QComboBox *mode, *sampling, *cache;
    QSpinBox *context, *slotCount, *reserve;
    QTableWidget *devices;
    QTextBrowser *report,*memoryReport;
    QLabel *modelLabel, *deviceLabel,*memoryEstimate;
    QCheckBox *cpu;
    QPushButton *detect, *apply, *applyFit;
    void updateAdvice();
    void inspectModel();
    void detectDevices();
    void addDevice(const DeviceInfo &info);
};
