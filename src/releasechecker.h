#pragma once
#include <QNetworkAccessManager>
#include <QProcess>
#include <QUrl>
class ReleaseChecker : public QObject {
    Q_OBJECT
public:
    explicit ReleaseChecker(QObject *parent = nullptr, const QUrl &endpoint = QUrl("https://api.github.com/repos/ggml-org/llama.cpp/releases?per_page=20"));
    ~ReleaseChecker() override;
    void check(const QString &directory);
    bool busy() const { return networkPending || versionPending; }
    QString text() const { return resultText; }
    QUrl releaseUrl() const { return latestUrl; }
signals:
    void checkingChanged(bool checking);
    void resultChanged(const QString &text, const QString &details, const QUrl &url);
private:
    QNetworkAccessManager network;
    QProcess version;
    QUrl apiUrl, latestUrl;
    QString latestTag, latestDate, networkError, resultText;
    QByteArray versionOutput;
    qint64 latestBuild = -1, localBuild = -1;
    bool networkPending = false, versionPending = false, prerelease = false;
    quint64 run = 0;
    void finish();
};
