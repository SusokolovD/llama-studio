#pragma once
#include <QObject>
#include <QJsonObject>
#include <QProcess>
#include <QThread>
#include <atomic>
#include <memory>
#include "buildrecipe.h"
struct BuildSettings {
    QString sourceDirectory, binaryDirectory, targetDirectory, commands;
    QString repositoryUrl, repositoryBranch;
    static BuildSettings fromJson(const QJsonObject &json);
    QJsonObject toJson() const;
    bool validate(QString *error) const;
};
class BuildService : public QObject {
    Q_OBJECT
public:
    explicit BuildService(QObject *parent=nullptr);
    ~BuildService() override;
    bool start(const BuildSettings &settings, QString *error);
    void cancel();
    bool busy() const { return active; }
signals:
    void output(const QString &text);
    void phaseChanged(const QString &text);
    void busyChanged(bool busy);
    void finished(bool success, const QString &message, const QString &targetDirectory);
private:
    QProcess process;
    QProcess treeKiller;
    QThread *copyThread=nullptr;
    BuildSettings settings;
    QVector<BuildCommand> commands;
    QByteArray pending;
    int step=0;
    bool active=false;
    quint64 generation=0;
    std::shared_ptr<std::atomic_bool> canceled;
    void next();
    void complete(bool success,const QString &message);
    void drain(bool flush=false);
    void deploy();
};
