#include "buildservice.h"
#include "config.h"
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>

namespace {
QString resolvedPath(QString path) {
    path=QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    QStringList suffix;
    while(!QFileInfo::exists(path)){QFileInfo info(path);suffix.prepend(info.fileName());auto parent=info.absolutePath();if(parent==path)break;path=parent;}
    QString result=QFileInfo(path).canonicalFilePath();
    if(result.isEmpty())result=path;
    for(const auto &part:suffix)result=QDir(result).filePath(part);
    return QDir::cleanPath(result);
}
bool within(const QString &child,const QString &parent) {
#ifdef Q_OS_WIN
    constexpr auto sensitivity=Qt::CaseInsensitive;
#else
    constexpr auto sensitivity=Qt::CaseSensitive;
#endif
    return child.compare(parent,sensitivity)==0 || child.startsWith(parent+"/",sensitivity);
}
QString absoluteBinary(const BuildSettings &s){return resolvedPath(QDir(s.sourceDirectory).absoluteFilePath(s.binaryDirectory));}
QString deployFiles(const BuildSettings &s,const std::shared_ptr<std::atomic_bool> &cancel) {
    QString validation;if(!s.validate(&validation))return validation;
    const QString source=absoluteBinary(s), target=resolvedPath(s.targetDirectory);
    auto exe=findExecutable(source,false);
    if(exe.path.isEmpty() || resolvedPath(QFileInfo(exe.path).absolutePath())!=source)return "В папке готовых бинарников не найден llama-server (или llama). Укажите папку, где exe лежит непосредственно, например build/bin/Release.";
    if(!QDir().mkpath(QFileInfo(target).absolutePath()))return "Не удалось создать родительскую папку назначения.";
    QTemporaryDir stage(QFileInfo(target).absolutePath()+"/.llama-deploy-XXXXXX");
    if(!stage.isValid())return "Не удалось подготовить временную папку копирования.";
    QStringList files;
    QDirIterator it(source,QDir::Files|QDir::Dirs|QDir::NoDotAndDotDot|QDir::Hidden,QDirIterator::Subdirectories);
    while(it.hasNext()) {
        it.next();auto info=it.fileInfo();
        if(info.isSymLink())return "В бинарниках обнаружена символическая ссылка: "+info.fileName()+". Используйте обычные файлы.";
        if(info.isDir())continue;
        QString relative=QDir(source).relativeFilePath(info.absoluteFilePath());
        if(relative==".git" || relative.startsWith(".git/"))return "Выбрана папка репозитория вместо готовых бинарников.";
        files<<relative;
        const QString prepared=stage.filePath("new/"+relative);
        if(!QDir().mkpath(QFileInfo(prepared).absolutePath()))return "Не удалось подготовить дерево файлов.";
        QFile in(info.absoluteFilePath()),out(prepared);
        if(!in.open(QIODevice::ReadOnly)||!out.open(QIODevice::WriteOnly))return "Не удалось открыть файл для копирования: "+relative;
        while(!in.atEnd()) {
            if(cancel->load())return "Сборка/копирование отменены.";
            auto bytes=in.read(1024*1024);
            if(bytes.isEmpty() && in.error()!=QFileDevice::NoError)return "Ошибка чтения: "+relative;
            if(out.write(bytes)!=bytes.size())return "Ошибка записи: "+relative;
        }
        out.close();QFile::setPermissions(prepared,info.permissions());
    }
    if(cancel->load())return "Сборка/копирование отменены.";
    if(!QDir().mkpath(target))return "Не удалось создать папку назначения.";
    struct Installed{QString relative;bool backup=false,placed=false;};
    QVector<Installed> history;
    QString failure;
    for(const auto &relative:files) {
        if(cancel->load()){failure="Копирование отменено.";break;}
        const QString destination=QDir(target).filePath(relative),parent=QFileInfo(destination).absolutePath();
        if(!within(resolvedPath(parent),target)||QFileInfo(destination).isSymLink()){failure="Небезопасный путь назначения: "+relative;break;}
        if(!QDir().mkpath(parent)){failure="Не удалось создать папку: "+relative;break;}
        history.append({relative,false,false});auto &record=history.last();
        if(QFileInfo::exists(destination)) {
            if(!QFileInfo(destination).isFile()){failure="Вместо файла назначения обнаружена папка: "+relative;break;}
            const QString backup=stage.filePath("backup/"+relative);QDir().mkpath(QFileInfo(backup).absolutePath());
            if(!QFile::rename(destination,backup)){failure="Не удалось заменить файл: "+relative+". Возможно, он используется другим процессом.";break;}
            record.backup=true;
        }
        if(!QFile::rename(stage.filePath("new/"+relative),destination)){failure="Не удалось установить файл: "+relative;break;}
        record.placed=true;
    }
    if(failure.isEmpty())return {};
    bool restored=true;
    for(int i=history.size()-1;i>=0;--i){const auto &r=history[i];const QString dest=QDir(target).filePath(r.relative);if(r.placed && !QFile::remove(dest))restored=false;if(r.backup && !QFile::rename(stage.filePath("backup/"+r.relative),dest))restored=false;}
    if(!restored){stage.setAutoRemove(false);failure+=" Не все файлы удалось восстановить. Резервные копии: "+stage.path();}
    else failure+=" Прежние файлы восстановлены.";
    return failure;
}
}
BuildSettings BuildSettings::fromJson(const QJsonObject &j) {
    return {j.value("sourceDirectory").toString(),j.value("binaryDirectory").toString("build/bin/Release"),j.value("targetDirectory").toString(QDir(QCoreApplication::applicationDirPath()).filePath("llama-build")),j.value("commands").toString("cmake -S . -B build -DCMAKE_BUILD_TYPE=Release\ncmake --build build --config Release --parallel 4")};
}
QJsonObject BuildSettings::toJson() const{return {{"sourceDirectory",sourceDirectory},{"binaryDirectory",binaryDirectory},{"targetDirectory",targetDirectory},{"commands",commands}};}
bool BuildSettings::validate(QString *error) const {
    auto fail=[&](const QString &message){if(error)*error=message;return false;};
    if(sourceDirectory.trimmed().isEmpty()||!QFileInfo(sourceDirectory).isDir()||!QFileInfo(QDir(sourceDirectory).filePath(".git")).exists()||!QFileInfo(QDir(sourceDirectory).filePath("CMakeLists.txt")).isFile())return fail("Укажите корень Git-репозитория исходников llama.cpp с CMakeLists.txt.");
    if(binaryDirectory.trimmed().isEmpty()||targetDirectory.trimmed().isEmpty()||!QFileInfo(targetDirectory).isAbsolute())return fail("Укажите папку готовых бинарников и абсолютный путь к папке назначения.");
    const QString source=resolvedPath(sourceDirectory),binary=absoluteBinary(*this),target=resolvedPath(targetDirectory);
    if(QDir(target).isRoot()||target.endsWith("/.git",Qt::CaseInsensitive)||target.contains("/.git/",Qt::CaseInsensitive))return fail("Выберите отдельную папку назначения, а не корень диска или служебную папку Git.");
    if(within(source,target)||within(binary,target)||within(target,binary))return fail("Папка назначения должна быть отдельной: она не может содержать исходники или пересекаться с папкой готовых бинарников.");
    int count=0;
    for(auto line:commands.split('\n')){line=line.trimmed();if(line.isEmpty()||line.startsWith('#'))continue;QStringList parts;QString why;if(!Config::tokenize(line,&parts,&why)||parts.isEmpty()||parts.first().isEmpty())return fail("Некорректная команда: "+why);++count;}
    return count>0 || fail("Введите хотя бы одну команду сборки.");
}
BuildService::BuildService(QObject *parent):QObject(parent) {
    process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&process,&QProcess::readyReadStandardOutput,this,[this]{drain();});
    connect(&process,&QProcess::started,this,[this]{if(active&&canceled->load())cancel();});
    connect(&process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus status){drain(true);if(!active)return;if(canceled->load())complete(false,"Сборка отменена. Готовая папка не обновлена.");else if(code!=0||status!=QProcess::NormalExit)complete(false,"Команда завершилась с ошибкой, код "+QString::number(code)+". Копирование не выполнялось.");else next();});
    connect(&process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError e){if(e==QProcess::FailedToStart&&active)complete(false,"Не удалось запустить команду: "+process.errorString());});
}
BuildService::~BuildService(){if(canceled)canceled->store(true);if(treeKiller.state()!=QProcess::NotRunning)treeKiller.waitForFinished(3000);if(process.state()!=QProcess::NotRunning){process.kill();process.waitForFinished(3000);}if(copyThread){copyThread->wait();delete copyThread;}}
bool BuildService::start(const BuildSettings &s,QString *error) {
    if(active){if(error)*error="Сборка уже выполняется.";return false;}
    if(!s.validate(error))return false;
    if(copyThread){copyThread->wait();delete copyThread;copyThread=nullptr;}
    settings=s;settings.sourceDirectory=resolvedPath(s.sourceDirectory);settings.targetDirectory=resolvedPath(s.targetDirectory);commands.clear();
    for(auto line:s.commands.split('\n'))if(!line.trimmed().isEmpty()&&!line.trimmed().startsWith('#'))commands<<line.trimmed();
    step=0;pending.clear();canceled=std::make_shared<std::atomic_bool>(false);active=true;++generation;emit busyChanged(true);next();return true;
}
void BuildService::next() {
    if(canceled->load()){complete(false,"Сборка отменена.");return;}
    QString program;QStringList args;
    if(step==0){program="git";args=QStringList{"-C",settings.sourceDirectory,"pull","--ff-only"};emit phaseChanged("Обновление исходников · git pull --ff-only");}
    else if(step<=commands.size()){QString why;Config::tokenize(commands[step-1],&args,&why);program=args.takeFirst();emit phaseChanged(QString("Сборка · команда %1 из %2").arg(step).arg(commands.size()));}
    else {deploy();return;}
    ++step;emit output("\n> "+Config::displayCommand(program,args,false)+"\n");process.setWorkingDirectory(settings.sourceDirectory);process.start(program,args);
}
void BuildService::drain(bool flush) {
    pending+=process.readAllStandardOutput();int end;
    while((end=pending.indexOf('\n'))>=0){emit output(QString::fromUtf8(pending.left(end+1)));pending.remove(0,end+1);}
    if(flush||pending.size()>65536){emit output(QString::fromUtf8(pending));pending.clear();}
}
void BuildService::complete(bool success,const QString &message){if(!active)return;active=false;emit phaseChanged(message);emit output("\n["+message+"]\n");emit busyChanged(false);emit finished(success,message,settings.targetDirectory);}
void BuildService::deploy() {
    emit phaseChanged("Копирование бинарников с заменой файлов…");auto result=std::make_shared<QString>();auto snapshot=settings;auto flag=canceled;
    copyThread=QThread::create([result,snapshot,flag]{*result=deployFiles(snapshot,flag);});
    connect(copyThread,&QThread::finished,this,[this,result]{complete(result->isEmpty(),result->isEmpty()?"Готово: llama.cpp обновлён в "+settings.targetDirectory:*result);});copyThread->start();
}
void BuildService::cancel(){
    if(!active)return;canceled->store(true);
    if(process.state()==QProcess::Starting){emit phaseChanged("Отмена…");return;}
    if(process.state()!=QProcess::NotRunning){
#ifdef Q_OS_WIN
        if(treeKiller.state()==QProcess::NotRunning)treeKiller.start(QDir(qEnvironmentVariable("SystemRoot")).filePath("System32/taskkill.exe"),QStringList{"/PID",QString::number(process.processId()),"/T","/F"});
#else
        process.terminate();
#endif
        const auto run=generation;QTimer::singleShot(3000,this,[this,run]{if(active&&run==generation&&process.state()!=QProcess::NotRunning)process.kill();});
    }
    emit phaseChanged("Отмена…");
}
