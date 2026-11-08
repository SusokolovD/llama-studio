#include "modellibrary.h"
#include "studiobutton.h"
#include "modelcache.h"
#include <QtWidgets>
#include <QDirIterator>
namespace {
class NumberItem:public QTableWidgetItem{
public:
    NumberItem(const QString &text,qint64 number):QTableWidgetItem(text){setData(Qt::UserRole+1,number);}
    bool operator<(const QTableWidgetItem &other)const override{return data(Qt::UserRole+1).toLongLong()<other.data(Qt::UserRole+1).toLongLong();}
};
QString number(qint64 n){return n>0?QLocale().toString(n):QString("—");}
}
void ModelScan::run(){
    canceled=false;cacheHits=0;cacheError.clear();ModelCache cache;QVector<ModelInfo> results;
    QDirIterator files(directory,QDir::Files|QDir::Readable|QDir::NoSymLinks,recursive?QDirIterator::Subdirectories:QDirIterator::NoIteratorFlags);
    while(!isInterruptionRequested()&&files.hasNext()){
        const auto path=files.next();if(QFileInfo(path).suffix().compare("gguf",Qt::CaseInsensitive))continue;
        bool hit=false;const auto info=cache.get(path,force,&hit);if(isInterruptionRequested())break;if(hit)++cacheHits;results<<info;emit found(info);
    }
    canceled=isInterruptionRequested();
    if(!canceled)cache.setDirectory(directory,recursive,results);cache.save(&cacheError);
}
ModelLibraryDialog::ModelLibraryDialog(const QString &folder,QWidget *parent):QDialog(parent){
    qRegisterMetaType<ModelInfo>();setWindowTitle("Каталог моделей GGUF");setObjectName("modelLibraryWindow");resize(1320,760);
    auto root=new QVBoxLayout(this);auto title=new QLabel("Локальные модели");title->setObjectName("title");root->addWidget(title);
    auto paths=new QHBoxLayout;directory=new QLineEdit(folder);directory->setObjectName("modelLibraryDirectory");directory->setPlaceholderText("Папка с GGUF-моделями");paths->addWidget(directory,1);auto browse=new StudioButton("Обзор…");paths->addWidget(browse);refresh=new StudioButton("Обновить каталог");paths->addWidget(refresh);cancel=new StudioButton("Отмена");paths->addWidget(cancel);cancel->setEnabled(false);root->addLayout(paths);
    auto options=new QHBoxLayout;recursive=new QCheckBox("Включая вложенные папки");recursive->setChecked(true);options->addWidget(recursive);filter=new QLineEdit;filter->setObjectName("modelLibraryFilter");filter->setPlaceholderText("Фильтр по модели, архитектуре, квантованию или пути");filter->setClearButtonEnabled(true);options->addWidget(filter,1);root->addLayout(options);
    options->addWidget(new QLabel("Тип:"));role=new QComboBox;role->setObjectName("modelLibraryRole");role->addItem("Все",QString());for(const auto &type:QStringList{"--model","--mmproj","--spec-draft-model"})role->addItem(modelRoleLabel(type),type);options->addWidget(role);connect(role,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{filterRows();});
    auto note=new QLabel("Читаются только метаданные GGUF, веса не загружаются. Одна строка — один файл; для разделённой модели указан размер части, а не всех весов. Квантование берётся из general.file_type, не из имени файла.");note->setWordWrap(true);root->addWidget(note);
    force=new QCheckBox("Перечитать все GGUF при обновлении");force->setObjectName("forceModelRefresh");root->addWidget(force);auto cachePath=new QLabel("JSON-кэш: "+ModelCache::defaultPath());cachePath->setTextFormat(Qt::PlainText);cachePath->setWordWrap(true);cachePath->setTextInteractionFlags(Qt::TextSelectableByMouse);root->addWidget(cachePath);
    table=new QTableWidget(0,12);table->setObjectName("modelLibraryTable");table->setHorizontalHeaderLabels({"Файл","Название модели","Архитектура","Масштаб","Квантование","Размер файла, ГиБ","Контекст модели","Слоёв","Часть","Шаблон чата","Тип / состояние","Полный путь"});table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->setSelectionBehavior(QAbstractItemView::SelectRows);table->setSelectionMode(QAbstractItemView::SingleSelection);table->setSortingEnabled(true);table->verticalHeader()->hide();table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);table->horizontalHeader()->setStretchLastSection(true);table->setColumnWidth(0,260);table->setColumnWidth(1,220);table->setColumnWidth(5,145);table->setColumnWidth(6,145);root->addWidget(table,1);
    status=new QLabel("Выберите папку и нажмите «Обновить каталог».");status->setObjectName("modelLibraryStatus");status->setWordWrap(true);root->addWidget(status);
    auto footer=new QHBoxLayout;footer->addWidget(new QLabel("Использовать как:"));targetRole=new QComboBox;targetRole->setObjectName("modelLibraryTargetRole");targetRole->addItem("Основная модель","--model");targetRole->addItem("Графика (mmproj)","--mmproj");targetRole->addItem("Draft","--spec-draft-model");footer->addWidget(targetRole);use=new StudioButton("Использовать выбранную");use->setEnabled(false);footer->addWidget(use);auto assign=new StudioButton("Задать тип…");assign->setObjectName("assignModelRole");auto menu=new QMenu(assign);menu->setObjectName("modelRoleMenu");auto automatic=menu->addAction("Автоматически");automatic->setData(QString());connect(automatic,&QAction::triggered,this,[this]{assignRole({});});for(const auto &type:QStringList{"--model","--mmproj","--spec-draft-model"}){auto action=menu->addAction(modelRoleLabel(type));action->setData(type);connect(action,&QAction::triggered,this,[this,type]{assignRole(type);});}assign->setMenu(menu);assign->setToolTip("Назначение draft нельзя достоверно определить по GGUF. Укажите тип вручную; он сохранится в JSON-кэше.");footer->addWidget(assign);footer->addStretch();auto close=new StudioButton("Закрыть");footer->addWidget(close);root->addLayout(footer);
    connect(browse,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getExistingDirectory(this,"Папка с моделями",directory->text());if(!path.isEmpty()){directory->setText(path);if(!loadCachedDirectory())startScan();}});
    connect(refresh,&QPushButton::clicked,this,&ModelLibraryDialog::startScan);connect(directory,&QLineEdit::returnPressed,this,&ModelLibraryDialog::startScan);connect(cancel,&QPushButton::clicked,&scan,&QThread::requestInterruption);connect(filter,&QLineEdit::textChanged,this,&ModelLibraryDialog::filterRows);connect(use,&QPushButton::clicked,this,&ModelLibraryDialog::selectModel);connect(close,&QPushButton::clicked,this,&QDialog::close);
    connect(table,&QTableWidget::itemSelectionChanged,this,[this]{auto item=table->item(table->currentRow(),0);use->setEnabled(item&&!item->data(Qt::UserRole).toString().isEmpty()&&!table->isRowHidden(item->row()));});
    connect(&scan,&ModelScan::found,this,&ModelLibraryDialog::addModel);
    connect(&scan,&QThread::finished,this,[this,browse]{directory->setEnabled(true);recursive->setEnabled(true);force->setEnabled(true);browse->setEnabled(true);refresh->setEnabled(true);cancel->setEnabled(false);status->setText(QString("%1 Файлов: %2; ошибок: %3; размер: %4 ГиБ; из JSON: %5.").arg(scan.canceled?"Сканирование отменено.":"Сканирование завершено.").arg(table->rowCount()).arg(errors).arg(totalBytes/1073741824.0,0,'f',2).arg(scan.cacheHits)+(scan.cacheError.isEmpty()?QString():"\n"+scan.cacheError));});
    connect(&scan,&QThread::started,this,[this,browse,assign]{directory->setEnabled(false);recursive->setEnabled(false);force->setEnabled(false);assign->setEnabled(false);browse->setEnabled(false);refresh->setEnabled(false);cancel->setEnabled(true);});
    connect(&scan,&QThread::finished,assign,[assign]{assign->setEnabled(true);});
    if(!folder.isEmpty()&&!loadCachedDirectory()&&QFileInfo(folder).isDir())QTimer::singleShot(0,this,&ModelLibraryDialog::startScan);
}
ModelLibraryDialog::~ModelLibraryDialog(){scan.requestInterruption();scan.wait();}
void ModelLibraryDialog::startScan(){
    if(scan.isRunning())return;QDir folder(directory->text());if(directory->text().trimmed().isEmpty()||!folder.exists()){status->setText("Укажите существующую папку с моделями.");return;}
    scan.directory=folder.absolutePath();scan.recursive=recursive->isChecked();scan.force=force->isChecked();table->setRowCount(0);use->setEnabled(false);errors=0;totalBytes=0;emit directoryChanged(scan.directory);status->setText("Обновление каталога и проверка изменений файлов…");scan.start();
}
bool ModelLibraryDialog::loadCachedDirectory(){bool found=false;ModelCache cache;const auto models=cache.directory(directory->text(),recursive->isChecked(),&found);if(!found)return false;table->setRowCount(0);errors=0;totalBytes=0;for(const auto &model:models)addModel(model);status->setText(QString("Загружено из JSON без сканирования: %1 файлов. Для поиска новых и изменённых моделей нажмите «Обновить каталог».").arg(models.size()));emit directoryChanged(directory->text());return true;}
void ModelLibraryDialog::addModel(const ModelInfo &m){
    table->setSortingEnabled(false);int row=table->rowCount();table->insertRow(row);totalBytes+=m.fileBytes;if(!m.error.isEmpty())++errors;
    QString selected=m.path;QString state=m.error.isEmpty()?modelRoleLabel(modelRole(m)):m.error;
    if(m.shards>1&&m.shardIndex>0){QFileInfo file(m.path);auto first=file.fileName();first.replace(QRegularExpression("-\\d{5}-of-(\\d{5}\\.gguf)$",QRegularExpression::CaseInsensitiveOption),"-00001-of-\\1");selected=file.dir().filePath(first);if(first==file.fileName()||!QFileInfo::exists(selected)){selected.clear();state="Не найдена первая часть модели";}}
    const QStringList cells={QFileInfo(m.path).fileName(),m.name,m.architecture,m.sizeLabel,modelQuantization(m.fileType),QString::number(m.fileBytes/1073741824.0,'f',3),number(m.trainingContext),number(m.layers),QString("%1 / %2").arg(m.shardIndex+1).arg(m.shards),m.hasChatTemplate?"Есть":"Нет / не указан",state,QDir::toNativeSeparators(m.path)};
    for(int c=0;c<cells.size();++c){QTableWidgetItem *item;if(c==5)item=new NumberItem(cells[c],m.fileBytes);else if(c==6)item=new NumberItem(cells[c],m.trainingContext);else if(c==7)item=new NumberItem(cells[c],m.layers);else item=new QTableWidgetItem(cells[c]);item->setToolTip(cells[c]);table->setItem(row,c,item);}
    table->item(row,5)->setToolTip(QLocale().toString(m.fileBytes)+" байт");table->item(row,0)->setData(Qt::UserRole,m.error.isEmpty()?selected:QString());table->item(row,0)->setData(Qt::UserRole+2,modelRole(m));if(!m.error.isEmpty())for(int c=0;c<table->columnCount();++c)table->item(row,c)->setForeground(QColor("#ff9ca7"));table->setSortingEnabled(true);filterRows();status->setText(QString("Найдено файлов: %1. Чтение метаданных…").arg(table->rowCount()));
}
void ModelLibraryDialog::filterRows(){
    const auto text=filter->text().trimmed(),type=role->currentData().toString();for(int r=0;r<table->rowCount();++r){bool matches=text.isEmpty();for(int c=0;c<table->columnCount()&&!matches;++c)matches=table->item(r,c)->text().contains(text,Qt::CaseInsensitive);const bool kind=type.isEmpty()||table->item(r,0)->data(Qt::UserRole+2).toString()==type;table->setRowHidden(r,!matches||!kind);}auto item=table->item(table->currentRow(),0);use->setEnabled(item&&!table->isRowHidden(item->row())&&!item->data(Qt::UserRole).toString().isEmpty());
}
void ModelLibraryDialog::assignRole(const QString &type){const auto row=table->currentRow();if(row<0||table->isRowHidden(row))return;const auto path=QDir::fromNativeSeparators(table->item(row,11)->text());ModelCache cache;cache.setRole(path,type);QString error;if(!cache.save(&error)){QMessageBox::warning(this,"Тип модели",error);return;}const auto models=cache.allModels();QMap<QString,ModelInfo> byPath;for(const auto &m:models)byPath.insert(QDir::toNativeSeparators(m.path),m);table->setSortingEnabled(false);for(int r=0;r<table->rowCount();++r){const auto p=table->item(r,11)->text();if(!byPath.contains(p))continue;const auto m=byPath.value(p);table->item(r,0)->setData(Qt::UserRole+2,modelRole(m));if(m.error.isEmpty())table->item(r,10)->setText(modelRoleLabel(modelRole(m)));}table->setSortingEnabled(true);filterRows();}
void ModelLibraryDialog::selectModel(){auto item=table->item(table->currentRow(),0);if(!item||table->isRowHidden(item->row()))return;const auto path=item->data(Qt::UserRole).toString();if(path.isEmpty()||!QFileInfo::exists(path)){status->setText("Файл модели больше недоступен. Обновите каталог.");return;}emit modelSelected(path,targetRole->currentData().toString());}
