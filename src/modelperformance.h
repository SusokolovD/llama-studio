#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QMap>
#include <QTimer>
enum class TimingKind{None,Prompt,Generation,Total};
struct TimingReport{
    TimingKind kind=TimingKind::None;
    QString task;
    double tokensPerSecond=-1,milliseconds=-1;
    bool live=false;
};
TimingReport parseModelTiming(const QString &line);
struct ModelPerformanceSnapshot{
    QString model,task;
    bool running=false,promptLive=false,generationLive=false;
    qint64 uptimeMs=0,reportAgeMs=-1;
    double promptSpeed=-1,generationSpeed=-1,requestMs=-1;
};
class ModelPerformance:public QObject{
    Q_OBJECT
public:
    explicit ModelPerformance(QObject *parent=nullptr);
    void start(const QString &model);
    void stop();
    void readLine(const QString &line);
    ModelPerformanceSnapshot snapshot() const;
signals:
    void changed();
private:
    ModelPerformanceSnapshot current;
    QMap<QString,ModelPerformanceSnapshot> tasks;
    QElapsedTimer uptime,lastReport;
    QTimer tick;
};
