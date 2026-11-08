#include "mainwindow.h"
#include "advisordialog.h"
#include "builderpage.h"
#include "modellibrary.h"
#include "modelpicker.h"
#include "monitordialog.h"
#include "version.h"
#include "studiobutton.h"
#include <QtWidgets>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>

namespace {
QString groupLabel(const QString &group) {
    if (group == "Common params") return "Общие параметры";
    if (group == "Sampling params") return "Сэмплирование";
    if (group == "Server-specific params") return "Параметры сервера";
    return group;
}
bool modelField(const QString &s) { return QStringList{"--model", "--mmproj", "--spec-draft-model", "--lora", "--lora-scaled", "--control-vector", "--control-vector-scaled", "--models-dir", "--models-preset"}.contains(s); }
QPushButton *button(const QString &text, QLayout *layout, const QString &name = {}) {
    auto b = new StudioButton(text); b->setObjectName(name); layout->addWidget(b); return b;
}
}
MainWindow::MainWindow(QWidget *parent, bool checkUpdatesOnStartup,bool restoreLastProfile,const QString &powerExecutable) : QMainWindow(parent),power(this,powerExecutable) {
    QFile catalog(QCoreApplication::applicationDirPath() + "/parameters.json");
    if (!catalog.exists()) catalog.setFileName(":/parameters.json");
    QString message;
    if (!catalog.open(QIODevice::ReadOnly) || !config.load(catalog.readAll(), &message)) {
        QFile fallback(":/parameters.json"); fallback.open(QIODevice::ReadOnly); config.load(fallback.readAll(), &message);
        QTimer::singleShot(0, this, [this, message] { error("Каталог рядом с программой не прочитан. Загружен встроенный. " + message); });
    }
    QString profileWarning,restoredPath;
    if(restoreLastProfile){const auto path=QSettings().value("lastProfilePath").toString();if(!path.isEmpty()){QFile file(path);Config next;QString reason;if(!file.open(QIODevice::ReadOnly))reason=file.errorString();else if(next.load(file.readAll(),&reason)){config=next;restoredPath=path;}if(!reason.isEmpty())profileWarning="Не удалось загрузить последний профиль "+path+": "+reason;}}
    if (config.directory.isEmpty() && QDir("C:/llama.cpp").exists()) config.directory = "C:/llama.cpp";
    buildUi(); rebuild(); updatePreview();
    power.profileLimits=config.metadata.value("gpuPowerLimits").toObject();
    connect(&power,&GpuPowerService::limitApplied,this,[this](const QString &uuid,double watts){auto limits=config.metadata.value("gpuPowerLimits").toObject();limits[uuid]=watts;config.metadata["gpuPowerLimits"]=limits;const auto path=windowFilePath();if(path.isEmpty()){log("[Лимит GPU обновлён. Сохраните JSON-профиль кнопкой «Сохранить как…».]\n");return;}QFile original(path);Config disk;QString reason;if(!original.open(QIODevice::ReadOnly)||!disk.load(original.readAll(),&reason)){error("Лимит GPU применён, но не сохранён в профиль: "+(reason.isEmpty()?original.errorString():reason));return;}original.close();disk.metadata["gpuPowerLimits"]=limits;QSaveFile file(path);const auto bytes=disk.save();if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())error("Лимит GPU применён, но не сохранён в профиль: "+file.errorString());});
    connect(&power,&GpuPowerService::profileFinished,this,[this](bool success,const QString &message){if(!launchAfterPower)return;launchAfterPower=false;if(success&&!closeConfirmed){powerReady=true;launchServer();powerReady=false;}else{status->setText("Лимиты GPU не применены");log("["+message+"]\n");}updateButtons();});
    connect(&power,&GpuPowerService::resetFinished,this,[this](bool success,const QString &message){if(!powerResetStarted)return;if(success){powerResetDone=true;QTimer::singleShot(0,this,&QWidget::close);return;}
        log("[Не удалось сбросить лимиты GPU: "+message+"]\n");QMessageBox question(QMessageBox::Warning,"Не удалось сбросить мощность GPU","Драйвер не подтвердил заводские лимиты. Можно остаться в программе для повторной попытки или выйти без сброса. При выходе изменённые лимиты могут сохраниться.\n\n"+message,QMessageBox::NoButton,this);question.setObjectName("powerResetExitChoice");auto leave=new StudioButton("Выйти без сброса");leave->setObjectName("exitWithoutPowerReset");auto stay=new StudioButton("Остаться");stay->setObjectName("stayAfterPowerResetFailure");question.addButton(leave,QMessageBox::DestructiveRole);question.addButton(stay,QMessageBox::RejectRole);question.setDefaultButton(stay);question.setEscapeButton(stay);question.exec();
        if(question.clickedButton()==leave){powerResetDone=true;QTimer::singleShot(0,this,&QWidget::close);}else{powerResetStarted=false;closeConfirmed=false;centralWidget()->setEnabled(true);for(auto dialog:findChildren<QDialog*>())dialog->setEnabled(true);updateButtons();}});
    if(!restoredPath.isEmpty()){setWindowFilePath(restoredPath);log("[Загружен последний профиль: "+restoredPath+"]\n");}
    if(!profileWarning.isEmpty()){log("["+profileWarning+"]\n");status->setText("Профиль недоступен · настройки по умолчанию");status->setToolTip(profileWarning);}
    server.setProcessChannelMode(QProcess::SeparateChannels);
    fit.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&server, &QProcess::readyReadStandardOutput, this, [this] { readLog(server, true, serverPending); });
    connect(&server, &QProcess::readyReadStandardError, this, [this] { readLog(server, false, serverErrorPending); });
    connect(&server, &QProcess::started, this, [this] { status->setText("Загрузка модели…"); healthTimer.start(); updateButtons(); });
    connect(&server, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus exit) {
        healthTimer.stop(); readLog(server, true, serverPending, true); readLog(server, false, serverErrorPending, true);
        performance.stop();
        status->setText(stopping ? "Сервер остановлен" : (code == 0 && exit == QProcess::NormalExit ? "Сервер завершён" : "Ошибка сервера"));
        log(QString("\n[Сервер завершён: код %1, %2]\n").arg(code).arg(exit == QProcess::CrashExit ? "аварийный выход" : "обычный выход"));
        updateButtons();
    });
    connect(&server, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        log("\n[Ошибка процесса] " + server.errorString() + "\n");
        if (e == QProcess::FailedToStart) {performance.stop();healthTimer.stop(); status->setText("Ошибка запуска"); updateButtons(); }
    });
    connect(&fit, &QProcess::readyReadStandardOutput, this, [this] { readLog(fit, true, fitPending); });
    connect(&fit, &QProcess::readyReadStandardError, this, [this] { readLog(fit, false, fitErrorPending); });
    connect(&fit, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int code, QProcess::ExitStatus exit) {
        readLog(fit, true, fitPending, true); readLog(fit, false, fitErrorPending, true);
        if (fitCanceled) { status->setText("Подбор отменён"); }
        else if (code == 0 && exit == QProcess::NormalExit) {
            QString message;
            if (config.applyFit(QString::fromUtf8(fitOutput), &message)) {
                rebuild(); updatePreview(); status->setText("Параметры подобраны");
                log("\n[Результат fit применён к конфигурации. Сервер ещё не запущен.]\n");
            } else { status->setText("Ошибка результата fit"); error(message); }
        } else { status->setText("Ошибка подбора"); log("\n[Подбор не выполнен, код " + QString::number(code) + "]\n"); }
        editor->setEnabled(true); directory->setEnabled(true); context->setEnabled(true); updateButtons();
    });
    connect(&fit, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        log("\n[Ошибка fit] " + fit.errorString() + "\n");
        if (e == QProcess::FailedToStart) { status->setText("Ошибка запуска fit"); editor->setEnabled(true); directory->setEnabled(true); context->setEnabled(true); updateButtons(); }
    });
    healthTimer.setInterval(1000); connect(&healthTimer, &QTimer::timeout, this, &MainWindow::pollHealth);
    updateButtons();
    if (checkUpdatesOnStartup) QTimer::singleShot(500, this, [this] { releases.check(config.directory); });
}
void MainWindow::buildUi() {
    setWindowTitle(QString("Llama Studio %1 · локальный сервер").arg(STUDIO_VERSION));setWindowIcon(QIcon(":/app-icon.png")); resize(1280, 900); setMinimumSize(960, 680);
    auto root = new QWidget; setCentralWidget(root); auto main = new QVBoxLayout(root); main->setContentsMargins(24, 20, 24, 20); main->setSpacing(14);
    auto header = new QHBoxLayout;
    auto emblem=new QLabel;emblem->setPixmap(QPixmap(":/app-icon.png").scaled(58,58,Qt::KeepAspectRatio,Qt::SmoothTransformation));header->addWidget(emblem);
    auto titles = new QVBoxLayout;auto titleRow=new QHBoxLayout;auto title = new QLabel("Llama Studio"); title->setObjectName("title");titleRow->addWidget(title);auto version=new QLabel(STUDIO_VERSION);version->setObjectName("version");titleRow->addWidget(version);titleRow->addStretch();titles->addLayout(titleRow);
    auto sub = new QLabel("Локальные модели. Полный контроль над запуском."); sub->setObjectName("subtitle"); titles->addWidget(sub); header->addLayout(titles); header->addStretch();
    status = new QLabel("Сервер остановлен"); status->setObjectName("status"); header->addWidget(status); main->addLayout(header);
    auto toolbar = new QHBoxLayout;
    connect(button("Открыть конфиг", toolbar), &QPushButton::clicked, this, [this] { loadFile(); });
    connect(button("Сохранить как…", toolbar), &QPushButton::clicked, this, &MainWindow::saveFile);
    connect(button("Загрузить каталог JSON", toolbar), &QPushButton::clicked, this, [this] { loadFile(true); });
    connect(button("Сбросить", toolbar), &QPushButton::clicked, this, &MainWindow::reset);
    connect(button("Помощник настройки", toolbar), &QPushButton::clicked, this, &MainWindow::showAdvisor); toolbar->addStretch(); main->addLayout(toolbar);
    auto updates = new QHBoxLayout;
    connect(button("Каталог моделей",updates,"modelLibraryButton"),&QPushButton::clicked,this,&MainWindow::showModels);
    connect(button("Мониторинг",updates,"hardwareMonitorButton"),&QPushButton::clicked,this,&MainWindow::showMonitor);
    auto releaseStatus = new QLabel("llama.cpp: релизы GitHub"); releaseStatus->setObjectName("subtitle"); releaseStatus->setWordWrap(true); releaseStatus->setTextFormat(Qt::PlainText); updates->addWidget(releaseStatus, 1);
    auto checkReleases = button("Проверить обновления", updates);
    auto openRelease = button("Открыть релиз", updates); openRelease->setEnabled(false);
    connect(checkReleases, &QPushButton::clicked, this, [this] { releases.check(config.directory); });
    connect(openRelease, &QPushButton::clicked, this, [this] { if (!releases.releaseUrl().isEmpty()) QDesktopServices::openUrl(releases.releaseUrl()); });
    connect(&releases, &ReleaseChecker::checkingChanged, this, [checkReleases](bool busy) { checkReleases->setEnabled(!busy); });
    connect(&releases, &ReleaseChecker::resultChanged, this, [releaseStatus, openRelease](const QString &text, const QString &details, const QUrl &url) { releaseStatus->setText(text); releaseStatus->setToolTip(details); openRelease->setEnabled(!url.isEmpty()); });
    main->addLayout(updates);
    auto paths = new QFrame; paths->setObjectName("card"); auto pl = new QHBoxLayout(paths); pl->setContentsMargins(14, 12, 14, 12);
    pl->addWidget(new QLabel("Папка llama.cpp")); directory = new QLineEdit(config.directory); directory->setPlaceholderText("Папка с llama-server и llama-fit-params, либо корень сборки"); pl->addWidget(directory, 1);
    auto browse = button("Обзор…", pl); connect(browse, &QPushButton::clicked, this, [this] {
        auto p = QFileDialog::getExistingDirectory(this, "Папка llama.cpp", directory->text()); if (!p.isEmpty()) directory->setText(p);
    });
    connect(directory, &QLineEdit::textChanged, this, [this](const QString &s) { config.directory = s; updatePreview(); }); main->addWidget(paths);
    tabs = new QTabWidget; main->addWidget(tabs, 1);
    editor = new QWidget; auto el = new QVBoxLayout(editor); el->setContentsMargins(0, 0, 0, 0);
    auto filter = new QHBoxLayout; search = new QLineEdit; search->setClearButtonEnabled(true); search->setPlaceholderText("Поиск по имени, аргументу или описанию…"); filter->addWidget(search); count = new QLabel; filter->addWidget(count); el->addLayout(filter);
    auto split = new QSplitter; navigation = new QListWidget; navigation->setMaximumWidth(240); navigation->setMinimumWidth(210);
    navigation->addItem("Частые параметры"); navigation->addItem("Модели и адаптеры");
    for (auto g : config.metadata.value("groups").toArray()) navigation->addItem(groupLabel(g.toString()));
    navigation->addItem("Включённые параметры"); split->addWidget(navigation);
    scroll = new QScrollArea; scroll->setWidgetResizable(true); split->addWidget(scroll); split->setStretchFactor(1, 1); el->addWidget(split, 1);
    el->addWidget(new QLabel("Команда следующего запуска · отмечайте только параметры, которые нужно передать"));
    preview = new QPlainTextEdit; preview->setReadOnly(true); preview->setMaximumHeight(85); el->addWidget(preview);
    auto cp = new QHBoxLayout; cp->addStretch(); connect(button("Копировать команду", cp), &QPushButton::clicked, this, [this] {
        QStringList args; QString message; if (!config.arguments(&args, &message)) { error(message); return; }
        auto exe = findExecutable(config.directory, false); QApplication::clipboard()->setText(Config::displayCommand(exe.path.isEmpty() ? "llama-server" : exe.path, exe.prefix + args, false));
    }); el->addLayout(cp); tabs->addTab(editor, "Параметры запуска");
    auto page = new QWidget; auto cl = new QVBoxLayout(page); cl->setContentsMargins(0, 0, 0, 0);
    cl->addWidget(new QLabel("Фактическая команда текущего / последнего процесса (ключи скрыты)")); runningCommand = new QPlainTextEdit; runningCommand->setReadOnly(true); runningCommand->setMaximumHeight(92); cl->addWidget(runningCommand);
    auto logbar = new QHBoxLayout; logbar->addWidget(new QLabel("Журнал сервера и подбора")); logbar->addStretch();
    connect(button("Сохранить журнал…", logbar), &QPushButton::clicked, this, [this] {
        auto path = QFileDialog::getSaveFileName(this, "Сохранить журнал", "llama-server.log", "Log (*.log);;Все файлы (*)"); if (path.isEmpty()) return;
        QSaveFile f(path); if (!f.open(QIODevice::WriteOnly) || f.write(console->toPlainText().toUtf8()) < 0 || !f.commit()) error("Не удалось сохранить журнал");
    }); connect(button("Очистить", logbar), &QPushButton::clicked, this, [this] { console->clear(); }); cl->addLayout(logbar);
    console = new QPlainTextEdit; console->setReadOnly(true); console->setMaximumBlockCount(10000); cl->addWidget(console, 1); tabs->addTab(page, "Сервер · журнал");
    buildPage=new BuilderPage;buildPage->loadSettings(config.metadata.value("build").toObject());tabs->addTab(buildPage,"Сборка llama.cpp");
    config.metadata["build"]=buildPage->settings().toJson();
    connect(buildPage,&BuilderPage::settingsChanged,this,[this](const QJsonObject &s){config.metadata["build"]=s;});
    connect(buildPage,&BuilderPage::startRequested,this,[this]{if(server.state()!=QProcess::NotRunning||fit.state()!=QProcess::NotRunning){error("Остановите сервер и fit перед обновлением бинарников.");return;}buildPage->start();});
    connect(buildPage,&BuilderPage::busyChanged,this,[this](bool busy){directory->setEnabled(!busy);updateButtons();if(!busy&&closeAfterBuild)QTimer::singleShot(0,this,&QWidget::close);});
    connect(buildPage,&BuilderPage::deployed,this,[this](const QString &path){directory->setText(path);status->setText("llama.cpp обновлён");});
    auto bottom = new QHBoxLayout; bottom->addWidget(new QLabel("Контекст для fit:")); context = new QSpinBox; context->setRange(0, 2147483647);context->setSpecialValueText("0 — авто / максимум модели");context->setToolTip("0 передаёт --ctx-size 0: fit выбирает контекст до предела модели с учётом памяти. Для фиксированного контекста задайте положительное число."); context->setValue(config.fitContext); context->setSingleStep(1024); bottom->addWidget(context);
    connect(context, qOverload<int>(&QSpinBox::valueChanged), this, [this](int n) { config.fitContext = n; });
    fitButton = button("Подобрать параметры", bottom); fitButton->setToolTip("Подбор размещения в памяти для указанного контекста. Не измеряет скорость. Дополнительные модели могут потребовать отдельный запас памяти.");
    cancelFitButton = button("Отменить fit", bottom); bottom->addStretch();
    openButton = button("Открыть Web UI", bottom); stopButton = button("Остановить", bottom, "danger"); startButton = button("▶  Запустить сервер", bottom, "primary"); main->addLayout(bottom);
    connect(startButton, &QPushButton::clicked, this, &MainWindow::launchServer);
    connect(stopButton, &QPushButton::clicked, this, [this] { stopping = true; healthTimer.stop(); status->setText("Остановка…"); stop(server); });
    connect(fitButton, &QPushButton::clicked, this, &MainWindow::launchFit);
    connect(cancelFitButton, &QPushButton::clicked, this, [this] { fitCanceled = true; stop(fit); });
    connect(openButton, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(runningUrl); });
    navigation->setCurrentRow(0);
    connect(navigation, &QListWidget::currentRowChanged, this, [this] { rebuild(); });
    connect(search, &QLineEdit::textChanged, this, [this] { rebuild(); });
}
void MainWindow::rebuild() {
    auto body = new QWidget; auto rows = new QVBoxLayout(body); rows->setContentsMargins(12, 0, 6, 0); rows->setSpacing(9);
    int shown = 0; QString query = search->text().trimmed(); int section = navigation->currentRow();
    auto groups = config.metadata.value("groups").toArray();
    for (int i = 0; i < config.parameters.size(); ++i) {
        auto &p = config.parameters[i];
        bool visible = section == 0 ? p.priority < 100 : section == 1 ? modelField(p.name) : section == navigation->count() - 1 ? p.enabled : section >= 2 && section - 2 < groups.size() && p.group == groups[section - 2].toString();
        QString desc = p.definition.value("description").toString();
        if (!query.isEmpty()) visible = (p.name + " " + p.definition.value("label").toString() + " " + desc).contains(query, Qt::CaseInsensitive);
        if (!visible) continue;
        ++shown;
        auto card = new QFrame; card->setObjectName("card"); auto layout = new QVBoxLayout(card); layout->setContentsMargins(14, 10, 14, 10);
        auto top = new QHBoxLayout; auto enabled = new QCheckBox(p.definition.value("label").toString(p.name)); enabled->setChecked(p.enabled); top->addWidget(enabled);
        auto flag = new QLabel(p.name); flag->setObjectName("subtitle"); top->addWidget(flag); top->addStretch();
        auto value = new QWidget; auto vl = new QHBoxLayout(value); vl->setContentsMargins(0, 0, 0, 0); value->setMinimumWidth(250); value->setMaximumWidth(440);
        auto change = [this, i](QJsonValue v) { config.parameters[i].value = v; updatePreview(); };
        switch (p.type) {
        case DataType::Boolean: {
            auto combo = new QComboBox; combo->addItem("Включено", true); combo->addItem("Выключено", false); combo->setCurrentIndex(p.value.isNull() || p.value.toBool() ? 0 : 1); vl->addWidget(combo);
            connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [combo, change](int) { change(combo->currentData().toBool()); }); break;
        }
        case DataType::Integer: {
            auto spin = new QSpinBox; spin->setRange(-2147483647, 2147483647); spin->setValue(p.value.toInt()); vl->addWidget(spin);
            connect(spin, qOverload<int>(&QSpinBox::valueChanged), this, [change](int n) { change(n); }); break;
        }
        case DataType::Number: {
            auto spin = new QDoubleSpinBox; spin->setRange(-1e12, 1e12); spin->setDecimals(6); spin->setSingleStep(0.05); spin->setValue(p.value.toDouble()); vl->addWidget(spin);
            connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [change](double n) { change(n); }); break;
        }
        case DataType::Choice: {
            auto combo = new QComboBox; combo->addItem("Выберите значение…");
            for (auto c : p.definition.value("choices").toArray()) combo->addItem(p.definition.value("choiceLabels").toObject().value(c.toString()).toString(c.toString()), c.toString());
            combo->setCurrentIndex(p.value.isNull() ? 0 : combo->findData(p.value.toString())); vl->addWidget(combo);
            connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [combo, change](int n) { change(n == 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(combo->currentData().toString())); }); break;
        }
        default: {
            auto line = new QLineEdit(p.value.toString());line->setObjectName("parameter:"+p.name); vl->addWidget(line, 1);
            if (p.name == "--api-key" || p.name == "--hf-token") line->setEchoMode(QLineEdit::Password);
            line->setPlaceholderText(p.value.isNull() ? "Значение наследуется из llama.cpp" : "Значение");
            connect(line, &QLineEdit::textChanged, this, [change](const QString &s) { change(s); });
            if (p.type == DataType::Path) {
                auto browse = button("…", vl);browse->setObjectName("browse:"+p.name); browse->setMaximumWidth(38);
                connect(browse, &QPushButton::clicked, this, [this, line, i] {
                    const auto name = config.parameters[i].name;
                    bool dir = name.endsWith("-dir") || name == "--models-dir" || name == "--slot-save-path" || name == "--media-path" || name == "--path";
                    QString path;
                    if (dir) path = QFileDialog::getExistingDirectory(this, "Выбрать папку", line->text());
                    else if(QStringList{"--model","--mmproj","--spec-draft-model"}.contains(name))path=ModelPickerDialog::choose(this,"Выбрать модель · "+name,line->text());
                    else if (name == "--lora" || name == "--control-vector") { auto list = QFileDialog::getOpenFileNames(this, "Выбрать файлы", line->text(), "Модели (*.gguf);;Все файлы (*)"); path = list.join(','); }
                    else path = QFileDialog::getOpenFileName(this, "Выбрать файл", line->text(), "Все файлы (*);;Модели (*.gguf)");
                    if (!path.isEmpty()) line->setText(QDir::toNativeSeparators(path));
                });
            }
            break;
        }
        }
        bool unavailable = p.definition.value("unavailable").toBool() || p.definition.value("action").toBool();
        enabled->setEnabled(!unavailable); value->setEnabled(p.enabled && !unavailable);
        connect(enabled, &QCheckBox::toggled, this, [this, i, value](bool on) {
            auto &p = config.parameters[i]; p.enabled = on;
            if (on && p.value.isNull() && (p.type == DataType::Integer || p.type == DataType::Number)) p.value = 0;
            if (on && p.value.isNull() && p.type == DataType::Boolean) p.value = true;
            value->setEnabled(on); updatePreview();
        });
        top->addWidget(value, 1); layout->addLayout(top);
        QString brief = desc.section('\n', 0, 0); if (brief.size() > 190) brief = brief.left(187) + "…";
        auto explanation = new QLabel((unavailable ? "[Служебный / устаревший] " : "") + brief); explanation->setWordWrap(true); explanation->setObjectName("subtitle"); explanation->setTextFormat(Qt::PlainText); layout->addWidget(explanation);
        auto defaultValue=p.definition.value("default");
        QString defaultText=defaultValue.isNull()?"наследуется из llama.cpp / модели":defaultValue.isBool()?(defaultValue.toBool()?"включено":"выключено"):defaultValue.toVariant().toString();
        if(p.type==DataType::Choice)defaultText=p.definition.value("choiceLabels").toObject().value(defaultText).toString(defaultText);
        auto guidance=p.definition.value("guidance").toObject();
        QString tips=guidance.isEmpty()?QString():"\n\nДля чата: "+guidance.value("chat").toString()+"\nДля агентского кода: "+guidance.value("agent").toString();
        card->setToolTip(desc + "\n\nГруппа: " + groupLabel(p.group) + "\nПо умолчанию: " + defaultText + "\nПриоритет: " + QString::number(p.priority)+tips); rows->addWidget(card);
    }
    if (!shown) rows->addWidget(new QLabel("Параметры не найдены. Измените поиск или выберите другую группу."));
    rows->addStretch(); auto old = scroll->takeWidget(); scroll->setWidget(body); if (old) old->deleteLater(); count->setText(QString("%1 / %2").arg(shown).arg(config.parameters.size()));
}
void MainWindow::updatePreview() {
    QStringList args; QString message;
    if (!config.arguments(&args, &message)) { preview->setPlainText(message); return; }
    auto exe = findExecutable(config.directory, false);
    preview->setPlainText(Config::displayCommand(exe.path.isEmpty() ? "llama-server" : exe.path, exe.prefix + args));
}
void MainWindow::updateButtons() {
    bool s = server.state() != QProcess::NotRunning, f = fit.state() != QProcess::NotRunning;
    bool building=buildPage&&buildPage->busy();startButton->setEnabled(!s && !f&&!building&&!launchAfterPower); stopButton->setEnabled(s); fitButton->setEnabled(!s && !f&&!building&&!launchAfterPower); cancelFitButton->setEnabled(f); openButton->setEnabled(s && !stopping);
}
void MainWindow::error(const QString &text) { QMessageBox::warning(this, "Llama Studio", text); }
void MainWindow::log(const QString &text) { auto cursor = console->textCursor(); cursor.movePosition(QTextCursor::End); cursor.insertText(text); console->setTextCursor(cursor); console->ensureCursorVisible(); }
void MainWindow::readLog(QProcess &process, bool out, QByteArray &pending, bool finish) {
    QByteArray data = out ? process.readAllStandardOutput() : process.readAllStandardError();
    if (&process == &fit && out) {
        if (fitOutput.size() + data.size() <= 8 * 1024 * 1024) fitOutput += data;
        else { fitCanceled = true; stop(fit); log("\n[Слишком большой вывод fit; подбор отменён]\n"); }
    }
    pending += data; int end;
    while ((end = pending.indexOf('\n')) >= 0) {auto line=QString::fromUtf8(pending.left(end+1));if(&process==&server)performance.readLine(line);log(line);pending.remove(0,end+1);}
    if (finish || pending.size() > 65536) {auto line=QString::fromUtf8(pending);if(&process==&server)performance.readLine(line);log(line);pending.clear();}
}
void MainWindow::loadFile(bool catalog) {
    if (fit.state() != QProcess::NotRunning||buildPage->busy()) { error("Дождитесь завершения подбора/сборки или отмените их."); return; }
    auto path = QFileDialog::getOpenFileName(this, catalog ? "Каталог параметров" : "Конфигурация", {}, "JSON (*.json)"); if (path.isEmpty()) return;
    QString message;if(!loadProfile(path,&message,catalog))error(message);
}
bool MainWindow::loadProfile(const QString &path,QString *message,bool catalog){
    if(fit.state()!=QProcess::NotRunning||buildPage->busy()){if(message)*message="Дождитесь завершения подбора/сборки.";return false;}
    QFile file(path);Config next;QString reason;if(!file.open(QIODevice::ReadOnly))reason=file.errorString();else if(next.load(file.readAll(),&reason))reason.clear();else if(reason.isEmpty())reason="Неверный JSON профиля.";
    if(!reason.isEmpty()){if(message)*message="Не удалось загрузить профиль: "+reason;return false;}
    if (catalog) { next.directory = config.directory; next.fitContext = config.fitContext;next.metadata["build"]=config.metadata.value("build");next.metadata["gpuPowerLimits"]=config.metadata.value("gpuPowerLimits"); }
    config = next; directory->setText(config.directory); context->setValue(config.fitContext);
    power.profileLimits=config.metadata.value("gpuPowerLimits").toObject();
    buildPage->loadSettings(config.metadata.value("build").toObject());
    config.metadata["build"]=buildPage->settings().toJson();
    navigation->blockSignals(true); navigation->clear(); navigation->addItem("Частые параметры"); navigation->addItem("Модели и адаптеры");
    for (auto group : config.metadata.value("groups").toArray()) navigation->addItem(groupLabel(group.toString()));
    navigation->addItem("Включённые параметры"); navigation->setCurrentRow(0); navigation->blockSignals(false); rebuild(); updatePreview();
    if(!catalog){const auto absolute=QFileInfo(path).absoluteFilePath();QSettings settings;settings.setValue("lastProfilePath",absolute);settings.sync();setWindowFilePath(absolute);}return true;
}
void MainWindow::saveFile() {
    auto path = QFileDialog::getSaveFileName(this, "Сохранить конфигурацию", "llama-config.json", "JSON (*.json)"); if (path.isEmpty()) return;
    QString message;if(!saveProfile(path,&message))error(message);
}
bool MainWindow::saveProfile(const QString &path,QString *message){QSaveFile file(path);const auto data=config.save();if(!file.open(QIODevice::WriteOnly)||file.write(data)!=data.size()||!file.commit()){if(message)*message="Не удалось сохранить конфигурацию: "+file.errorString();return false;}const auto absolute=QFileInfo(path).absoluteFilePath();QSettings settings;settings.setValue("lastProfilePath",absolute);settings.sync();setWindowFilePath(absolute);return true;}
void MainWindow::reset() {
    if (fit.state() != QProcess::NotRunning) return;
    for (auto &p : config.parameters) { p.value = p.definition.value("default"); p.enabled = false; }
    rebuild(); updatePreview();
}
void MainWindow::showAdvisor() {
    if(server.state()!=QProcess::NotRunning || fit.state()!=QProcess::NotRunning||buildPage->busy()){error("Остановите сервер и дождитесь завершения fit/сборки перед настройкой.");return;}
    AdvisorDialog dialog(config,this);
    if(dialog.exec()!=QDialog::Accepted)return;
    QString message;
    if(!dialog.recommendation.apply(&config,&message)){error(message);return;}
    config.metadata["advisor"]=dialog.options();
    context->setValue(config.fitContext);rebuild();updatePreview();
    if(dialog.runFit)launchFit();
}
void MainWindow::showModels(){
    if(auto existing=findChild<ModelLibraryDialog*>("modelLibraryWindow")){existing->showNormal();existing->raise();existing->activateWindow();return;}
    QSettings preferences;auto folder=config.metadata.value("modelLibraryDirectory").toString(preferences.value("modelLibraryDirectory").toString());
    auto dialog=new ModelLibraryDialog(folder,this);dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog,&ModelLibraryDialog::directoryChanged,this,[this](const QString &path){config.metadata["modelLibraryDirectory"]=path;QSettings().setValue("modelLibraryDirectory",path);});
    connect(dialog,&ModelLibraryDialog::modelSelected,this,[this,dialog](const QString &path,const QString &name){
        if(server.state()!=QProcess::NotRunning||fit.state()!=QProcess::NotRunning||buildPage->busy()){QMessageBox::information(dialog,"Выбор модели","Остановите сервер и дождитесь завершения fit/сборки перед сменой модели.");return;}
        if(auto p=config.find(name)){p->value=path;p->enabled=true;navigation->setCurrentRow(1);rebuild();updatePreview();status->setText("Выбрано "+name+": "+QFileInfo(path).fileName());}
    });dialog->show();
}
void MainWindow::showMonitor(){
    if(auto existing=findChild<MonitorDialog*>("hardwareMonitorWindow")){existing->showNormal();existing->raise();existing->activateWindow();return;}
    auto dialog=new MonitorDialog(this,&performance,&power);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->show();
}
bool MainWindow::validateModel() {
    auto p = config.find("--model");
    if (!p || !p->enabled || !QFileInfo(p->value.toString()).isFile()) { error("В разделе «Модели и адаптеры» включите --model и выберите существующий GGUF-файл."); return false; }
    return true;
}
QUrl MainWindow::endpoint() const {
    auto val = [this](const QString &name, const QString &fallback) { auto p = config.find(name); return p && p->enabled ? p->value.toVariant().toString() : fallback; };
    QString host = val("--host", "127.0.0.1").section(',', 0, 0);
    if (host == "0.0.0.0") host = "127.0.0.1";
    if (host == "::" || host == "[::]") host = "::1";
    QUrl url; url.setScheme(config.find("--ssl-cert-file") && config.find("--ssl-cert-file")->enabled ? "https" : "http"); url.setHost(host); url.setPort(val("--port", "9931").toInt()); url.setPath(val("--api-prefix", "") + "/"); return url;
}
void MainWindow::launchServer() {
    if (server.state() != QProcess::NotRunning || fit.state() != QProcess::NotRunning||buildPage->busy()) return;
    bool router = config.find("--models-dir") && config.find("--models-dir")->enabled;
    bool remote = (config.find("--hf-repo") && config.find("--hf-repo")->enabled) || (config.find("--model-url") && config.find("--model-url")->enabled);
    if (!router && !remote && !validateModel()) return;
    auto exe = findExecutable(config.directory, false);
    if (exe.path.isEmpty()) { error("Не найден llama-server. Выберите папку с готовыми бинарными файлами или корень llama.cpp со сборкой build/bin."); return; }
    const auto limits=config.metadata.value("gpuPowerLimits").toObject();if(!powerReady&&!limits.isEmpty()){if(launchAfterPower)return;launchAfterPower=true;power.applyProfile(limits);status->setText("Применение лимитов GPU…");updateButtons();return;}
    QStringList args; QString message; if (!config.arguments(&args, &message)) { error(message); return; }
    args = exe.prefix + args; runningUrl = endpoint(); runningKey.clear();
    if (auto key = config.find("--api-key")) if (key->enabled) runningKey = key->value.toString().section(',', 0, 0).toUtf8();
    runningCommand->setPlainText(Config::displayCommand(exe.path, args)); tabs->setCurrentIndex(1); serverPending.clear(); serverErrorPending.clear(); stopping = false; ++serverRun;
    QString modelName="llama.cpp";if(router)modelName="Router llama.cpp";else if(auto p=config.find("--model")){if(p->enabled)modelName=QFileInfo(p->value.toString()).fileName();}if(remote)modelName="Удалённая модель llama.cpp";performance.start(modelName);
    server.setWorkingDirectory(QFileInfo(exe.path).absolutePath()); server.setProgram(exe.path); server.setArguments(args); status->setText("Запуск сервера…"); log("\n[Запуск сервера]\n"); server.start(); updateButtons();
}
void MainWindow::launchFit() {
    if (server.state() != QProcess::NotRunning || fit.state() != QProcess::NotRunning||buildPage->busy() || !validateModel()) return;
    auto exe = findExecutable(config.directory, true); if (exe.path.isEmpty()) { error("Не найден llama-fit-params (или llama fit-params). Установите сборку llama.cpp с этой утилитой."); return; }
    QStringList args = exe.prefix; args << "--model" << config.find("--model")->value.toString() << "--ctx-size" << QString::number(config.fitContext);
    // Only model-placement arguments accepted by the fitting utility. GPU layers and
    // tensor overrides are deliberately unset so that a previous fit cannot pin them.
    for (auto name : {"--device", "--split-mode", "--main-gpu", "--fit-target", "--cache-type-k", "--cache-type-v", "--batch-size", "--ubatch-size", "--parallel", "--flash-attn"}) {
        if (auto p = config.find(name)) if (p->enabled && !p->value.isNull()) args << name << p->value.toVariant().toString();
    }
    bool extras = false; for (auto name : {"--mmproj", "--spec-draft-model", "--lora"}) if (auto p = config.find(name)) extras |= p->enabled;
    if (extras) log("\n[fit оценивает основную модель. Проектор, draft-модель и LoRA могут потребовать дополнительную память; увеличьте --fit-target.]\n");
    fitOutput.clear(); fitPending.clear(); fitErrorPending.clear(); fitCanceled = false; ++fitRun;
    runningCommand->setPlainText(Config::displayCommand(exe.path, args)); tabs->setCurrentIndex(1); editor->setEnabled(false); directory->setEnabled(false); context->setEnabled(false);
    fit.setWorkingDirectory(QFileInfo(exe.path).absolutePath()); fit.setProgram(exe.path); fit.setArguments(args); status->setText("Подбор по памяти…"); log("\n[Подбор параметров]\n"); fit.start(); updateButtons();
}
void MainWindow::stop(QProcess &process) {
    if (process.state() == QProcess::NotRunning) return;
    process.terminate(); auto ptr = &process; quint64 run = ptr == &server ? serverRun : fitRun;
    QTimer::singleShot(5000, this, [this, ptr, run] { if (run == (ptr == &server ? serverRun : fitRun) && ptr->state() != QProcess::NotRunning)static_cast<ManagedProcess*>(ptr)->killTree(); });
}
void MainWindow::pollHealth() {
    if (healthPending || server.state() != QProcess::Running || stopping) return;
    auto url = runningUrl; url.setPath(runningUrl.path() + "health"); QNetworkRequest request(url);
    if (!runningKey.isEmpty()) request.setRawHeader("Authorization", "Bearer " + runningKey);
    auto reply = network.get(request); healthPending = true; auto run = serverRun;
    QTimer::singleShot(2000, reply, [reply] { if (reply->isRunning()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, run] {
        healthPending = false;
        if (run == serverRun && server.state() == QProcess::Running && !stopping) {
            auto code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            status->setText(code == 200 ? "Сервер готов · " + QString::number(runningUrl.port()) : code == 401 ? "Сервер запущен · нужна авторизация" : "Сервер запущен · ожидание готовности");
        }
        reply->deleteLater();
    });
}
void MainWindow::closeEvent(QCloseEvent *event) {
    if(closeQuestionOpen){event->ignore();return;}
    if(!closeConfirmed){
        closeQuestionOpen=true;
        QMessageBox question(QMessageBox::Question,"Завершить работу","Закрыть Llama Studio? Сервер и fit будут остановлены, модель выгружена, активная сборка отменена.",QMessageBox::NoButton,this);question.setObjectName("exitConfirmation");
        auto yes=new StudioButton("Да");yes->setObjectName("exitYes");auto no=new StudioButton("Нет");no->setObjectName("exitNo");question.addButton(yes,QMessageBox::YesRole);question.addButton(no,QMessageBox::NoRole);question.setDefaultButton(no);question.setEscapeButton(no);question.exec();closeQuestionOpen=false;
        if(question.clickedButton()!=yes){event->ignore();return;}
        closeConfirmed=true;launchAfterPower=false;stopping=true;fitCanceled=true;healthTimer.stop();centralWidget()->setEnabled(false);for(auto dialog:findChildren<QDialog*>())dialog->setEnabled(false);
    }
    if(buildPage->busy()){closeAfterBuild=true;buildPage->cancel();event->ignore();return;}
    if (server.state() != QProcess::NotRunning || fit.state() != QProcess::NotRunning) {
        server.killTree();fit.killTree();event->ignore();QTimer::singleShot(50,this,&QWidget::close);return;
    }
    server.killTree();fit.killTree();
    if(!powerResetDone&&power.needsReset()){event->ignore();if(!powerResetStarted){powerResetStarted=true;power.restoreAllDefaults();}return;}
    for(auto dialog:findChildren<QDialog*>(QString(),Qt::FindDirectChildrenOnly))dialog->close();
    event->accept();
}
bool MainWindow::smokeTest(const QString &screenshot) {
    for (int i = 0; i < navigation->count(); ++i) { navigation->setCurrentRow(i); QApplication::processEvents(); }
    search->setText("cache"); QApplication::processEvents(); search->clear(); navigation->setCurrentRow(0); QApplication::processEvents();
    return config.parameters.size() > 200 && !preview->toPlainText().isEmpty() && (screenshot.isEmpty() || grab().save(screenshot));
}
