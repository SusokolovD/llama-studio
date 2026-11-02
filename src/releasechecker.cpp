#include "releasechecker.h"
#include "version.h"
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
QString LocalLlamaVersion::description() const {
    QStringList parts;if(!version.isEmpty())parts<<version;if(build>=0)parts<<"b"+QString::number(build);if(!commit.isEmpty())parts<<commit;
    return parts.isEmpty()?QString("версия неизвестна"):parts.join(" · ");
}
LocalLlamaVersion parseLocalLlamaVersion(const QString &output){
    LocalLlamaVersion result;
    const auto options=QRegularExpression::CaseInsensitiveOption|QRegularExpression::MultilineOption;
    const auto line=QRegularExpression("^[ \\t]*version[ \\t]*:[ \\t]*([^\\r\\n]+)",options).match(output).captured(1).trimmed();
    // Modern semver is not the GitHub bNNNNN build number. Prefer the explicit build field.
    const auto semver=QRegularExpression("^v?\\d+\\.\\d+\\.\\d+(?:[-+][A-Za-z0-9_.+-]+)?").match(line);
    if(semver.hasMatch())result.version=semver.captured(0);
    auto build=QRegularExpression("\\bbuild[ \\t]*[:=]?[ \\t]+b?(\\d+)\\b",options).match(line);
    if(!build.hasMatch())build=QRegularExpression("^[ \\t]*build[ \\t]*[:=][ \\t]*b?(\\d+)\\b",options).match(output);
    if(!build.hasMatch()&&!semver.hasMatch())build=QRegularExpression("^b?(\\d+)(?:[ \\t]*\\([^\\r\\n]*\\))?$").match(line);
    if(build.hasMatch()){bool ok=false;auto n=build.captured(1).toLongLong(&ok);if(ok)result.build=n;}
    const auto commit=QRegularExpression("\\bcommit[ \\t]+([0-9a-f]{7,40})\\b",options).match(line);if(commit.hasMatch())result.commit=commit.captured(1).toLower();
    return result;
}
ReleaseChecker::ReleaseChecker(QObject *parent, const QUrl &endpoint) : QObject(parent), apiUrl(endpoint) {
    version.setProcessChannelMode(QProcess::MergedChannels);
    connect(&version, &QProcess::readyReadStandardOutput, this, [this] {
        auto bytes = version.readAllStandardOutput();
        if (versionOutput.size() + bytes.size() <= 65536) versionOutput += bytes;
        else {localError="Вывод --version превышает 64 КиБ.";version.kill();}
    });
    connect(&version, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus exit) {
        const auto tail=version.readAllStandardOutput();if(versionOutput.size()+tail.size()<=65536)versionOutput+=tail;else localError="Вывод --version превышает 64 КиБ.";
        if (code == 0 && exit == QProcess::NormalExit && localError.isEmpty()) {
            installed=parseLocalLlamaVersion(QString::fromUtf8(versionOutput));
            if(installed.build<0)localError="Номер build не найден в выводе --version; сравнение с релизами невозможно.";
        } else if(localError.isEmpty())localError="Команда --version завершилась с ошибкой (код "+QString::number(code)+").";
        versionPending = false; finish();
    });
    connect(&version, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {localError="Не удалось запустить --version: "+version.errorString();versionPending = false; finish(); }
    });
}
ReleaseChecker::~ReleaseChecker() {
    if (version.state() != QProcess::NotRunning) { version.kill(); version.waitForFinished(1000); }
}
void ReleaseChecker::check(const QString &directory) {
    if (busy()) return;
    ++run; networkPending = true; latestBuild = -1;installed={};latestDate.clear();prerelease=false;latestTag.clear(); latestUrl.clear(); networkError.clear(); versionOutput.clear();localPath.clear();localError.clear();
    emit checkingChanged(true); emit resultChanged("llama.cpp: проверка локальной версии и релизов…", {}, {});
    auto exe = findExecutable(directory, false);
    if (!exe.path.isEmpty()) {
        localPath=exe.path;
        versionPending = true; version.setWorkingDirectory(QFileInfo(exe.path).absolutePath());
        version.start(exe.path, exe.prefix + QStringList{"--version"});
        auto currentRun = run;
        QTimer::singleShot(3000, this, [this, currentRun] { if (run == currentRun && versionPending){localError="Превышено время ожидания --version (3 секунды).";version.kill();} });
    }else localError="llama-server не найден в выбранной папке и каталогах сборки.";
    QNetworkRequest request(apiUrl);
    request.setRawHeader("User-Agent", QByteArray("LlamaStudio/")+STUDIO_VERSION); request.setRawHeader("Accept", "application/vnd.github+json");
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
    const auto localText=localPath.isEmpty()?QString("Локальный llama.cpp не найден"):"Установлен llama.cpp: "+installed.description();
    QString details="Локальный файл: "+(localPath.isEmpty()?QString("не найден"):localPath)+"\n";
    if(!localError.isEmpty())details+=localError+"\n";
    if(!versionOutput.isEmpty())details+="Вывод --version:\n"+QString::fromUtf8(versionOutput).trimmed()+"\n";
    if (busy()) {
        if(!versionPending){resultText=localText+" · проверка релизов…";emit resultChanged(resultText,details,{});}
        return;
    }
    if (!networkError.isEmpty()) resultText = localText+" · "+networkError;
    else {
        if (installed.build < 0) resultText = localText+" · последний релиз "+latestTag+" · локальный build неизвестен";
        else if (latestBuild > installed.build) resultText = localText+" · доступен "+latestTag;
        else resultText = localText+" · обновлений нет";
        if (prerelease) resultText += " (pre-release)";
        auto date = QDateTime::fromString(latestDate, Qt::ISODate).toLocalTime();
        details += "Последний опубликованный build, включая pre-release.\n" + (date.isValid() ? "Опубликован: " + date.toString("dd.MM.yyyy HH:mm") : QString());
    }
    emit resultChanged(resultText, details, latestUrl); emit checkingChanged(false);
}
