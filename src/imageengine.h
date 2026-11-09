#pragma once
#include "managedprocess.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <QUrl>
class QNetworkReply;

struct ImageSettings {
    QString rootDirectory, executablePath, modelDirectory, listenIp, selectedProfile;
    int port=9940;
    QJsonArray profiles;
    static QJsonObject defaults();
    static ImageSettings fromJson(const QJsonObject &json);
    QJsonObject toJson() const;
    QJsonObject profile() const;
    QString rootPath() const;
    QString resolve(const QString &path) const;
    QStringList componentProblems() const;
    bool command(QString *executable,QStringList *arguments,QUrl *url,QString *error,bool checkFiles=true) const;
};

class ImageServer : public QObject {
    Q_OBJECT
public:
    explicit ImageServer(QObject *parent=nullptr);
    ~ImageServer() override;
    bool start(const ImageSettings &settings,QString *error);
    void stop();
    void kill();
    bool busy() const { return active; }
    bool ready() const { return modelReady; }
    bool webAvailable() const { return webReady; }
    QUrl url() const { return runningUrl; }
    QString status() const { return stateText; }
    QString commandText() const { return launchedCommand; }
signals:
    void output(const QString &text);
    void changed();
private:
    friend class ImageTest;
    ManagedProcess process;
    QNetworkAccessManager network;
    QTimer healthTimer;
    QPointer<QNetworkReply> reply;
    QByteArray pendingOut,pendingError;
    QUrl runningUrl;
    QString stateText="Сервер изображений остановлен",launchedCommand;
    bool active=false,stopping=false,modelReady=false,webReady=false;
    quint64 run=0;
    void poll();
    void read(bool standardOutput,bool finish=false);
    void setStatus(const QString &text);
    void finish(int code,QProcess::ExitStatus exit);
};
