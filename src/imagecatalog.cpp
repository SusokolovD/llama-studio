#include "imagecatalog.h"
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QRegularExpression>
#include <limits>
#include <algorithm>
#include <cmath>

namespace {
QString key(const QString &path){auto result=QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
    result=result.toCaseFolded();
#endif
    return result;
}
QJsonObject load(const QString &path){QFile file(path);if(!file.open(QIODevice::ReadOnly)||file.size()>32*1024*1024)return {};auto json=QJsonDocument::fromJson(file.readAll()).object();return json.value("schemaVersion")==QJsonValue(1)?json:QJsonObject();}
struct Header {
    QFile file;QDataStream stream;bool ok=true;QMap<QString,QString> metadata;QMap<QString,QVector<quint64>> tensors;
    explicit Header(const QString &path):file(path),stream(&file){stream.setByteOrder(QDataStream::LittleEndian);}
    bool bound(quint64 count=0){return ok=ok&&stream.status()==QDataStream::Ok&&file.pos()>=0&&quint64(file.pos())<=64*1024*1024&&file.size()>=file.pos()&&count<=quint64(file.size()-file.pos())&&count<=64*1024*1024-quint64(file.pos());}
    QString string(){quint64 count=0;stream>>count;if(!bound(count)||count>1024*1024){ok=false;return {};}auto bytes=file.read(qint64(count));if(quint64(bytes.size())!=count)ok=false;return QString::fromUtf8(bytes);}
    void value(quint32 type,const QString &name={}){
        static const int sizes[]={1,1,2,2,4,4,4,1,0,0,8,8,8};
        if(type>12||!bound()){ok=false;return;}
        if(type==8){auto text=string();if(!name.isEmpty())metadata[name]=text;}
        else if(type==9){quint32 element=0;quint64 count=0;stream>>element>>count;if(element>12||element==9||count>2000000){ok=false;return;}if(sizes[element]){const auto bytes=count*quint64(sizes[element]);if(bound(bytes))ok=file.seek(file.pos()+qint64(bytes));}else for(quint64 i=0;i<count&&ok;++i)value(element);}
        else if(type==4){quint32 number=0;stream>>number;if(!name.isEmpty())metadata[name]=QString::number(number);}
        else {if(bound(sizes[type]))ok=file.seek(file.pos()+sizes[type]);}
    }
    bool read(){
        if(!file.open(QIODevice::ReadOnly))return false;
        if(file.read(4)=="GGUF"){
            quint32 version=0;quint64 count=0,values=0;stream>>version>>count>>values;if((version!=2&&version!=3)||!count||count>50000||values>100000)return false;
            for(quint64 i=0;i<values&&ok;++i){auto name=string();quint32 type=0;stream>>type;value(type,name.startsWith("general.")||name.endsWith("embedding_length")?name:QString());}
            quint64 maximumOffset=0;
            for(quint64 i=0;i<count&&ok;++i){auto name=string();quint32 dimensions=0;stream>>dimensions;if(dimensions>6){ok=false;break;}QVector<quint64> shape;for(quint32 d=0;d<dimensions;++d){quint64 size=0;stream>>size;if(!size||size>quint64(1)<<40)ok=false;shape.prepend(size);}quint32 type=0;quint64 offset=0;stream>>type>>offset;maximumOffset=qMax(maximumOffset,offset);if(tensors.contains(name)||!bound())ok=false;tensors[name]=shape;}
            bool alignmentOk=false;const auto alignment=metadata.value("general.alignment","32").toULongLong(&alignmentOk);if(!alignmentOk||!alignment||alignment>4096)return false;const auto header=quint64(file.pos());const auto start=((header+alignment-1)/alignment)*alignment;
            return ok&&stream.status()==QDataStream::Ok&&start<quint64(file.size())&&maximumOffset<quint64(file.size())-start;
        }
        file.seek(0);quint64 length=0;stream>>length;if(length<2||length>32*1024*1024||!bound(length))return false;QJsonParseError error;const auto doc=QJsonDocument::fromJson(file.read(qint64(length)),&error);if(error.error!=QJsonParseError::NoError||!doc.isObject())return false;auto json=doc.object();auto meta=json.take("__metadata__").toObject();for(auto it=meta.begin();it!=meta.end();++it)metadata[it.key()]=it.value().toString();if(json.isEmpty()||json.size()>50000)return false;
        const auto dataBytes=file.size()-qint64(length)-8;
        for(auto it=json.begin();it!=json.end();++it){auto value=it.value().toObject();if(!value.value("shape").isArray()||!value.value("data_offsets").isArray()||!value.value("dtype").isString())return false;auto offsets=value.value("data_offsets").toArray();if(offsets.size()!=2)return false;const auto from=offsets[0].toDouble(-1),to=offsets[1].toDouble(-1);if(from<0||to<from||to>double(dataBytes)||from!=std::floor(from)||to!=std::floor(to))return false;QVector<quint64> shape;for(auto dim:value.value("shape").toArray()){const auto n=dim.toDouble(-1);if(n<1||n>double(quint64(1)<<40)||n!=std::floor(n))return false;shape<<quint64(n);}if(shape.size()>6)return false;tensors[it.key()]=shape;}
        return true;
    }
    bool has(const QString &suffix)const{for(auto it=tensors.begin();it!=tensors.end();++it)if(it.key()==suffix||it.key().endsWith('.'+suffix))return true;return false;}
    QVector<quint64> shape(const QString &suffix)const{for(auto it=tensors.begin();it!=tensors.end();++it)if(it.key()==suffix||it.key().endsWith('.'+suffix))return it.value();return {};}
};
}
QJsonObject ImageAsset::json()const{return {{"path",path},{"kind",kind},{"family",family},{"evidence",evidence},{"error",error},{"bytes",QString::number(bytes)},{"trusted",trusted}};}
ImageAsset ImageAsset::fromJson(const QJsonObject &j){return {j.value("path").toString(),j.value("kind").toString("unknown"),j.value("family").toString(),j.value("evidence").toString(),j.value("error").toString(),j.value("bytes").toString().toLongLong(),j.value("trusted").toBool()};}
ImageAsset inspectImageAsset(const QString &path){
    ImageAsset result;result.path=QFileInfo(path).absoluteFilePath();result.bytes=QFileInfo(path).size();
    if(path.endsWith(".part",Qt::CaseInsensitive)||path.endsWith(".partial",Qt::CaseInsensitive)){result.error="Загрузка не завершена";return result;}
    if(QFileInfo(path).suffix().compare("ckpt",Qt::CaseInsensitive)==0){result.error="CKPT: безопасное чтение заголовка недоступно; выберите компоненты вручную";return result;}
    Header h(path);if(!h.read()){result.error="Повреждённый, неполный или неподдерживаемый заголовок весов";return result;}
    const auto arch=h.metadata.value("general.architecture").toLower(),spec=h.metadata.value("modelspec.architecture").toLower();
    auto set=[&](const QString &kind,const QString &family,const QString &why){result.kind=kind;result.family=family;result.trusted=true;result.evidence=why;};
    if((arch=="qwen3vl"||arch=="qwen3_vl")&&h.has("token_embd.weight")){
        const auto embedding=h.metadata.value("qwen3vl.embedding_length").toInt();if(embedding==2560)set("encoder","qwen3vl4b","GGUF: Qwen3-VL, embedding 2560 (4B)");else if(embedding==4096)set("encoder","qwen3vl8b","GGUF: Qwen3-VL, embedding 4096 (8B)");
    }else if(h.has("content_mode_blocks.image.0.q_proj.weight")&&h.has("txt_in.weight"))set("model","flux3","Тензоры content_mode_blocks.image и txt_in: FLUX.3");
    else if(arch=="qwen_image21"||arch=="qwen_image_2_1"||((h.has("transformer_blocks.0.img_mlp.gate_layer.weight"))&&h.has("img_in.weight")))set("model","qwen21","Метаданные/структура transformer_blocks.img_mlp: Qwen Image 2.1");
    else if(spec=="qwen_image_2.1_vae"||((h.shape("decoder.conv1.weight").size()==5&&h.shape("decoder.conv1.weight").value(1)==64)&&h.shape("encoder.conv1.weight").value(1)==4))set("vae","qwen21vae","VAE Qwen Image 2.1: 64 latent-канала / RGBA encoder");
    else if(h.shape("decoder.conv_in.weight").size()==4&&h.shape("decoder.conv_in.weight").value(1)==32&&h.has("bn.running_mean"))set("vae","flux2vae","VAE FLUX.2: 32 latent-канала и batch norm");
    else if(h.has("model.diffusion_model.input_blocks.1.1.transformer_blocks.0.attn2.to_k.weight")&&h.shape("model.diffusion_model.input_blocks.1.1.transformer_blocks.0.attn2.to_k.weight").value(1)==1024&&h.has("cond_stage_model.transformer.text_model.embeddings.token_embedding.weight")&&h.has("first_stage_model.decoder.conv_in.weight"))set("model","sd21","Полный SD 2.x checkpoint: UNet context 1024 + text encoder + VAE");
    if(result.family.isEmpty()){result.evidence="Заголовок прочитан; семейство не определено. Автоподбор недоступен";if(h.has("decoder.conv_in.weight")||h.has("decoder.conv1.weight"))result.kind="vae";else if(h.has("token_embd.weight"))result.kind="encoder";}
    return result;
}
QString imageFamilyLabel(const QString &family){static const QMap<QString,QString> labels={{"sd21","SD 2.x checkpoint"},{"flux3","FLUX.3 Image Klein"},{"qwen21","Qwen Image 2.1"},{"qwen3vl4b","Qwen3-VL 4B"},{"qwen3vl8b","Qwen3-VL 8B"},{"flux2vae","FLUX.2 VAE"},{"qwen21vae","Qwen Image 2.1 VAE"}};return labels.value(family,"Не определено");}
QString imageKindLabel(const QString &kind){return kind=="model"?"Модель":kind=="encoder"?"Энкодер":kind=="vae"?"VAE":"Не определено";}
QStringList imageDependencies(const QString &family){if(family=="flux3")return {"qwen3vl4b","flux2vae"};if(family=="qwen21")return {"qwen3vl8b","qwen21vae"};return {};}
QVector<ImageAsset> imageCandidates(const QVector<ImageAsset> &assets,const QString &family){QVector<ImageAsset> result;for(const auto &asset:assets)if(asset.family==family&&asset.trusted&&asset.error.isEmpty())result<<asset;std::sort(result.begin(),result.end(),[](const ImageAsset &a,const ImageAsset &b){return a.path<b.path;});return result;}
ImageCatalog::ImageCatalog(const QString &path):filePath(path){}
QString ImageCatalog::defaultPath(){return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("image-models-cache.json");}
QVector<ImageAsset> ImageCatalog::cached(const QString &directory,bool *found)const{const auto dirs=load(filePath).value("directories").toObject();const auto id=key(directory);if(found)*found=dirs.contains(id);QVector<ImageAsset> result;for(auto value:dirs.value(id).toObject().value("assets").toArray())result<<ImageAsset::fromJson(value.toObject().value("info").toObject());return result;}
QVector<ImageAsset> ImageCatalog::scan(const QString &directory,const std::function<bool()> &cancel,const std::function<void(int,const QString &)> &progress,QString *error){
    if(!QFileInfo(directory).isDir()){if(error)*error="Укажите существующую папку моделей";return {};}
    const auto id=key(directory);auto data=load(filePath);QMap<QString,QJsonObject> previous;for(auto value:data.value("directories").toObject().value(id).toObject().value("assets").toArray()){auto entry=value.toObject();previous[key(entry.value("info").toObject().value("path").toString())]=entry;}
    QVector<ImageAsset> result;QJsonArray entries;QDirIterator files(directory,QDir::Files|QDir::Readable|QDir::NoSymLinks,QDirIterator::Subdirectories);
    while(files.hasNext()){
        if(cancel())return {};const auto path=files.next();const auto name=QFileInfo(path).fileName().toLower();if(!QRegularExpression("\\.(gguf|safetensors|ckpt)(\\.part|\\.partial)?$").match(name).hasMatch())continue;
        QFileInfo file(path);const auto stamp=QJsonObject{{"bytes",QString::number(file.size())},{"modified",QString::number(file.lastModified().toMSecsSinceEpoch())}};const auto old=previous.value(key(path));auto info=old.value("fingerprint").toObject()==stamp?ImageAsset::fromJson(old.value("info").toObject()):inspectImageAsset(path);file.refresh();const auto after=QJsonObject{{"bytes",QString::number(file.size())},{"modified",QString::number(file.lastModified().toMSecsSinceEpoch())}};
        if(stamp!=after){info.error="Файл меняется во время сканирования; повторите после завершения загрузки";info.trusted=false;}result<<info;entries.append(QJsonObject{{"fingerprint",stamp==after?stamp:QJsonObject()},{"info",info.json()}});progress(result.size(),file.fileName());
        if(result.size()>=50000){if(error)*error="Найдено 50000 файлов; сузьте папку для полного каталога";return result;}
    }
    if(cancel())return {};if(!QDir().mkpath(QFileInfo(filePath).absolutePath())){if(error)*error="Не удалось создать папку JSON-кэша";return result;}QLockFile lock(filePath+".lock");if(!lock.tryLock(1000)){if(error)*error="JSON-кэш занят другим окном";return result;}data=load(filePath);auto dirs=data.value("directories").toObject();dirs[id]=QJsonObject{{"assets",entries},{"updatedAt",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};data["directories"]=dirs;data["schemaVersion"]=1;QSaveFile out(filePath);auto bytes=QJsonDocument(data).toJson();if(!out.open(QIODevice::WriteOnly)||out.write(bytes)!=bytes.size()||!out.commit()){if(error)*error="Не удалось записать JSON-кэш: "+out.errorString();}return result;
}
QStringList parseImageDevices(const QString &output){QStringList result;for(auto line:output.split('\n')){line=line.trimmed();if(!line.contains('\t'))continue;const auto id=line.section('\t',0,0);if(QRegularExpression("^[A-Za-z][A-Za-z0-9_.:-]*$").match(id).hasMatch()&&!id.startsWith("cpu",Qt::CaseInsensitive)&&!result.contains(id))result<<id;}return result;}
ImageFit makeImageFit(const QVector<ImageAsset> &components,const QStringList &devices,int reserveMiB,qint64 availableRamBytes,int width,int height){
    ImageFit fit;for(const auto &asset:components)fit.weightBytes+=qMin(qMax<qint64>(0,asset.bytes),std::numeric_limits<qint64>::max()-fit.weightBytes);const auto reserve=qBound(0,reserveMiB,65536)/1024.0;const auto budget=reserve>0?QString::number(-reserve,'f',3):QString("0");fit.maxVram=budget;
    if(devices.size()==1&&devices.first()=="cpu"){fit.backend="cpu";fit.maxVram="0";}else if(!devices.isEmpty()){fit.backend="diffusion="+devices.join('&')+",te=cpu,vae="+devices.first();QStringList limits;for(const auto &device:devices)limits<<device+'='+budget;fit.maxVram=limits.join(',');}
    else fit.backend="te=cpu";
    fit.notes<<QString("Весовые файлы: %1 ГиБ. Это размер файлов, не точный расход RAM или VRAM.").arg(fit.weightBytes/1073741824.0,0,'f',2);
    if(availableRamBytes>0){fit.notes<<QString("Свободная RAM сейчас: %1 ГиБ.").arg(availableRamBytes/1073741824.0,0,'f',2);if(availableRamBytes<fit.weightBytes)fit.notes<<"Свободной RAM меньше размера весов: высокий риск активного pagefile или ошибки загрузки.";}
    fit.notes<<QString("Разрешение: %1×%2; резерв каждой выбранной GPU: %3 МиБ. Энкодер — CPU, offload и VAE tiling включены.").arg(width).arg(height).arg(reserveMiB);
    fit.notes<<"Точный расход зависит от графа, разрешения, промпта, кэшей и backend. --auto-fit on рассчитает размещение весов/буферов при загрузке и генерации; отдельного dry-run fit у этой сборки нет. Это подбор по памяти, не измерение скорости или качества.";
    return fit;
}
