#include "gpupower.h"
#include <cmath>
GpuPowerService::GpuPowerService(QObject *parent,const QString &program):QObject(parent),executable(program){
    process.setProcessChannelMode(QProcess::MergedChannels);timeout.setSingleShot(true);
    connect(&timeout,&QTimer::timeout,this,[this]{processError="Превышено время ожидания nvidia-smi. Проверьте фактический лимит кнопкой обновления.";process.killTree();});
    connect(&process,&QProcess::readyReadStandardOutput,this,[this]{const auto bytes=process.readAllStandardOutput();if(output.size()+bytes.size()>256*1024){processError="Слишком большой ответ nvidia-smi.";process.killTree();}else output+=bytes;});
    connect(&process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){if(error==QProcess::FailedToStart&&busy())finish(false,"Не удалось запустить nvidia-smi: "+process.errorString());});
    connect(&process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus exit){
        timeout.stop();output+=process.readAllStandardOutput();if(!busy())return;
        if(!processError.isEmpty()||code||exit!=QProcess::NormalExit){auto reason=processError.isEmpty()?QString::fromUtf8(output).trimmed().left(1600):processError;if(phase==Read){cards.clear();emit readingsChanged();}finish(false,"nvidia-smi: "+reason+"\nДля изменения лимита могут требоваться права администратора Windows. Проверьте поддержку карты и обновите показания.");return;}
        if(phase==Write){begin(Verify);return;}
        cards=parseGpuPowerInfo(QString::fromUtf8(output));emit readingsChanged();
        if(cards.isEmpty()){finish(false,"Драйвер не вернул параметры мощности. Проверьте поддержку nvidia-smi и видеокарты.");return;}
        if(phase==Read){finish(true,"Параметры мощности обновлены. Изменения выполняются только кнопками применения.");return;}
        const auto card=selected();if(!card||!card->adjustable()){finish(false,"У выбранной GPU недоступен диапазон изменения мощности. Лимит не отправлен или не удалось подтвердить результат.");return;}
        if(phase==PrepareWrite){if(factory)requested=card->defaultLimit;if(!std::isfinite(requested)||requested<card->minimum||requested>card->maximum){finish(false,"Лимит вне диапазона, который сейчас сообщает драйвер. Обновите показания.");return;}
            if(batch==ResetBatch&&uncertainOriginal.contains(target)&&std::abs(card->limit-uncertainOriginal.value(target))<=0.5){touched.remove(target);uncertainOriginal.remove(target);finish(true,"Лимит не изменился после неудачной команды; сброс не требуется.");return;}
            if(factory&&std::abs(card->limit-requested)<=0.5){touched.remove(target);uncertainOriginal.remove(target);if(batch!=ResetBatch){profileLimits[target]=card->limit;emit limitApplied(target,card->limit);}finish(true,"Заводской лимит уже установлен; запись не требуется.");return;}
            begin(Write);return;}
        if(std::abs(card->limit-requested)>0.5){finish(false,QString("Запрошено %1 Вт, драйвер сообщает %2 Вт. Изменение не подтверждено.").arg(requested,0,'f',1).arg(card->limit,0,'f',1));return;}
        if(card->defaultLimit>0&&std::abs(card->limit-card->defaultLimit)<=0.5)touched.remove(target);
        uncertainOriginal.remove(target);
        if(batch!=ResetBatch){profileLimits[target]=card->limit;emit limitApplied(target,card->limit);}
        finish(true,QString("GPU %1: подтверждён лимит %2 Вт. Для сравнения скорости выполните новый такой же запрос к модели.").arg(target).arg(card->limit,0,'f',1));
    });
}
GpuPowerService::~GpuPowerService(){timeout.stop();disconnect(&process,nullptr,this,nullptr);process.killTree();if(process.state()!=QProcess::NotRunning)process.waitForFinished(1000);}
const GpuPowerInfo *GpuPowerService::selected()const{for(const auto &card:cards)if(card.uuid==target)return &card;return nullptr;}
void GpuPowerService::finish(bool success,const QString &message){timeout.stop();phase=Idle;if(!success&&batch!=NoBatch)batchErrors<<target+": "+message;emit busyChanged(busy());emit completed(success,message);QTimer::singleShot(0,this,&GpuPowerService::next);}
void GpuPowerService::begin(Phase next){
    phase=next;output.clear();processError.clear();QStringList args;
    if(next==Write){if(!touched.contains(target)&&selected())uncertainOriginal[target]=selected()->limit;touched.insert(target);args=QStringList{"-i",target,"-pl",QString::number(requested,'f',1)};}
    else args=QStringList{"--query-gpu=uuid,name,power.draw,power.limit,power.default_limit,power.min_limit,power.max_limit,temperature.gpu,utilization.gpu","--format=csv,noheader,nounits"};
    timeout.start(5000);process.start(executable,args);
}
void GpuPowerService::refresh(){if(busy())return;if(executable.isEmpty()){emit completed(false,"nvidia-smi не найден. Управление мощностью доступно для поддерживаемых NVIDIA GPU с установленным драйвером.");return;}phase=Read;emit busyChanged(true);begin(Read);}
void GpuPowerService::request(const QString &uuid,double watts,bool reset){if(busy())return;target=uuid;const auto card=selected();if(!card||!card->adjustable()||(!reset&&(!std::isfinite(watts)||watts<card->minimum||watts>card->maximum))){emit completed(false,"Выберите доступную GPU и значение в диапазоне драйвера.");return;}requested=watts;factory=reset;phase=PrepareWrite;emit busyChanged(true);begin(PrepareWrite);}
void GpuPowerService::apply(const QString &uuid,double watts){request(uuid,watts,false);}
void GpuPowerService::restoreDefault(const QString &uuid){request(uuid,0,true);}
void GpuPowerService::applyProfile(const QJsonObject &limits){profileLimits=limits;profilePending=true;emit busyChanged(true);next();}
void GpuPowerService::restoreAllDefaults(){resetPending=true;profilePending=false;emit busyChanged(true);next();}
void GpuPowerService::next(){
    if(phase!=Idle)return;
    if(resetPending){resetPending=false;if(batch==ProfileBatch)emit profileFinished(false,"Применение профиля отменено при выходе.");batch=ResetBatch;queue.clear();batchErrors.clear();for(const auto &id:touched)queue[id]=0;}
    if(batch==NoBatch&&profilePending){profilePending=false;batch=ProfileBatch;queue.clear();batchErrors.clear();for(auto i=profileLimits.begin();i!=profileLimits.end();++i){const auto value=i.value().toDouble(-1);if(!std::isfinite(value)||value<=0||queue.size()>=64)batchErrors<<"Неверный лимит: "+i.key();else queue[i.key()]=value;}}
    if(batch==NoBatch)return;
    if(queue.isEmpty()){const auto finished=batch;batch=NoBatch;const auto error=batchErrors.join("\n");emit busyChanged(busy());if(finished==ResetBatch)emit resetFinished(error.isEmpty(),error);else emit profileFinished(error.isEmpty(),error);QTimer::singleShot(0,this,&GpuPowerService::next);return;}
    const auto item=queue.begin();target=item.key();requested=item.value();queue.erase(item);factory=batch==ResetBatch;begin(PrepareWrite);
}
