#include "studiobutton.h"
#include "builderpage.h"
#include <QtWidgets>
BuilderPage::BuilderPage(QWidget *parent):QWidget(parent),service(this) {
    auto root=new QVBoxLayout(this);root->setContentsMargins(0,0,0,0);
    auto hint=new QLabel("Обновление исходников → сборка → установка в отдельную llama-build. Настройки сохраняются вместе с JSON-конфигом.");hint->setWordWrap(true);root->addWidget(hint);
    fields=new QWidget;auto form=new QFormLayout(fields);form->setContentsMargins(0,6,0,6);
    auto pathRow=[&](const QString &label,QLineEdit **field,const QString &name){auto container=new QWidget;auto row=new QHBoxLayout(container);row->setContentsMargins(0,0,0,0);auto edit=new QLineEdit;edit->setObjectName(name);row->addWidget(edit,1);auto browse=new StudioButton("Обзор…");row->addWidget(browse);*field=edit;form->addRow(label,container);connect(browse,&QPushButton::clicked,this,[this,edit]{auto p=QFileDialog::getExistingDirectory(this,"Выберите папку",edit->text());if(!p.isEmpty())edit->setText(QDir::toNativeSeparators(p));});};
    pathRow("Исходники (Git-репозиторий)",&source,"buildSource");source->setPlaceholderText("Папка с .git и CMakeLists.txt");
    pathRow("Готовые бинарники сборки",&binary,"buildBinary");binary->setPlaceholderText("build/bin/Release или абсолютный путь к папке с exe и DLL");
    pathRow("Установить в (llama-build)",&target,"buildTarget");
    auto recipeTools=new QWidget;auto tools=new QHBoxLayout(recipeTools);tools->setContentsMargins(0,0,0,0);
    auto load=new StudioButton("Загрузить JSON команд…");tools->addWidget(load);auto save=new StudioButton("Сохранить JSON команд…");tools->addWidget(save);tools->addStretch();form->addRow(recipeTools);
    commands=new QPlainTextEdit;commands->setObjectName("buildCommands");commands->setFixedHeight(150);form->addRow("JSON команд сборки",commands);
    recipeState=new QLabel;recipeState->setObjectName("recipeState");recipeState->setWordWrap(true);recipeState->setTextFormat(Qt::PlainText);
    auto syntax=new QLabel("program — программа, arguments — массив строк. Пути с пробелами указываются одной строкой массива, без дополнительных кавычек. Рабочая папка — исходники. Флаги backend изменяйте под свой комплект. Файл по умолчанию: build-commands.json рядом с exe.");syntax->setWordWrap(true);syntax->setObjectName("subtitle");form->addRow(syntax);
    auto split=new QSplitter(Qt::Vertical);split->setChildrenCollapsible(false);auto settingsScroll=new QScrollArea;settingsScroll->setWidgetResizable(true);settingsScroll->setWidget(fields);settingsScroll->setMinimumHeight(180);split->addWidget(settingsScroll);root->addWidget(split,1);
    connect(load,&QPushButton::clicked,this,[this]{auto path=QFileDialog::getOpenFileName(this,"Загрузить команды",{},"JSON (*.json)");if(path.isEmpty())return;QString error;if(!loadRecipeFile(path,&error))QMessageBox::warning(this,"Команды сборки",error);});
    connect(save,&QPushButton::clicked,this,[this]{auto path=QFileDialog::getSaveFileName(this,"Сохранить команды","build-commands.json","JSON (*.json)");if(path.isEmpty())return;QString error;if(!saveRecipeFile(path,&error))QMessageBox::warning(this,"Команды сборки",error);});
    auto actions=new QHBoxLayout;startButton=new StudioButton("Git pull → собрать → скопировать");startButton->setObjectName("primary");actions->addWidget(startButton);cancelButton=new StudioButton("Отменить");cancelButton->setEnabled(false);actions->addWidget(cancelButton);actions->addStretch();root->addLayout(actions);root->addWidget(recipeState);
    state=new QLabel("Готово к запуску. Git pull выполняется с --ff-only; локальные изменения не сбрасываются.");state->setWordWrap(true);state->setTextFormat(Qt::PlainText);root->addWidget(state);
    log=new QPlainTextEdit;log->setReadOnly(true);log->setMaximumBlockCount(12000);log->setMinimumHeight(80);log->setPlaceholderText("Журнал обновления, сборки и копирования. Размер панелей можно менять разделителем.");split->addWidget(log);split->setSizes({390,140});
    connect(startButton,&QPushButton::clicked,this,&BuilderPage::startRequested);connect(cancelButton,&QPushButton::clicked,&service,&BuildService::cancel);
    for(auto edit:{source,binary,target})connect(edit,&QLineEdit::textChanged,this,[this]{emit settingsChanged(settings().toJson());});
    connect(commands,&QPlainTextEdit::textChanged,this,[this]{QVector<BuildCommand> parsed;QString why;bool ok=BuildRecipe::parse(commands->toPlainText().toUtf8(),&parsed,&why);recipeState->setText(ok?QString("JSON корректен: команд %1").arg(parsed.size()):why);emit settingsChanged(settings().toJson());});
    connect(&service,&BuildService::output,this,[this](const QString &s){auto cursor=log->textCursor();cursor.movePosition(QTextCursor::End);cursor.insertText(s);log->setTextCursor(cursor);log->ensureCursorVisible();});
    connect(&service,&BuildService::phaseChanged,state,&QLabel::setText);
    connect(&service,&BuildService::busyChanged,this,[this](bool busy){fields->setEnabled(!busy);startButton->setEnabled(!busy);cancelButton->setEnabled(busy);emit busyChanged(busy);});
    connect(&service,&BuildService::finished,this,[this](bool success,const QString &,const QString &path){if(success)emit deployed(path);});
    loadSettings({});
}
void BuilderPage::loadSettings(const QJsonObject &json){
    auto s=BuildSettings::fromJson(json);
    for(auto edit:{source,binary,target})edit->blockSignals(true);commands->blockSignals(true);
    source->setText(s.sourceDirectory);binary->setText(s.binaryDirectory);target->setText(s.targetDirectory);commands->setPlainText(s.commands);
    for(auto edit:{source,binary,target})edit->blockSignals(false);commands->blockSignals(false);
    QVector<BuildCommand> parsed;QString why;bool ok=BuildRecipe::parse(s.commands.toUtf8(),&parsed,&why);recipeState->setText(ok?QString("JSON корректен: команд %1").arg(parsed.size()):why);
}
bool BuilderPage::loadRecipeFile(const QString &path,QString *error){
    if(service.busy()){if(error)*error="Дождитесь завершения сборки.";return false;}
    QFile file(path);if(!file.open(QIODevice::ReadOnly)){if(error)*error=file.errorString();return false;}
    auto data=file.read(1024*1024+1);if(!BuildRecipe::parse(data,nullptr,error))return false;commands->setPlainText(BuildRecipe::format(QJsonDocument::fromJson(data).object()));return true;
}
bool BuilderPage::saveRecipeFile(const QString &path,QString *error) const{
    auto data=commands->toPlainText().toUtf8();if(!BuildRecipe::parse(data,nullptr,error))return false;data=QJsonDocument::fromJson(data).toJson(QJsonDocument::Indented);
    QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(data)!=data.size()||!file.commit()){if(error)*error=file.errorString();return false;}return true;
}
BuildSettings BuilderPage::settings() const{return {source->text().trimmed(),binary->text().trimmed(),target->text().trimmed(),commands->toPlainText()};}
void BuilderPage::start(){QString message;if(!service.start(settings(),&message))state->setText(message);}
