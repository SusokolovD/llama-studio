#include "modelperformance.h"
#include <QRegularExpression>
#include <cmath>
TimingReport parseModelTiming(const QString &input){
    QString line=input;line.remove(QRegularExpression("\\x1b\\[[0-9;]*m"));TimingReport report;
    static const QRegularExpression task("\\bid\\s+(\\d+)\\s*\\|\\s*task\\s+(\\d+)\\b");
    const auto id=task.match(line);if(id.hasMatch())report.task=QString("Слот %1 · задача %2").arg(id.captured(1),id.captured(2));
    static const QString number="([0-9]+(?:\\.[0-9]+)?)";
    static const QRegularExpression prompt("prompt eval time\\s*=\\s*"+number+"\\s*ms[^\\r\\n]*,\\s*"+number+"\\s*tokens per second");
    static const QRegularExpression generation("(?<!prompt )\\beval time\\s*=\\s*"+number+"\\s*ms[^\\r\\n]*,\\s*"+number+"\\s*tokens per second");
    static const QRegularExpression total("\\btotal time\\s*=\\s*"+number+"\\s*ms");
    static const QRegularExpression prefillProgress("\\bt\\s*=\\s*"+number+"\\s*s\\s*/\\s*"+number+"\\s*tokens per second");
    static const QRegularExpression generationProgress("\\bn_gen\\s*=\\s*\\d+,\\s*tg\\s*=\\s*"+number+"\\s*t/s,\\s*tg_3s\\s*=\\s*"+number+"\\s*t/s");
    auto match=prompt.match(line);
    if(match.hasMatch()){report.kind=TimingKind::Prompt;report.milliseconds=match.captured(1).toDouble();report.tokensPerSecond=match.captured(2).toDouble();return report;}
    match=generation.match(line);if(match.hasMatch()){report.kind=TimingKind::Generation;report.milliseconds=match.captured(1).toDouble();report.tokensPerSecond=match.captured(2).toDouble();return report;}
    match=total.match(line);if(match.hasMatch()){report.kind=TimingKind::Total;report.milliseconds=match.captured(1).toDouble();return report;}
    match=prefillProgress.match(line);if(line.contains("prompt processing,")&&match.hasMatch()){report.kind=TimingKind::Prompt;report.live=true;report.tokensPerSecond=match.captured(2).toDouble();return report;}
    match=generationProgress.match(line);if(match.hasMatch()){report.kind=TimingKind::Generation;report.live=true;report.tokensPerSecond=match.captured(2).toDouble();return report;}
    return report;
}
ModelPerformance::ModelPerformance(QObject *parent):QObject(parent){tick.setInterval(1000);connect(&tick,&QTimer::timeout,this,&ModelPerformance::changed);}
void ModelPerformance::start(const QString &model){current={};current.model=model;current.running=true;tasks.clear();lastReport.invalidate();uptime.start();tick.start();emit changed();}
void ModelPerformance::stop(){if(current.running)current.uptimeMs=uptime.elapsed();current.running=false;tick.stop();emit changed();}
void ModelPerformance::readLine(const QString &line){
    const auto r=parseModelTiming(line);if(r.kind==TimingKind::None||!current.running)return;
    if(!std::isfinite(r.tokensPerSecond)||!std::isfinite(r.milliseconds))return;
    const auto key=r.task.isEmpty()?QString("Последний отчёт"):r.task;
    // Keep each task independent: interleaved slots must not combine rates from different requests.
    if(tasks.size()>=128&&!tasks.contains(key))tasks.clear();auto &task=tasks[key];
    if(r.kind==TimingKind::Prompt&&r.task.isEmpty())task={};
    task.task=key;if(r.kind==TimingKind::Prompt){task.promptSpeed=r.tokensPerSecond;task.promptLive=r.live;}else if(r.kind==TimingKind::Generation){task.generationSpeed=r.tokensPerSecond;task.generationLive=r.live;}else task.requestMs=r.milliseconds;
    current.task=task.task;current.promptSpeed=task.promptSpeed;current.generationSpeed=task.generationSpeed;current.promptLive=task.promptLive;current.generationLive=task.generationLive;current.requestMs=task.requestMs;lastReport.start();emit changed();
}
ModelPerformanceSnapshot ModelPerformance::snapshot()const{auto result=current;if(result.running&&uptime.isValid())result.uptimeMs=uptime.elapsed();if(lastReport.isValid())result.reportAgeMs=lastReport.elapsed();return result;}
