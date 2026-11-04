#include "mainwindow.h"
#include "advisordialog.h"
#include "builderpage.h"
#include "version.h"
#include "studiobutton.h"
#include "modellibrary.h"
#include "monitordialog.h"
#include "modelpicker.h"
#include "modelcache.h"
#include <QtTest>
#include <QtWidgets>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QSslSocket>
#include <QTcpSocket>
class GuiTest : public QObject {
    Q_OBJECT
    QTemporaryDir settingsDirectory;
    void answerExit(bool yes){QTimer::singleShot(0,qApp,[yes]{auto box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());QVERIFY(box);auto button=box->findChild<QPushButton*>(yes?"exitYes":"exitNo");QVERIFY(button);QTest::mouseClick(button,Qt::LeftButton,Qt::NoModifier,QPoint(2,button->height()/2));});}
private slots:
    void modelPickerAndParameterPaths(){
        QTemporaryDir dir;QVERIFY(QDir(dir.path()).mkdir("A"));QVERIFY(QDir(dir.path()).mkdir("B"));const auto a=dir.filePath("A/main.gguf"),b=dir.filePath("B/projector.gguf");
        for(const auto &path:QStringList{a,b}){QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QDataStream s(&file);s.setByteOrder(QDataStream::LittleEndian);s.writeRawData("GGUF",4);s<<quint32(3)<<quint64(0)<<quint64(0);}
        ModelCache cache;auto first=cache.get(a),second=cache.get(b);cache.setDirectory(dir.filePath("A"),true,{first});cache.setDirectory(dir.filePath("B"),true,{second});QString error;QVERIFY2(cache.save(&error),qPrintable(error));
        ModelPickerDialog picker("Выбор",a);auto source=picker.findChild<QComboBox*>("modelPickerSource");auto table=picker.findChild<QTableWidget*>("modelPickerTable");auto filter=picker.findChild<QLineEdit*>("modelPickerFilter");auto choose=picker.findChild<QPushButton*>("useCachedModel");QVERIFY(picker.findChild<QPushButton*>("browseAnyModelFile"));QVERIFY(source->findData(dir.filePath("A"))>=0);source->setCurrentIndex(source->findData(dir.filePath("A")));int row=-1,visible=0;for(int i=0;i<table->rowCount();++i){if(!table->isRowHidden(i))++visible;if(table->item(i,4)->text()==QDir::toNativeSeparators(a))row=i;}QCOMPARE(visible,1);QVERIFY(row>=0);table->selectRow(row);QVERIFY(choose->isEnabled());filter->setText("projector");QVERIFY(!choose->isEnabled());filter->clear();table->selectRow(row);choose->click();QCOMPARE(picker.selectedPath(),a);
        MainWindow window(nullptr,false,false);window.show();for(const auto &name:QStringList{"--model","--mmproj","--spec-draft-model"})window.config.find(name)->enabled=true;window.navigation->setCurrentRow(1);window.rebuild();
        for(const auto &name:QStringList{"--model","--mmproj","--spec-draft-model"}){auto browse=window.findChild<QPushButton*>("browse:"+name);QVERIFY(browse);QVERIFY(browse->isEnabled());QTimer::singleShot(0,qApp,[a]{auto active=qobject_cast<QDialog*>(QApplication::activeModalWidget());auto dialog=qobject_cast<ModelPickerDialog*>(active);if(!dialog){if(active)active->reject();QFAIL("Expected model picker");}auto rows=dialog->findChild<QTableWidget*>("modelPickerTable");for(int r=0;r<rows->rowCount();++r)if(rows->item(r,4)->text()==QDir::toNativeSeparators(a)){rows->selectRow(r);dialog->findChild<QPushButton*>("useCachedModel")->click();return;}dialog->reject();QFAIL("Cached model missing");});browse->click();QCOMPARE(QDir::fromNativeSeparators(window.config.find(name)->value.toString()),a);}
        answerExit(true);window.close();
    }
    void lastProfileRestored(){
        QSettings().remove("lastProfilePath");QTemporaryDir dir;QString error;const auto profile=dir.filePath("profile.json");
        MainWindow first(nullptr,false,false);first.config.fitContext=0;first.config.directory="C:/profile-llama";first.config.find("--temp")->value=0.37;first.config.find("--temp")->enabled=true;QVERIFY(first.saveProfile(profile,&error));
        MainWindow restored(nullptr,false);QCOMPARE(restored.config.fitContext,0);QCOMPARE(restored.directory->text(),QString("C:/profile-llama"));QCOMPARE(restored.config.find("--temp")->value.toDouble(),0.37);QVERIFY(restored.config.find("--temp")->enabled);QCOMPARE(restored.windowFilePath(),profile);
        QFile bad(dir.filePath("bad.json"));QVERIFY(bad.open(QIODevice::WriteOnly));bad.write("bad JSON");bad.close();QVERIFY(!first.loadProfile(bad.fileName(),&error));QVERIFY(!first.saveProfile(dir.filePath("missing/fail.json"),&error));QCOMPARE(QSettings().value("lastProfilePath").toString(),profile);
        QFile catalog(":/parameters.json");QVERIFY(catalog.open(QIODevice::ReadOnly));QFile second(dir.filePath("second.json"));QVERIFY(second.open(QIODevice::WriteOnly));second.write(catalog.readAll());second.close();QVERIFY(first.loadProfile(second.fileName(),&error,true));QCOMPARE(QSettings().value("lastProfilePath").toString(),profile);QVERIFY(first.loadProfile(second.fileName(),&error));QCOMPARE(QSettings().value("lastProfilePath").toString(),second.fileName());
        QVERIFY(QFile::remove(second.fileName()));MainWindow missing(nullptr,false);QVERIFY(missing.status->text().contains(QString::fromUtf8("Профиль недоступен")));QVERIFY(missing.config.parameters.size()>200);QSettings().setValue("lastProfilePath",bad.fileName());MainWindow invalid(nullptr,false);QVERIFY(invalid.status->text().contains(QString::fromUtf8("Профиль недоступен")));QSettings().remove("lastProfilePath");
    }
    void localVersionParsing_data(){
        QTest::addColumn<QString>("output");QTest::addColumn<qint64>("build");QTest::addColumn<QString>("name");QTest::addColumn<QString>("commit");
        QTest::newRow("modern")<<QString("version: 0.8.0-dev (build 12024, commit ddf48f85c)\r\nbuilt with MSVC 19.51.36246.0 for x64\r\n")<<qint64(12024)<<QString("0.8.0-dev")<<QString("ddf48f85c");
        QTest::newRow("legacy")<<QString("version: 42 (fixture)\n")<<qint64(42)<<QString()<<QString();
        QTest::newRow("legacy-tag")<<QString("version: b1234\n")<<qint64(1234)<<QString()<<QString();
        QTest::newRow("build-line")<<QString("version: 0.8.0-dev\nbuild: 12025\n")<<qint64(12025)<<QString("0.8.0-dev")<<QString();
        QTest::newRow("only-build")<<QString("build: 9876 (abcdef0)\n")<<qint64(9876)<<QString()<<QString();
        QTest::newRow("no-build")<<QString("version: 0.8.0-dev\nbuilt with MSVC 19.51.36246.0 for x64\n")<<qint64(-1)<<QString("0.8.0-dev")<<QString();
        QTest::newRow("compiler-only")<<QString("built with MSVC 19.51.36246.0 for x64\n")<<qint64(-1)<<QString()<<QString();
        QTest::newRow("overflow")<<QString("version: 9999999999999999999999999999\n")<<qint64(-1)<<QString()<<QString();
    }
    void localVersionParsing(){QFETCH(QString,output);QFETCH(qint64,build);QFETCH(QString,name);QFETCH(QString,commit);auto parsed=parseLocalLlamaVersion(output);QCOMPARE(parsed.build,build);QCOMPARE(parsed.version,name);QCOMPARE(parsed.commit,commit);}
    void modelLibraryScanAndSelection(){
        QTemporaryDir dir;QVERIFY(QDir(dir.path()).mkdir("nested"));
        auto write=[&](const QString &path,int shard){QFile file(path);if(!file.open(QIODevice::WriteOnly))return false;QDataStream s(&file);s.setByteOrder(QDataStream::LittleEndian);auto string=[&](const QByteArray &v){s<<quint64(v.size());s.writeRawData(v.constData(),v.size());};s.writeRawData("GGUF",4);s<<quint32(3)<<quint64(0)<<quint64(7);for(const auto &p:QVector<QPair<QByteArray,QByteArray>>{{"general.name","Test model"},{"general.architecture","llama"},{"general.size_label","7B"}}){string(p.first);s<<quint32(8);string(p.second);}for(const auto &p:QVector<QPair<QByteArray,quint32>>{{"general.file_type",15},{"llama.context_length",32768},{"split.count",quint32(shard<0?1:2)},{"split.no",quint32(shard<0?0:shard)}}){string(p.first);s<<quint32(4)<<p.second;}return s.status()==QDataStream::Ok;};
        QVERIFY(write(dir.filePath("model.gguf"),-1));const auto first=dir.filePath("nested/model-00001-of-00002.GGUF");QVERIFY(write(first,0));QVERIFY(write(dir.filePath("nested/model-00002-of-00002.GGUF"),1));QFile bad(dir.filePath("broken.gguf"));QVERIFY(bad.open(QIODevice::WriteOnly));bad.write("bad");bad.close();
        ModelLibraryDialog dialog(dir.path());dialog.show();auto table=dialog.findChild<QTableWidget*>("modelLibraryTable");QVERIFY(table);QTRY_COMPARE(table->rowCount(),4);QTRY_VERIFY(dialog.findChild<QLabel*>("modelLibraryStatus")->text().contains(QString::fromUtf8("завершено")));
        auto filter=dialog.findChild<QLineEdit*>("modelLibraryFilter");filter->setText("Q4_K_M");int visible=0,selected=-1;for(int r=0;r<table->rowCount();++r){if(!table->isRowHidden(r))++visible;if(table->item(r,0)->text().contains("00002-of"))selected=r;}QCOMPARE(visible,3);QVERIFY(selected>=0);table->selectRow(selected);QSignalSpy choice(&dialog,&ModelLibraryDialog::modelSelected);auto use=dialog.findChild<QComboBox*>();QVERIFY(use);use->setCurrentIndex(2);for(auto button:dialog.findChildren<QPushButton*>())if(button->text()==QString::fromUtf8("Использовать выбранную"))button->click();QCOMPARE(choice.size(),1);QCOMPARE(choice[0][0].toString(),first);QCOMPARE(choice[0][1].toString(),QString("--spec-draft-model"));
        if(qEnvironmentVariableIsSet("STUDIO_SCREENSHOT_DIR"))dialog.grab().save(qEnvironmentVariable("STUDIO_SCREENSHOT_DIR")+"/models-preview.png");dialog.close();QVERIFY(write(dir.filePath("new.gguf"),-1));ModelLibraryDialog reopened(dir.path());auto cachedTable=reopened.findChild<QTableWidget*>("modelLibraryTable");QCOMPARE(cachedTable->rowCount(),4);QVERIFY(reopened.findChild<QLabel*>("modelLibraryStatus")->text().contains("JSON"));for(auto button:reopened.findChildren<QPushButton*>())if(button->text()==QString::fromUtf8("Обновить каталог"))button->click();QTRY_COMPARE(cachedTable->rowCount(),5);reopened.close();
    }
    void monitorWindowLifecycle(){
        MainWindow window(nullptr,false);window.show();window.showMonitor();auto dialog=window.findChild<MonitorDialog*>("hardwareMonitorWindow");QVERIFY(dialog);window.showMonitor();QCOMPARE(window.findChildren<MonitorDialog*>().size(),1);
        window.performance.start("test-model.gguf");window.performance.readLine("slot print_timings: id 0 | task 7 | prompt eval time = 100.00 ms / 100 tokens (1.00 ms per token, 1000.00 tokens per second)");window.performance.readLine("slot print_timings: id 0 | task 7 | eval time = 200.00 ms / 10 tokens (20.00 ms per token, 50.00 tokens per second)");window.performance.readLine("slot print_timings: id 0 | task 7 | total time = 300.00 ms / 110 tokens");QCOMPARE(dialog->findChild<QLabel*>("monitorModel")->text(),QString("test-model.gguf"));int speeds=0;for(auto value:dialog->findChildren<QLabel*>("monitorSpeed")){if(value->property("metric")=="prefill")QVERIFY(value->text().contains("1000.0"));else QVERIFY(value->text().contains("50.0"));++speeds;}QCOMPARE(speeds,2);
#ifdef Q_OS_WIN
        QTRY_VERIFY_WITH_TIMEOUT(dialog->findChild<QProgressBar*>("monitorCpu")->isEnabled(),6000);QVERIFY(dialog->findChild<QProgressBar*>("monitorRam")->isEnabled());
#endif
        QVERIFY(dialog->width()<=400);if(qEnvironmentVariableIsSet("STUDIO_SCREENSHOT_DIR"))dialog->grab().save(qEnvironmentVariable("STUDIO_SCREENSHOT_DIR")+"/monitor-preview.png");dialog->close();QTRY_VERIFY(window.findChild<MonitorDialog*>("hardwareMonitorWindow")==nullptr);window.showMonitor();auto reopened=window.findChild<MonitorDialog*>("hardwareMonitorWindow");QVERIFY(reopened);QCOMPARE(reopened->findChild<QLabel*>("monitorModel")->text(),QString("test-model.gguf"));window.performance.stop();QVERIFY(reopened->findChild<QLabel*>("monitorUptime")->text().contains(QString::fromUtf8("Остановлен")));answerExit(true);window.close();
    }
    void initTestCase() {
        QVERIFY(settingsDirectory.isValid());QStandardPaths::setTestModeEnabled(true);QCoreApplication::setOrganizationName("LlamaStudioTests");QCoreApplication::setApplicationName("LlamaStudioTests");QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settingsDirectory.path());
        QApplication::setStyle("Fusion");QFile style(":/style.qss");if(style.open(QIODevice::ReadOnly))qApp->setStyleSheet(QString::fromUtf8(style.readAll()));
    }
    void builderSettingsAndVersion() {
        MainWindow window(nullptr,false);window.show();
        QCOMPARE(window.findChild<QLabel*>("version")->text(),QString(STUDIO_VERSION));QVERIFY(window.windowTitle().contains(STUDIO_VERSION));QVERIFY(!window.windowIcon().isNull());
        auto page=window.buildPage;QVERIFY(page);page->findChild<QLineEdit*>("buildSource")->setText("C:/source with spaces");page->findChild<QLineEdit*>("buildTarget")->setText("C:/llama-build");
        QCOMPARE(window.config.metadata.value("build").toObject().value("sourceDirectory").toString(),QString("C:/source with spaces"));
        Config loaded;QString error;QVERIFY(loaded.load(window.config.save(),&error));QCOMPARE(BuildSettings::fromJson(loaded.metadata.value("build").toObject()).targetDirectory,QString("C:/llama-build"));
        window.tabs->setCurrentWidget(page);QApplication::processEvents();
        if(qEnvironmentVariableIsSet("STUDIO_SCREENSHOT_DIR"))window.grab().save(qEnvironmentVariable("STUDIO_SCREENSHOT_DIR")+"/builder-preview.png");
        answerExit(true);window.close();
    }
    void fullButtonAreaAndExitConfirmation(){
        StudioButton button("Кнопка");button.resize(240,60);button.show();QSignalSpy clicks(&button,&QPushButton::clicked);
        for(auto p:{QPoint(1,30),QPoint(238,30),QPoint(120,1),QPoint(120,58),QPoint(120,30)})QTest::mouseClick(&button,Qt::LeftButton,Qt::NoModifier,p);
        QCOMPARE(clicks.size(),5);button.setEnabled(false);QTest::mouseClick(&button,Qt::LeftButton,Qt::NoModifier,QPoint(1,30));QCOMPARE(clicks.size(),5);button.close();
        MainWindow window(nullptr,false);window.show();answerExit(false);QVERIFY(!window.close());QVERIFY(window.isVisible());QVERIFY(!window.closeConfirmed);
        answerExit(true);QVERIFY(window.close());QVERIFY(!window.isVisible());
    }
    void recipeFileImportExport(){
        QTemporaryDir dir;BuilderPage page;auto edit=page.findChild<QPlainTextEdit*>("buildCommands");QVERIFY(edit);QString error;
        const QString good=BuildRecipe::fromLegacy("cmake --version");QFile file(dir.filePath("commands.json"));QVERIFY(file.open(QIODevice::WriteOnly));file.write(good.toUtf8());file.close();
        QVERIFY(page.loadRecipeFile(file.fileName(),&error));QCOMPARE(page.settings().commands,good);QVERIFY(page.saveRecipeFile(dir.filePath("saved.json"),&error));
        QFile bad(dir.filePath("bad.json"));QVERIFY(bad.open(QIODevice::WriteOnly));bad.write("bad");bad.close();QVERIFY(!page.loadRecipeFile(bad.fileName(),&error));QCOMPARE(page.settings().commands,good);
        edit->setPlainText("{bad JSON");QVERIFY(!page.saveRecipeFile(dir.filePath("saved.json"),&error));QFile saved(dir.filePath("saved.json"));QVERIFY(saved.open(QIODevice::ReadOnly));QVERIFY(BuildRecipe::parse(saved.readAll(),nullptr,&error));
    }
    void advisorLearningPages(){
        QTemporaryDir dir;QFile gguf(dir.filePath("educational.gguf"));QVERIFY(gguf.open(QIODevice::WriteOnly));QDataStream stream(&gguf);stream.setByteOrder(QDataStream::LittleEndian);
        auto string=[&](const QByteArray &s){stream<<quint64(s.size());stream.writeRawData(s.constData(),s.size());};stream.writeRawData("GGUF",4);stream<<quint32(3)<<quint64(0)<<quint64(8);
        for(const auto &pair:QVector<QPair<QByteArray,QByteArray>>{{"general.architecture","llama"},{"general.name","Educational model"},{"tokenizer.chat_template","{{ messages }}"}}){string(pair.first);stream<<quint32(8);string(pair.second);}
        for(const auto &pair:QVector<QPair<QByteArray,quint32>>{{"llama.context_length",32768},{"llama.block_count",32},{"llama.embedding_length",4096},{"llama.attention.head_count",32},{"llama.attention.head_count_kv",8}}){string(pair.first);stream<<quint32(4)<<pair.second;}gguf.close();
        Config cfg;QFile catalog(":/parameters.json");QVERIFY(catalog.open(QIODevice::ReadOnly));QString error;QVERIFY(cfg.load(catalog.readAll(),&error));cfg.find("--model")->value=gguf.fileName();cfg.metadata["advisor"]=QJsonObject{{"cpuOnly",true}};
        AdvisorDialog helper(cfg);helper.show();auto label=helper.findChild<QLabel*>("memoryEstimate");QVERIFY(label->text().contains("1024"));
        helper.findChild<QSpinBox*>("advisorContext")->setValue(16384);QVERIFY(label->text().contains("2048"));helper.findChild<QComboBox*>("advisorCache")->setCurrentIndex(1);QVERIFY(label->text().contains("1088"));
        auto guide=helper.findChild<QTextBrowser*>("advisorGuide");QVERIFY(guide->toPlainText().contains("--top-k"));QVERIFY(guide->toPlainText().contains(QString::fromUtf8("Температура")));
        auto tabs=helper.findChild<QTabWidget*>("advisorInsights");QCOMPARE(tabs->count(),3);tabs->setCurrentIndex(1);QApplication::processEvents();
        if(qEnvironmentVariableIsSet("STUDIO_SCREENSHOT_DIR"))helper.grab().save(qEnvironmentVariable("STUDIO_SCREENSHOT_DIR")+"/advisor-memory-preview.png");
        tabs->setCurrentIndex(2);if(qEnvironmentVariableIsSet("STUDIO_SCREENSHOT_DIR"))helper.grab().save(qEnvironmentVariable("STUDIO_SCREENSHOT_DIR")+"/advisor-guide-preview.png");helper.close();
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
        const auto bin=dir.filePath("build/bin/Release");QVERIFY(QDir().mkpath(bin));const auto executable=QDir(bin).filePath("llama-server"+ext);QVERIFY(QFile::copy(FIXTURE_PATH,executable));
        QSignalSpy finished(&checker, &ReleaseChecker::checkingChanged);
        checker.check(dir.path()); checker.check(dir.path());
        QTRY_VERIFY(!checker.busy()); QCOMPARE(requests, 1); QCOMPARE(finished.size(), 2);
        QVERIFY(checker.text().contains("b44")); QVERIFY(checker.text().contains("b42")); QVERIFY(checker.text().contains("pre-release"));
        QCOMPARE(checker.releaseUrl().host(), QString("github.com"));
        payload = R"([{"tag_name":"b42","html_url":"https://github.com/ggml-org/llama.cpp/releases/tag/b42"}])";
        checker.check(dir.path()); QTRY_VERIFY(!checker.busy()); QVERIFY(checker.text().contains(QString::fromUtf8("обновлений нет")));
        auto writeVersion=[&](int build){QFile output(QDir(bin).filePath("fixture-version.txt"));if(!output.open(QIODevice::WriteOnly|QIODevice::Truncate))return false;const auto bytes=QString("version: 0.8.0-dev (build %1, commit ddf48f85c)\nbuilt with MSVC 19.51.36246.0 for x64\n").arg(build).toUtf8();return output.write(bytes)==bytes.size();};
        payload=R"([{"tag_name":"b12025","html_url":"https://github.com/ggml-org/llama.cpp/releases/tag/b12025"}])";QVERIFY(writeVersion(12024));QSignalSpy result(&checker,&ReleaseChecker::resultChanged);
        checker.check(dir.path());QTRY_VERIFY(!checker.busy());QVERIFY(checker.text().contains("0.8.0-dev"));QVERIFY(checker.text().contains("b12024"));QVERIFY(checker.text().contains("b12025"));QVERIFY(checker.text().contains("ddf48f85c"));QVERIFY(result.last()[1].toString().contains("MSVC 19.51.36246.0 for x64"));
        QVERIFY(writeVersion(12026));checker.check(dir.path());QTRY_VERIFY(!checker.busy());QVERIFY(checker.text().contains("b12026"));QVERIFY(checker.text().contains(QString::fromUtf8("обновлений нет")));
        QVERIFY(writeVersion(12027));responseCode = 403; checker.check(dir.path()); QTRY_VERIFY(!checker.busy()); QVERIFY(checker.releaseUrl().isEmpty()); QVERIFY(checker.text().contains(QString::fromUtf8("Лимит")));QVERIFY(checker.text().contains("b12027"));
        responseCode = 200; payload = "invalid JSON"; checker.check(dir.path()); QTRY_VERIFY(!checker.busy()); QVERIFY(checker.releaseUrl().isEmpty());
        QVERIFY(QFile::remove(executable));checker.check(dir.path());QTRY_VERIFY(!checker.busy());QVERIFY(!checker.text().contains("b12027"));QVERIFY(result.last()[1].toString().contains(QString::fromUtf8("не найден")));
    }
    void liveGitHubCheck() {
        if (!qEnvironmentVariableIsSet("STUDIO_LIVE_RELEASE_TEST")) QSKIP("Run explicitly to verify GitHub HTTPS");
        ReleaseChecker checker; checker.check({});
        QTRY_VERIFY_WITH_TIMEOUT(!checker.busy(), 20000);
        qInfo() << checker.text();
        QVERIFY2(!checker.releaseUrl().isEmpty(), qPrintable(checker.text()));
    }
    void sslRuntimeAvailable() {
#ifndef STUDIO_STATIC_TLS
        QSKIP("TLS validation applies to the fully static build");
#else
        QVERIFY(QSslSocket::supportsSsl());
#ifdef STUDIO_STATIC_OPENSSL
        QVERIFY(!QSslSocket::sslLibraryVersionString().isEmpty());
#endif
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
        QCOMPARE(w.context->minimum(),0);w.context->setValue(0);w.launchFit();QTRY_COMPARE(w.fit.state(),QProcess::NotRunning);QCOMPARE(w.config.find("--ctx-size")->value.toInt(),32768);QCOMPARE(w.config.fitContext,0);
        w.launchServer(); QTRY_COMPARE(w.server.state(), QProcess::Running);
        QTRY_VERIFY(w.status->text().contains(QString::fromUtf8("Сервер готов")));
        QTRY_COMPARE(w.performance.snapshot().promptSpeed,1000.0);QCOMPARE(w.performance.snapshot().generationSpeed,50.0);
        answerExit(false);QVERIFY(!w.close());QCOMPARE(w.server.state(),QProcess::Running);
        QVERIFY(w.runningCommand->toPlainText().contains(model.fileName().replace('/', QDir::separator())) || w.runningCommand->toPlainText().contains(model.fileName()));
        w.stopping = true; w.stop(w.server); QTRY_COMPARE(w.server.state(), QProcess::NotRunning);
        QVERIFY(w.startButton->isEnabled());
        QFile bad(dir.filePath("failure.gguf")); QVERIFY(bad.open(QIODevice::WriteOnly)); bad.close();
        w.config.find("--model")->value = bad.fileName(); auto before = w.config.save(); w.launchFit();
        QTRY_COMPARE(w.fit.state(), QProcess::NotRunning);
        QCOMPARE(w.config.save(), before); QCOMPARE(w.status->text(), QString::fromUtf8("Ошибка подбора"));
        answerExit(true);w.close();
    }
};
QTEST_MAIN(GuiTest)
#include "test_gui.moc"
