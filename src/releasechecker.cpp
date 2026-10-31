#include "releasechecker.h"
#include "config.h"
#include <QDateTime>
#include <QFileInfo>
#include <QSharedPointer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
ReleaseChecker::ReleaseChecker(QObject *parent, const QUrl &endpoint) : QObject(parent), apiUrl(endpoint) {
    version.setProcessChannelMode(QProcess::MergedChannels);
    connect(&version, &QProcess::readyReadStandardOutput, this, [this] {
        auto bytes = version.readAllStandardOutput();
        if (versionOutput.size() + bytes.size() <= 65536) versionOutput += bytes;
        else version.kill();
    });
    connect(&version, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus exit) {
        versionOutput += version.readAllStandardOutput();
        if (code == 0 && exit == QProcess::NormalExit) {
            auto match = QRegularExpression("(?:version|build)\\s*:\\s*b?(\\d+)", QRegularExpression::CaseInsensitiveOption).match(QString::fromUtf8(versionOutput));
            if (match.hasMatch()) localBuild = match.captured(1).toLongLong();
        }
        versionPending = false; finish();
    });
    connect(&version, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) { versionPending = false; finish(); }
    });
}
ReleaseChecker::~ReleaseChecker() {
    if (version.state() != QProcess::NotRunning) { version.kill(); version.waitForFinished(1000); }
}
void ReleaseChecker::check(const QString &directory) {
    if (busy()) return;
    ++run; networkPending = true; latestBuild = localBuild = -1; latestTag.clear(); latestUrl.clear(); networkError.clear(); versionOutput.clear();
    emit checkingChanged(true); emit resultChanged("llama.cpp: проверка релизов…", {}, {});
    auto exe = findExecutable(directory, false);
    if (!exe.path.isEmpty()) {
        versionPending = true; version.setWorkingDirectory(QFileInfo(exe.path).absolutePath());
        version.start(exe.path, exe.prefix + QStringList{"--version"});
        auto currentRun = run;
        QTimer::singleShot(3000, this, [this, currentRun] { if (run == currentRun && versionPending) version.kill(); });
    }
    QNetworkRequest request(apiUrl);
    request.setRawHeader("User-Agent", "LlamaStudio/0.2"); request.setRawHeader("Accept", "application/vnd.github+json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto reply = network.get(request); reply->setReadBufferSize(2 * 1024 * 1024 + 1);
    auto data = QSharedPointer<QByteArray>::create();
    connect(reply, &QNetworkReply::readyRead, this, [reply, data] {
        data->append(reply->readAll()); if (data->size() > 2 * 1024 * 1024) reply->abort();
    });
    QTimer::singleShot(12000, reply, [reply] { if (reply->isRunning()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, data] {
        data->append(reply->readAll());
        auto code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || code != 200) {
            networkError = code == 403 || code == 429 ? "Лимит GitHub API. Повторите позже." : "Не удалось проверить релизы: " + reply->errorString();
        } else {
            auto doc = QJsonDocument::fromJson(*data);
            if (doc.isArray()) for (auto value : doc.array()) {
                auto o = value.toObject(); if (o.value("draft").toBool()) continue;
                auto tag = o.value("tag_name").toString(); auto m = QRegularExpression("^b(\\d+)$").match(tag);
                QUrl url(o.value("html_url").toString());
                if (!m.hasMatch() || url.scheme() != "https" || url.host() != "github.com" || !url.userInfo().isEmpty() || !url.path().startsWith("/ggml-org/llama.cpp/releases/tag/")) continue;
                qint64 build = m.captured(1).toLongLong();
                if (build > latestBuild) { latestBuild = build; latestTag = tag; latestUrl = url; latestDate = o.value("published_at").toString(); prerelease = o.value("prerelease").toBool(); }
            }
            if (latestBuild < 0) networkError = "GitHub не вернул подходящий релиз llama.cpp.";
        }
        networkPending = false; reply->deleteLater(); finish();
    });
}
void ReleaseChecker::finish() {
    if (busy()) return;
    QString details;
    if (!networkError.isEmpty()) resultText = networkError;
    else {
        if (localBuild < 0) resultText = "Последний релиз llama.cpp: " + latestTag + " · локальная версия неизвестна";
        else if (latestBuild > localBuild) resultText = "Доступен llama.cpp " + latestTag + " · установлен b" + QString::number(localBuild);
        else resultText = "llama.cpp b" + QString::number(localBuild) + " · обновлений нет";
        if (prerelease) resultText += " (pre-release)";
        auto date = QDateTime::fromString(latestDate, Qt::ISODate).toLocalTime();
        details = "Последний опубликованный build, включая pre-release.\n" + (date.isValid() ? "Опубликован: " + date.toString("dd.MM.yyyy HH:mm") : QString());
    }
    emit resultChanged(resultText, details, latestUrl); emit checkingChanged(false);
}
