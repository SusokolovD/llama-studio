#include "mainwindow.h"
#include "advisordialog.h"
#include <QtTest>
#include <QtWidgets>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QSslSocket>
#include <QTcpSocket>
class GuiTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QApplication::setStyle("Fusion");QFile style(":/style.qss");if(style.open(QIODevice::ReadOnly))qApp->setStyleSheet(QString::fromUtf8(style.readAll()));
    }
    void releaseChecks() {
        QTcpServer api; QVERIFY(api.listen(QHostAddress::LocalHost, 0));
        QByteArray payload = R"([{"tag_name":"b43","html_url":"https://github.com/ggml-org/llama.cpp/releases/tag/b43"},{"tag_name":"b44","html_url":"https://github.com/ggml-org/llama.cpp/releases/tag/b44","prerelease":true},{"tag_name":"b999","draft":true,"html_url":"https://github.com/ggml-org/llama.cpp/releases/tag/b999"},{"tag_name":"b9999","html_url":"https://example.com/evil"}])";
        int responseCode = 200, requests = 0;
        connect(&api, &QTcpServer::newConnection, [&] {
            auto socket = api.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, [&, socket] {
                socket->readAll(); ++requests;
                socket->write("HTTP/1.1 " + QByteArray::number(responseCode) + " Result\r\nContent-Length: " + QByteArray::number(payload.size()) + "\r\nConnection: close\r\n\r\n" + payload); socket->disconnectFromHost();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
        ReleaseChecker checker(nullptr, QUrl("http://127.0.0.1:" + QString::number(api.serverPort())));
        QTemporaryDir dir;
#ifdef Q_OS_WIN
        QString ext = ".exe";
#else
        QString ext;
#endif
        QVERIFY(QFile::copy(FIXTURE_PATH, dir.filePath("llama-server" + ext)));
        QSignalSpy finished(&checker, &ReleaseChecker::checkingChanged);
        checker.check(dir.path()); checker.check(dir.path());
        QTRY_VERIFY(!checker.busy()); QCOMPARE(requests, 1); QCOMPARE(finished.size(), 2);
        QVERIFY(checker.text().contains("b44")); QVERIFY(checker.text().contains("b42")); QVERIFY(checker.text().contains("pre-release"));
        QCOMPARE(checker.releaseUrl().host(), QString("github.com"));
        payload = R"([{"tag_name":"b42","html_url":"https://github.com/ggml-org/llama.cpp/releases/tag/b42"}])";
        checker.check(dir.path()); QTRY_VERIFY(!checker.busy()); QVERIFY(checker.text().contains(QString::fromUtf8("обновлений нет")));
        responseCode = 403; checker.check(dir.path()); QTRY_VERIFY(!checker.busy()); QVERIFY(checker.releaseUrl().isEmpty()); QVERIFY(checker.text().contains(QString::fromUtf8("Лимит")));
        responseCode = 200; payload = "invalid JSON"; checker.check(dir.path()); QTRY_VERIFY(!checker.busy()); QVERIFY(checker.releaseUrl().isEmpty());
    }
    void liveGitHubCheck() {
        if (!qEnvironmentVariableIsSet("STUDIO_LIVE_RELEASE_TEST")) QSKIP("Run explicitly to verify GitHub HTTPS");
        ReleaseChecker checker; checker.check({});
        QTRY_VERIFY_WITH_TIMEOUT(!checker.busy(), 20000);
        qInfo() << checker.text();
        QVERIFY2(!checker.releaseUrl().isEmpty(), qPrintable(checker.text()));
    }
    void sslRuntimeAvailable() {
#ifndef STUDIO_STATIC_OPENSSL
        QSKIP("Static OpenSSL validation applies to the fully static build");
#else
        QVERIFY(QSslSocket::supportsSsl());
        QVERIFY(!QSslSocket::sslLibraryVersionString().isEmpty());
        qInfo() << "TLS runtime:" << QSslSocket::sslLibraryVersionString();
#endif
    }
    void fitThenServerLifecycle() {
        QTemporaryDir dir(QDir::tempPath() + "/llama test-XXXXXX"); QVERIFY(dir.isValid());
#ifdef Q_OS_WIN
        QString ext = ".exe";
#else
        QString ext;
#endif
        QVERIFY(QFile::copy(FIXTURE_PATH, dir.filePath("llama-server" + ext)));
        QVERIFY(QFile::copy(FIXTURE_PATH, dir.filePath("llama-fit-params" + ext)));
        QFile model(dir.filePath(QString::fromUtf8("модель test.gguf"))); QVERIFY(model.open(QIODevice::WriteOnly)); model.close();
        QTcpServer portProbe; QVERIFY(portProbe.listen(QHostAddress::LocalHost, 0)); int port = portProbe.serverPort(); portProbe.close();
        MainWindow w(nullptr, false); w.show(); w.directory->setText(dir.path());
        {
            AdvisorDialog helper(w.config);helper.show();auto table=helper.findChild<QTableWidget*>();QVERIFY(table);QTRY_COMPARE(table->rowCount(),2);
            QVERIFY(helper.recommendation.error.isEmpty());QVERIFY(helper.recommendation.html().contains(QString::fromUtf8("нескольких GPU")));
            if(qEnvironmentVariableIsSet("STUDIO_SCREENSHOT_DIR"))helper.grab().save(qEnvironmentVariable("STUDIO_SCREENSHOT_DIR")+"/advisor-preview.png");
            helper.close();
        }
        for(auto combo:w.findChildren<QComboBox*>())if(combo->findData("auto")>=0 && combo->findData("off")>=0){combo->setCurrentIndex(combo->findData("off"));break;}
        QCOMPARE(w.config.find("--flash-attn")->value.toString(),QString("off"));
        auto m = w.config.find("--model"); m->enabled = true; m->value = model.fileName();
        auto p = w.config.find("--port"); p->enabled = true; p->value = port;
        w.context->setValue(16384); w.launchFit();
        QTRY_COMPARE(w.fit.state(), QProcess::NotRunning);
        QCOMPARE(w.status->text(), QString::fromUtf8("Параметры подобраны"));
        QCOMPARE(w.config.find("--ctx-size")->value.toInt(), 16384);
        QCOMPARE(w.config.find("--override-tensor")->value.toString(), QString("blk\\.14\\.ffn_(up|down)=CPU"));
        w.launchServer(); QTRY_COMPARE(w.server.state(), QProcess::Running);
        QTRY_VERIFY(w.status->text().contains(QString::fromUtf8("Сервер готов")));
        QVERIFY(w.runningCommand->toPlainText().contains(model.fileName().replace('/', QDir::separator())) || w.runningCommand->toPlainText().contains(model.fileName()));
        w.stopping = true; w.stop(w.server); QTRY_COMPARE(w.server.state(), QProcess::NotRunning);
        QVERIFY(w.startButton->isEnabled());
        QFile bad(dir.filePath("failure.gguf")); QVERIFY(bad.open(QIODevice::WriteOnly)); bad.close();
        w.config.find("--model")->value = bad.fileName(); auto before = w.config.save(); w.launchFit();
        QTRY_COMPARE(w.fit.state(), QProcess::NotRunning);
        QCOMPARE(w.config.save(), before); QCOMPARE(w.status->text(), QString::fromUtf8("Ошибка подбора"));
        w.close();
    }
};
QTEST_MAIN(GuiTest)
#include "test_gui.moc"
