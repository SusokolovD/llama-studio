#include "builderpage.h"
#include <QtWidgets>
BuilderPage::BuilderPage(QWidget *parent):QWidget(parent),service(this) {
    auto root=new QVBoxLayout(this);root->setContentsMargins(0,0,0,0);
    auto hint=new QLabel("Обновление исходников → сборка → установка в отдельную llama-build. Настройки сохраняются вместе с JSON-конфигом.");hint->setWordWrap(true);root->addWidget(hint);
    fields=new QWidget;auto form=new QFormLayout(fields);form->setContentsMargins(0,6,0,6);
    auto pathRow=[&](const QString &label,QLineEdit **field,const QString &name){auto container=new QWidget;auto row=new QHBoxLayout(container);row->setContentsMargins(0,0,0,0);auto edit=new QLineEdit;edit->setObjectName(name);row->addWidget(edit,1);auto browse=new QPushButton("Обзор…");row->addWidget(browse);*field=edit;form->addRow(label,container);connect(browse,&QPushButton::clicked,this,[this,edit]{auto p=QFileDialog::getExistingDirectory(this,"Выберите папку",edit->text());if(!p.isEmpty())edit->setText(QDir::toNativeSeparators(p));});};
    pathRow("Исходники (Git-репозиторий)",&source,"buildSource");source->setPlaceholderText("Папка с .git и CMakeLists.txt");
    pathRow("Готовые бинарники сборки",&binary,"buildBinary");binary->setPlaceholderText("build/bin/Release или абсолютный путь к папке с exe и DLL");
    pathRow("Установить в (llama-build)",&target,"buildTarget");
    commands=new QPlainTextEdit;commands->setObjectName("buildCommands");commands->setFixedHeight(100);form->addRow("Команды сборки",commands);
    auto syntax=new QLabel("Рабочая папка — исходники. Одна команда на строку, пути с пробелами — в двойных кавычках. Для скрипта используйте, например, powershell.exe -File build.ps1. Флаги CUDA/Vulkan и генератор CMake добавьте под свой комплект.");syntax->setWordWrap(true);syntax->setObjectName("subtitle");form->addRow(syntax);root->addWidget(fields);
    auto actions=new QHBoxLayout;startButton=new QPushButton("Git pull → собрать → скопировать");startButton->setObjectName("primary");actions->addWidget(startButton);cancelButton=new QPushButton("Отменить");cancelButton->setEnabled(false);actions->addWidget(cancelButton);actions->addStretch();root->addLayout(actions);
    state=new QLabel("Готово к запуску. Git pull выполняется с --ff-only; локальные изменения не сбрасываются.");state->setWordWrap(true);state->setTextFormat(Qt::PlainText);root->addWidget(state);
    log=new QPlainTextEdit;log->setReadOnly(true);log->setMaximumBlockCount(12000);root->addWidget(log,1);
    connect(startButton,&QPushButton::clicked,this,&BuilderPage::startRequested);connect(cancelButton,&QPushButton::clicked,&service,&BuildService::cancel);
    for(auto edit:{source,binary,target})connect(edit,&QLineEdit::textChanged,this,[this]{emit settingsChanged(settings().toJson());});
    connect(commands,&QPlainTextEdit::textChanged,this,[this]{emit settingsChanged(settings().toJson());});
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
}
BuildSettings BuilderPage::settings() const{return {source->text().trimmed(),binary->text().trimmed(),target->text().trimmed(),commands->toPlainText()};}
void BuilderPage::start(){QString message;if(!service.start(settings(),&message))state->setText(message);}
