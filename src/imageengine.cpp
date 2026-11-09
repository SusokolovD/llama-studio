#include "imageengine.h"
#include "config.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QRegularExpression>
#include <QTcpServer>
#include <cmath>

QJsonObject ImageSettings::defaults() {
    static const auto initialized=[] { Q_INIT_RESOURCE(image_profiles);return true; }();Q_UNUSED(initialized);
    QFile external(QDir(QCoreApplication::applicationDirPath()).filePath("image-profiles.json"));
    if(external.open(QIODevice::ReadOnly)&&external.size()<=1024*1024){const auto json=QJsonDocument::fromJson(external.readAll()).object();if(json.value("schemaVersion")==QJsonValue(1)&&json.value("profiles").isArray()&&!json.value("profiles").toArray().isEmpty())return json;}
    QFile file(":/image-profiles.json");
    if(!file.open(QIODevice::ReadOnly))return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
ImageSettings ImageSettings::fromJson(const QJsonObject &json) {
    auto data=defaults();for(auto it=json.begin();it!=json.end();++it)data[it.key()]=it.value();
    return {data.value("rootDirectory").toString(),data.value("executablePath").toString(),data.value("modelDirectory").toString(),data.value("listenIp").toString(),data.value("selectedProfile").toString(),data.value("port").toInt(),data.value("profiles").toArray()};
}
QJsonObject ImageSettings::toJson() const {
    return {{"schemaVersion",1},{"rootDirectory",rootDirectory},{"executablePath",executablePath},{"modelDirectory",modelDirectory},{"listenIp",listenIp},{"port",port},{"selectedProfile",selectedProfile},{"profiles",profiles}};
}
QJsonObject ImageSettings::profile() const {for(auto value:profiles){const auto p=value.toObject();if(p.value("id").toString()==selectedProfile)return p;}return {};}
QString ImageSettings::rootPath() const {return QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QDir::fromNativeSeparators(rootDirectory)));}
QString ImageSettings::resolve(const QString &path) const {return path.trimmed().isEmpty()?QString():QDir::cleanPath(QDir(rootPath()).absoluteFilePath(QDir::fromNativeSeparators(path)));}
QStringList ImageSettings::componentProblems() const {
    QStringList problems;
    if(rootDirectory.trimmed().isEmpty()||!QFileInfo(rootPath()).isDir())problems<<"Выберите существующую папку окружения stable-diffusion.cpp. Относительный путь считается от папки LlamaStudio.";
    auto check=[&](const QString &label,const QString &path){
        if(path.trimmed().isEmpty()){problems<<label+": укажите файл компонента.";return;}
        const QFileInfo file(resolve(path));
        if(path.trimmed().isEmpty()||!file.isFile()||file.size()==0){
            auto part=file.absoluteFilePath()+".part";
            problems<<label+": "+(QFileInfo(part).isFile()?QString("загрузка не завершена (%1 МиБ в .part). Докачайте компонент и проверьте его целостность.").arg(QFileInfo(part).size()/1048576):"файл отсутствует или пуст: "+file.filePath());return;
        }
        if(path.endsWith(".part",Qt::CaseInsensitive)||path.endsWith(".partial",Qt::CaseInsensitive)){problems<<label+": выбран незавершённый файл загрузки.";return;}
        QFile manifest(QDir(rootPath()).filePath("assets.json"));
        if(manifest.open(QIODevice::ReadOnly)&&manifest.size()<=1024*1024){for(auto entry:QJsonDocument::fromJson(manifest.readAll()).array()){
            auto asset=entry.toObject();const auto expected=resolve(asset.value("target").toString());
            if(expected.compare(file.absoluteFilePath(),Qt::CaseInsensitive)==0&&asset.value("size").toDouble()>0&&file.size()!=qint64(asset.value("size").toDouble()))problems<<label+": размер отличается от assets.json. Завершите загрузку и проверку компонента.";
        }}
    };
    check("sd-server",executablePath);
    const auto p=profile();if(p.isEmpty()){problems<<"Выберите профиль модели.";return problems;}
    check("Модель",p.value("model").toString());
    for(const auto &key:QStringList{"llm","vae"})if(!p.value(key).toString().trimmed().isEmpty()||p.value("requiredComponents").toArray().contains(key))check(key=="llm"?"Текстовый энкодер":"VAE",p.value(key).toString());
    if(modelDirectory.trimmed().isEmpty()||!QFileInfo(resolve(modelDirectory)).isDir())problems<<"Папка моделей для встроенного веб-интерфейса не найдена.";
    return problems;
}
bool ImageSettings::command(QString *executable,QStringList *arguments,QUrl *url,QString *error,bool checkFiles) const {
    const auto fail=[&](const QString &why){if(error)*error=why;return false;};
    if(!executable||!arguments||!url)return fail("Не задан получатель команды.");
    const auto p=profile();if(p.isEmpty())return fail("Профиль модели не найден.");
    QHostAddress address;if(!address.setAddress(listenIp)||port<1||port>65535)return fail("Укажите IP-адрес сервера и порт от 1 до 65535.");
    const auto mode=p.value("modelMode").toString();if(mode!="checkpoint"&&mode!="diffusion")return fail("Выберите тип модели: полный checkpoint или отдельная diffusion-модель.");
    const auto integer=[&](const QString &key,int low,int high){const auto v=p.value(key);return v.isDouble()&&std::isfinite(v.toDouble())&&std::floor(v.toDouble())==v.toDouble()&&v.toDouble()>=low&&v.toDouble()<=high;};
    if(!integer("width",64,8192)||!integer("height",64,8192)||p.value("width").toInt()%32||p.value("height").toInt()%32)return fail("Ширина и высота должны быть от 64 до 8192 и делиться на 32. Для 8 ГБ VRAM начните с 512×512.");
    if(!integer("steps",1,1000)||!integer("threads",-1,1024))return fail("Шаги: 1–1000; потоки CPU: -1 (авто) или 0–1024.");
    const auto cfg=p.value("cfgScale");if(!cfg.isDouble()||!std::isfinite(cfg.toDouble())||cfg.toDouble()<0||cfg.toDouble()>100)return fail("CFG должен быть числом от 0 до 100.");
    bool seedOk=false;const auto seed=p.value("seed").toVariant().toString();seed.toLongLong(&seedOk);if(!seedOk)return fail("Seed должен быть целым 64-битным числом; -1 — случайный.");
    if(p.value("maxVram").toString().trimmed().isEmpty())return fail("Укажите бюджет VRAM; 0 — автоматический выбор по свободной памяти.");
    QStringList args{mode=="checkpoint"?"--model":"--diffusion-model",resolve(p.value("model").toString())};
    for(const auto &key:QStringList{"llm","vae"})if(!p.value(key).toString().trimmed().isEmpty())args<<"--"+key<<resolve(p.value(key).toString());
    args<<"--max-vram"<<p.value("maxVram").toString();
    const auto fit=p.value("autoFit").toString();if(!fit.isEmpty()){if(fit!="on"&&fit!="off")return fail("Fit: допустимы on, off или пустое значение по умолчанию движка.");args<<"--auto-fit"<<fit;}
    if(p.value("offloadToCpu").toBool())args<<"--offload-to-cpu";
    if(p.value("vaeTiling").toBool())args<<"--vae-tiling";
    if(!p.value("backend").toString().trimmed().isEmpty())args<<"--backend"<<p.value("backend").toString();
    args<<"--cfg-scale"<<QString::number(cfg.toDouble(),'g',10)<<"--steps"<<QString::number(p.value("steps").toInt())<<"--width"<<QString::number(p.value("width").toInt())<<"--height"<<QString::number(p.value("height").toInt())<<"--seed"<<seed<<"--threads"<<QString::number(p.value("threads").toInt());
    for(const auto &key:QStringList{"samplingMethod","scheduler"})if(!p.value(key).toString().trimmed().isEmpty())args<<(key=="scheduler"?"--scheduler":"--sampling-method")<<p.value(key).toString();
    args<<"--listen-ip"<<listenIp<<"--listen-port"<<QString::number(port)<<"--model-dir"<<resolve(modelDirectory);
    if(!p.value("extraArguments").isArray())return fail("Дополнительные аргументы должны быть JSON-массивом строк.");
    const QStringList managed{"--model","-m","--diffusion-model","--llm","--qwen2vl","--vae","--listen-ip","-l","--listen-port","--model-dir","--max-vram","--backend","--auto-fit","--offload-to-cpu","--vae-tiling","--cfg-scale","--steps","--width","-W","--height","-H","--seed","-s","--threads","-t","--sampling-method","--scheduler","--help","-h","--version","--list-devices"};
    for(auto value:p.value("extraArguments").toArray()){
        if(!value.isString()||value.toString().contains(QChar(0)))return fail("Дополнительные аргументы должны содержать только строки без нулевых символов.");
        const auto argument=value.toString();if(managed.contains(argument.section('=',0,0)))return fail("Параметр "+argument+" задаётся отдельным полем и не может переопределяться в дополнительных аргументах.");args<<argument;
    }
    if(checkFiles){const auto problems=componentProblems();if(!problems.isEmpty())return fail(problems.join('\n'));}
    *executable=resolve(executablePath);*arguments=args;
    QUrl endpoint;endpoint.setScheme("http");endpoint.setHost(address==QHostAddress::Any||address==QHostAddress::AnyIPv4?"127.0.0.1":address==QHostAddress::AnyIPv6?"::1":address.toString());endpoint.setPort(port);endpoint.setPath("/");*url=endpoint;return true;
}

ImageServer::ImageServer(QObject *parent):QObject(parent) {
    network.setProxy(QNetworkProxy::NoProxy);healthTimer.setInterval(1000);
    connect(&healthTimer,&QTimer::timeout,this,&ImageServer::poll);
    connect(&process,&QProcess::readyReadStandardOutput,this,[this]{read(true);});
    connect(&process,&QProcess::readyReadStandardError,this,[this]{read(false);});
    connect(&process,&QProcess::started,this,[this]{if(stopping){kill();return;}setStatus("Загрузка модели изображений…");healthTimer.start();poll();});
    connect(&process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,&ImageServer::finish);
    connect(&process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){emit output("\n[Ошибка sd-server] "+process.errorString()+"\n");if(error==QProcess::FailedToStart){active=false;modelReady=webReady=false;healthTimer.stop();setStatus("Ошибка запуска sd-server: "+process.errorString());}});
}
ImageServer::~ImageServer(){disconnect(&process,nullptr,this,nullptr);healthTimer.stop();if(reply){disconnect(reply,nullptr,this,nullptr);reply->abort();}process.killTree();}
void ImageServer::setStatus(const QString &text){stateText=text;emit changed();}
bool ImageServer::start(const ImageSettings &settings,QString *error) {
    if(active){if(error)*error="Сервер изображений уже запущен.";return false;}
    QString executable;QStringList args;QUrl endpoint;if(!settings.command(&executable,&args,&endpoint,error))return false;
    QTcpServer probe;if(!probe.listen(QHostAddress(settings.listenIp),quint16(settings.port))){if(error)*error="Нельзя открыть порт "+QString::number(settings.port)+": "+probe.errorString()+". Остановите другой сервер или измените порт.";return false;}probe.close();
    ++run;if(reply)reply->abort();runningUrl=endpoint;active=true;stopping=false;modelReady=webReady=false;pendingOut.clear();pendingError.clear();launchedCommand=Config::displayCommand(executable,args,false);
    emit output("\n[Запуск "+settings.profile().value("name").toString()+"]\n> "+launchedCommand+"\n");setStatus("Запуск sd-server…");
    process.setWorkingDirectory(settings.rootPath());process.start(executable,args);if(!active&&error)*error=process.errorString();return active;
}
void ImageServer::read(bool out,bool finish) {
    auto &pending=out?pendingOut:pendingError;pending+=out?process.readAllStandardOutput():process.readAllStandardError();
    const auto send=[this](const QByteArray &bytes){auto text=QString::fromUtf8(bytes);text.remove(QRegularExpression("\x1b\\[[0-9;]*[A-Za-z]"));emit output(text);};
    int end;while((end=pending.indexOf('\n'))>=0){send(pending.left(end+1));pending.remove(0,end+1);}if(finish||pending.size()>65536){send(pending);pending.clear();}
}
void ImageServer::finish(int code,QProcess::ExitStatus exit) {
    healthTimer.stop();++run;if(reply)reply->abort();read(true,true);read(false,true);active=false;modelReady=webReady=false;
    emit output(QString("\n[sd-server завершён: код %1, %2]\n").arg(code).arg(exit==QProcess::CrashExit?"аварийный выход":"обычный выход"));setStatus(stopping?"Сервер изображений остановлен":code==0&&exit==QProcess::NormalExit?"Сервер изображений завершён":"Ошибка сервера изображений · код "+QString::number(code));
}
void ImageServer::poll() {
    if(reply||!active||stopping||process.state()!=QProcess::Running)return;
    auto endpoint=runningUrl;endpoint.setPath("/sdcpp/v1/models/status");auto request=QNetworkRequest(endpoint);request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);reply=network.get(request);auto current=reply.data();auto generation=run;
    QTimer::singleShot(2000,current,[current]{if(current->isRunning())current->abort();});
    connect(current,&QNetworkReply::readyRead,this,[current]{if(current->bytesAvailable()>256*1024)current->abort();});
    connect(current,&QNetworkReply::finished,this,[this,current,generation]{
        if(reply==current)reply.clear();
        if(generation==run&&active&&!stopping&&process.state()==QProcess::Running){
            const auto bytes=current->isOpen()?current->read(256*1024+1):QByteArray();const auto data=QJsonDocument::fromJson(bytes).object();const auto state=data.value("state").toString();
            const bool valid=current->error()==QNetworkReply::NoError&&current->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()==200&&bytes.size()<=256*1024&&QStringList{"empty","loading","ready","error"}.contains(state);
            modelReady=valid&&state=="ready";webReady=valid;
            setStatus(!valid?"Ожидание готовности sd-server…":modelReady?"Модель изображений готова · "+runningUrl.toString():state=="empty"?"Веб-интерфейс доступен · модель выгружена":state=="error"?"Ошибка модели: "+data.value("error").toString().left(500):"Сервер доступен · загрузка модели…");
        }current->deleteLater();
    });
}
void ImageServer::stop() {
    if(!active)return;stopping=true;modelReady=webReady=false;healthTimer.stop();++run;if(reply)reply->abort();setStatus("Остановка sd-server…");process.terminate();const auto generation=run;
    QTimer::singleShot(3000,this,[this,generation]{if(active&&generation==run)kill();});
}
void ImageServer::kill(){if(active){stopping=true;modelReady=webReady=false;healthTimer.stop();++run;if(reply)reply->abort();setStatus("Остановка sd-server…");}process.killTree();}
