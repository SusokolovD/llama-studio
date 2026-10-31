#include "advisordialog.h"
#include <QtWidgets>
AdvisorDialog::AdvisorDialog(const Config &config,QWidget *parent):QDialog(parent),config(config) {
    setWindowTitle("Помощник настройки модели"); resize(1180,850); setMinimumSize(980,700);
    auto root=new QVBoxLayout(this); auto split=new QSplitter; root->addWidget(split,1);
    auto settings=new QWidget; auto left=new QVBoxLayout(settings); split->addWidget(settings);
    auto heading=new QLabel("Модель и задача"); heading->setObjectName("title"); left->addWidget(heading);
    auto saved=config.metadata.value("advisor").toObject();
    auto modelRow=new QHBoxLayout; path=new QLineEdit;
    if(auto p=config.find("--model"))path->setText(p->value.toString());
    path->setPlaceholderText("Файл GGUF для анализа метаданных"); modelRow->addWidget(path,1);
    auto browse=new QPushButton("…"); modelRow->addWidget(browse); left->addLayout(modelRow);
    connect(browse,&QPushButton::clicked,this,[this]{auto p=QFileDialog::getOpenFileName(this,"Модель GGUF",path->text(),"GGUF (*.gguf);;Все файлы (*)");if(!p.isEmpty()){path->setText(p);inspectModel();}});
    auto inspect=new QPushButton("Прочитать метаданные GGUF"); left->addWidget(inspect); connect(inspect,&QPushButton::clicked,this,&AdvisorDialog::inspectModel);
    modelLabel=new QLabel("Считываются только метаданные, веса не загружаются в RAM/GPU."); modelLabel->setWordWrap(true); modelLabel->setTextFormat(Qt::PlainText); left->addWidget(modelLabel);
    auto form=new QFormLayout; mode=new QComboBox; mode->addItems({"Чат и диалоги","Агентское программирование"});mode->setCurrentIndex(saved.value("mode").toInt(0)); form->addRow("Задача",mode);
    context=new QSpinBox; context->setRange(1,1048576); context->setSingleStep(1024); context->setValue(saved.value("contextPerSlot").toInt(8192)); form->addRow("Токенов на диалог",context);
    slotCount=new QSpinBox; slotCount->setRange(1,32); slotCount->setValue(saved.value("slots").toInt(1)); form->addRow("Одновременных диалогов",slotCount);
    reserve=new QSpinBox; reserve->setRange(0,1048576); reserve->setSingleStep(256); reserve->setValue(saved.value("reserveMiB").toInt(1024)); reserve->setSuffix(" МиБ"); form->addRow("Резерв каждой GPU",reserve);
    cache=new QComboBox; cache->addItem("f16 — исходное качество","f16"); cache->addItem("q8_0 — экономия памяти","q8_0"); cache->addItem("q4_0 — сильное сжатие","q4_0"); form->addRow("KV-кэш",cache);
    cache->setCurrentIndex(qMax(0,cache->findData(saved.value("cacheType").toString("f16"))));
    sampling=new QComboBox; sampling->addItems({"Не менять: по карточке модели","Пример для чата (0,7)","Пример для кода (0,2)"});sampling->setCurrentIndex(saved.value("sampling").toInt(0)); form->addRow("Сэмплирование",sampling); left->addLayout(form);
    cpu=new QCheckBox("Только CPU (без видеокарт)");cpu->setChecked(saved.value("cpuOnly").toBool(false)); left->addWidget(cpu);
    auto gpuRow=new QHBoxLayout; detect=new QPushButton("Найти GPU через llama.cpp"); gpuRow->addWidget(detect); auto manual=new QPushButton("Добавить вручную"); gpuRow->addWidget(manual); left->addLayout(gpuRow);
    devices=new QTableWidget(0,5); devices->setHorizontalHeaderLabels({"Исп.","ID устройства","Название","Всего, МиБ","Свободно, МиБ"}); devices->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents); devices->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch); devices->setMinimumHeight(140); left->addWidget(devices,1);
    deviceLabel=new QLabel("Выберите нужные GPU одного backend. Свободную память можно уточнить вручную; ID должны совпадать со списком llama.cpp."); deviceLabel->setWordWrap(true); deviceLabel->setTextFormat(Qt::PlainText); left->addWidget(deviceLabel);
    report=new QTextBrowser; report->setOpenExternalLinks(true); split->addWidget(report); split->setSizes({530,650});
    auto buttons=new QHBoxLayout; auto close=new QPushButton("Отмена"); buttons->addWidget(close); buttons->addStretch(); apply=new QPushButton("Применить в конфиг"); buttons->addWidget(apply); applyFit=new QPushButton("Применить и запустить fit"); applyFit->setObjectName("primary"); buttons->addWidget(applyFit); root->addLayout(buttons);
    connect(close,&QPushButton::clicked,this,&QDialog::reject); connect(apply,&QPushButton::clicked,this,[this]{runFit=false;accept();}); connect(applyFit,&QPushButton::clicked,this,[this]{runFit=true;accept();});
    connect(mode,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int n){int suggested=n?32768:8192;if(model.trainingContext>0)suggested=int(qMin<qint64>(suggested,model.trainingContext));context->setValue(suggested);updateAdvice();});
    for(auto spin:{context,slotCount,reserve})connect(spin,qOverload<int>(&QSpinBox::valueChanged),this,[this]{updateAdvice();});
    for(auto combo:{sampling,cache})connect(combo,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{updateAdvice();});
    connect(cpu,&QCheckBox::toggled,this,[this](bool on){devices->setEnabled(!on);updateAdvice();});
    connect(devices,&QTableWidget::itemChanged,this,[this]{updateAdvice();});
    connect(path,&QLineEdit::textChanged,this,[this]{updateAdvice();});
    connect(manual,&QPushButton::clicked,this,[this]{addDevice({"CUDA"+QString::number(devices->rowCount()),"Введите память устройства",0,0});updateAdvice();});
    connect(detect,&QPushButton::clicked,this,&AdvisorDialog::detectDevices);
    detector.setProcessChannelMode(QProcess::MergedChannels);
    connect(&detector,&QProcess::readyReadStandardOutput,this,[this]{auto bytes=detector.readAllStandardOutput();if(detectorOutput.size()+bytes.size()<1048576)detectorOutput+=bytes;else detector.kill();});
    connect(&detector,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus exit){
        detectorOutput+=detector.readAllStandardOutput();detect->setEnabled(true);
        auto found=parseDevices(QString::fromUtf8(detectorOutput));
        if(code!=0 || exit!=QProcess::NormalExit || found.isEmpty())deviceLabel->setText("GPU не обнаружены. Проверьте сборку llama.cpp, добавьте устройства вручную или выберите CPU.\n"+QString::fromUtf8(detectorOutput).right(600));
        else {devices->setRowCount(0);for(const auto &d:found)addDevice(d);deviceLabel->setText("Обнаружено GPU: "+QString::number(found.size())+". Исключите дубли одной карты в разных backend; отметьте нужные устройства.");}
        updateAdvice();
    });
    connect(&detector,&QProcess::errorOccurred,this,[this](QProcess::ProcessError e){if(e==QProcess::FailedToStart){detect->setEnabled(true);deviceLabel->setText("Не удалось запустить определение GPU: "+detector.errorString());updateAdvice();}});
    devices->setEnabled(!cpu->isChecked());
    if(!path->text().isEmpty())inspectModel();
    updateAdvice();
    QTimer::singleShot(0,this,[this]{if(!findExecutable(this->config.directory,false).path.isEmpty())detectDevices();});
}
AdvisorDialog::~AdvisorDialog(){if(detector.state()!=QProcess::NotRunning){detector.kill();detector.waitForFinished(1000);}}
QJsonObject AdvisorDialog::options() const {
    return {{"mode",mode->currentIndex()},{"contextPerSlot",context->value()},{"slots",slotCount->value()},{"reserveMiB",reserve->value()},{"cacheType",cache->currentData().toString()},{"sampling",sampling->currentIndex()},{"cpuOnly",cpu->isChecked()}};
}
void AdvisorDialog::inspectModel(){
    model=readModelInfo(path->text());
    if(!model.error.isEmpty())modelLabel->setText(model.error);
    else modelLabel->setText(QString("%1\nАрхитектура: %2; контекст обучения: %3; частей: %4; шаблон чата: %5. Размер выбранного файла: %6 ГиБ.").arg(model.name,model.architecture).arg(model.trainingContext).arg(model.shards).arg(model.hasChatTemplate?"есть":"не найден").arg(model.fileBytes/1073741824.0,0,'f',2));
    if(model.trainingContext>0 && context->value()>model.trainingContext)context->setValue(int(qMin<qint64>(model.trainingContext,1048576)));
    updateAdvice();
}
void AdvisorDialog::addDevice(const DeviceInfo &d){
    devices->blockSignals(true);int row=devices->rowCount();devices->insertRow(row);
    auto use=new QTableWidgetItem;use->setFlags(Qt::ItemIsUserCheckable|Qt::ItemIsEnabled);use->setCheckState(Qt::Checked);devices->setItem(row,0,use);
    devices->setItem(row,1,new QTableWidgetItem(d.id));devices->setItem(row,2,new QTableWidgetItem(d.name));devices->setItem(row,3,new QTableWidgetItem(QString::number(d.totalMiB)));devices->setItem(row,4,new QTableWidgetItem(QString::number(d.freeMiB)));devices->blockSignals(false);
}
void AdvisorDialog::detectDevices(){
    if(detector.state()!=QProcess::NotRunning)return;
    auto exe=findExecutable(config.directory,false);
    if(exe.path.isEmpty()){deviceLabel->setText("Не найден llama-server. Укажите его папку в главном окне или добавьте GPU вручную.");return;}
    detectorOutput.clear();detect->setEnabled(false);apply->setEnabled(false);applyFit->setEnabled(false);deviceLabel->setText("Определение устройств…");detector.setWorkingDirectory(QFileInfo(exe.path).absolutePath());detector.start(exe.path,exe.prefix+QStringList{"--list-devices"});
    const auto run=++detectionRun;
    QTimer::singleShot(7000,&detector,[this,run]{if(run==detectionRun && detector.state()!=QProcess::NotRunning)detector.kill();});
}
void AdvisorDialog::updateAdvice(){
    AdviceInput input;input.agent=mode->currentIndex()==1;input.cpuOnly=cpu->isChecked();input.contextPerSlot=context->value();input.slotCount=slotCount->value();input.reserveMiB=reserve->value();input.cacheType=cache->currentData().toString();input.sampling=sampling->currentIndex();
    input.model=model;if(model.path!=path->text()){input.model=ModelInfo();input.model.path=path->text();input.model.error="Метаданные выбранного файла ещё не прочитаны.";}
    for(auto name:{"--mmproj","--spec-draft-model","--lora","--lora-scaled"})if(auto p=config.find(name))input.hasExtras|=p->enabled;
    for(int row=0;row<devices->rowCount();++row)if(devices->item(row,0)->checkState()==Qt::Checked){DeviceInfo d;d.id=devices->item(row,1)->text().trimmed();d.name=devices->item(row,2)->text();d.totalMiB=devices->item(row,3)->text().toLongLong();d.freeMiB=devices->item(row,4)->text().toLongLong();input.devices.append(d);}
    recommendation=makeAdvice(input);report->setHtml(recommendation.html());bool ready=recommendation.error.isEmpty() && detector.state()==QProcess::NotRunning;apply->setEnabled(ready);applyFit->setEnabled(ready);
}
