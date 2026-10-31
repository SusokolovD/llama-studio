#include "advisor.h"
#include <QFile>
#include <QFileInfo>
#include <QDataStream>
#include <QRegularExpression>
#include <QSet>
#include <QVariant>
#include <limits>
#include <cmath>

namespace {
class GgufReader {
public:
    QFile file; QDataStream stream; bool ok = true;
    explicit GgufReader(const QString &path) : file(path), stream(&file) { stream.setByteOrder(QDataStream::LittleEndian); }
    bool bounded(quint64 n) { return ok = ok && stream.status() == QDataStream::Ok && n <= quint64(qMax<qint64>(0, file.size() - file.pos())) && quint64(file.pos()) + n <= 256ULL * 1024 * 1024; }
    bool skip(quint64 n) { return bounded(n) && (ok = file.seek(file.pos() + qint64(n))); }
    QString string(bool keep = true) {
        quint64 n = 0; stream >> n;
        if (!bounded(n) || n > 16 * 1024 * 1024) { ok = false; return {}; }
        if (!keep) { skip(n); return {}; }
        auto data = file.read(qint64(n)); ok &= quint64(data.size()) == n; return QString::fromUtf8(data);
    }
    QVariant value(quint32 type, bool keep, int depth = 0) {
        if (!ok || depth > 2 || !bounded(0)) { ok = false; return {}; }
        switch (type) {
        case 0: { quint8 v=0; stream>>v; return v; }
        case 1: { qint8 v=0; stream>>v; return v; }
        case 2: { quint16 v=0; stream>>v; return v; }
        case 3: { qint16 v=0; stream>>v; return v; }
        case 4: { quint32 v=0; stream>>v; return v; }
        case 5: { qint32 v=0; stream>>v; return v; }
        case 6: { quint32 bits=0; stream>>bits; return {}; }
        case 7: { quint8 v=0; stream>>v; return bool(v); }
        case 8: return string(keep);
        case 9: {
            quint32 element=0; quint64 count=0; stream >> element >> count;
            if (count > 2000000 || element > 12 || element == 9) { ok = false; return {}; }
            static const int bytes[] = {1,1,2,2,4,4,4,1,0,0,8,8,8};
            if (bytes[element]) skip(count * bytes[element]);
            else for (quint64 i=0; i<count && ok; ++i) value(element, false, depth + 1);
            return {};
        }
        case 10: { quint64 v=0; stream>>v; if (v > quint64(std::numeric_limits<qint64>::max())) return {}; return QVariant::fromValue(qint64(v)); }
        case 11: { qint64 v=0; stream>>v; return QVariant::fromValue(v); }
        case 12: { quint64 bits=0; stream>>bits; return {}; }
        default: ok=false; return {};
        }
    }
};
}
ModelInfo readModelInfo(const QString &path) {
    ModelInfo info; info.path = path; info.fileBytes = QFileInfo(path).size();
    GgufReader r(path);
    if (!r.file.open(QIODevice::ReadOnly)) { info.error = "Не удалось открыть GGUF: " + r.file.errorString(); return info; }
    if (r.file.read(4) != "GGUF") { info.error = "Файл не является GGUF с порядком байтов little-endian."; return info; }
    quint32 version=0; quint64 tensors=0, count=0; r.stream >> version >> tensors >> count;
    if ((version != 2 && version != 3) || count > 100000) { info.error = "Неподдерживаемая версия или повреждённый заголовок GGUF."; return info; }
    QMap<QString,QVariant> values;
    for (quint64 i=0; i<count && r.ok; ++i) {
        auto key=r.string(); quint32 type=0; r.stream>>type;
        bool keep=key == "general.name" || key == "general.architecture" || key == "split.count" || key.endsWith(".context_length") || key.endsWith(".block_count") || key.endsWith(".embedding_length") || key.endsWith(".attention.head_count") || key.endsWith(".attention.head_count_kv") || key.endsWith(".attention.key_length") || key.endsWith(".attention.value_length") || key.endsWith(".attention.sliding_window");
        if (key == "tokenizer.chat_template" && type == 8) info.hasChatTemplate=!r.string().trimmed().isEmpty();
        else { auto value=r.value(type,keep); if (keep) values[key]=value; }
    }
    if (!r.ok || r.stream.status()!=QDataStream::Ok) { info.error = "Метаданные GGUF повреждены или превышают безопасные пределы чтения."; return info; }
    info.name=values.value("general.name").toString(); info.architecture=values.value("general.architecture").toString();
    auto arch=info.architecture + ".";
    info.trainingContext=values.value(arch+"context_length").toLongLong(); info.layers=values.value(arch+"block_count").toLongLong();
    info.embedding=values.value(arch+"embedding_length").toLongLong(); info.heads=values.value(arch+"attention.head_count").toLongLong();
    info.kvHeads=values.value(arch+"attention.head_count_kv", QVariant::fromValue(info.heads)).toLongLong();
    info.keyLength=values.value(arch+"attention.key_length").toLongLong();info.valueLength=values.value(arch+"attention.value_length").toLongLong();info.slidingWindow=values.value(arch+"attention.sliding_window").toLongLong();
    info.shards=qMax<qint64>(1,values.value("split.count",1).toLongLong()); return info;
}
QVector<DeviceInfo> parseDevices(const QString &output) {
    QVector<DeviceInfo> devices; QSet<QString> ids;
    QRegularExpression pattern("^\\s*([A-Za-z][A-Za-z0-9_.:-]*):\\s*(.+)\\s+\\((\\d+) MiB, (\\d+) MiB free\\)\\s*$");
    for (auto line : output.split('\n')) {
        auto m=pattern.match(line.trimmed()); if (!m.hasMatch() || ids.contains(m.captured(1))) continue;
        devices.append({m.captured(1),m.captured(2),m.captured(4).toLongLong(),m.captured(3).toLongLong()}); ids.insert(m.captured(1));
    }
    return devices;
}
Advice makeAdvice(const AdviceInput &in) {
    Advice out;
    auto put=[&](QString name,QJsonValue value,bool enabled=true) { out.changes.append({name,value,enabled}); };
    if (in.contextPerSlot<1 || in.slotCount<1 || in.slotCount>32 || qint64(in.contextPerSlot)*in.slotCount>2147483647 || in.reserveMiB<0) { out.error="Недопустимый контекст, число слотов или резерв памяти."; return out; }
    if (in.model.trainingContext>0 && in.contextPerSlot>in.model.trainingContext) { out.error="Контекст одного диалога превышает обучающий контекст GGUF. Уменьшите его; масштабирование требует рекомендаций автора модели."; return out; }
    if (!QStringList{"f16","q8_0","q4_0"}.contains(in.cacheType)) { out.error="Неизвестный формат KV-кэша."; return out; }
    out.totalContext=in.contextPerSlot*in.slotCount;
    if (!in.model.path.isEmpty() && QFileInfo(in.model.path).isFile()) put("--model",in.model.path);
    put("--ctx-size",out.totalContext); put("--parallel",in.slotCount); put("--fit","on"); put("--flash-attn","auto");
    put("--jinja",true); put("--cache-prompt",true); put("--context-shift",false);
    put("--batch-size",512); put("--ubatch-size",128);
    put("--cache-type-k",in.cacheType); put("--cache-type-v",in.cacheType);
    put("--override-tensor",QJsonValue(),false); put("--tensor-split",QJsonValue(),false); put("--main-gpu",QJsonValue(),false);
    put("--kv-unified-per-slot",QJsonValue(),false);
    if (in.sampling) {
        put("--temp",in.sampling==1 ? 0.7 : 0.2); put("--top-p",0.95); put("--top-k",40); put("--min-p",0.0); put("--repeat-penalty",1.0);
        out.warnings << "Выбран пример сэмплирования, а не оптимум для этой модели. Сверьте температуру и фильтры с карточкой модели; параметры запроса IDE могут переопределять их.";
    } else out.notes << "Сэмплирование не изменяется. Для каждой модели используйте рекомендации её автора; универсальной лучшей температуры нет.";
    QStringList ids,ratios,margins; QSet<QString> unique,backends; qint64 budget=0;
    if (!in.cpuOnly && in.devices.isEmpty()) { out.error="Выберите обнаруженные GPU или режим «Только CPU»."; return out; }
    for (const auto &d:in.devices) {
        if (in.cpuOnly) break;
        if (!QRegularExpression("^[A-Za-z][A-Za-z0-9_.:-]*$").match(d.id).hasMatch() || unique.contains(d.id) || d.freeMiB<=in.reserveMiB || (d.totalMiB>0 && d.freeMiB>d.totalMiB)) { out.error="Проверьте ID, свободную память и резерв каждой GPU; ID не должны повторяться."; return out; }
        unique.insert(d.id); ids<<d.id; ratios<<QString::number(d.freeMiB-in.reserveMiB); margins<<QString::number(in.reserveMiB); budget+=d.freeMiB-in.reserveMiB;
        backends.insert(QRegularExpression("[0-9].*$").match(d.id).hasMatch() ? QString(d.id).remove(QRegularExpression("[0-9].*$")) : d.id);
    }
    if (backends.size()>1) { out.error="Выберите устройства одного backend. CUDA и Vulkan могут дважды показывать одну физическую карту; для смешанных GPU используйте подходящий общий backend."; return out; }
    if (in.cpuOnly) { put("--device","none"); put("--gpu-layers","0"); put("--split-mode","none"); put("--fit-target",QString::number(in.reserveMiB)); out.notes<<"Выбран CPU. Скорость зависит от процессора и пропускной способности RAM; fit не ограничивает использование оперативной памяти."; }
    else {
        put("--device",ids.join(',')); put("--gpu-layers","auto"); put("--split-mode",ids.size()>1 ? "layer" : "none"); put("--fit-target",margins.join(','));
        if (ids.size()>1) out.notes<<"Для нескольких GPU выбран режим layer. Пропорции по свободной памяти после резерва: " + ratios.join(',') + ". Они показаны как ориентир; tensor-split оставлен незаданным, чтобы fit рассчитал размещение сам. Скорость зависит также от PCIe/NVLink и различий GPU.";
    }
    auto &m=in.model;
    if (!m.error.isEmpty()) out.warnings<<m.error + " Рекомендации доступны по введённым значениям, но ограничения модели не проверены.";
    if (m.trainingContext<=0) out.warnings<<"Обучающий контекст неизвестен. Проверьте его в карточке модели; не увеличивайте RoPE наугад.";
    if (!m.hasChatTemplate) out.warnings<<"В GGUF не найден строковый шаблон чата. Для чата и инструментов нужен совместимый шаблон; выбирайте Instruct-модель или файл шаблона её автора.";
    if (m.shards>1) out.warnings<<"Модель состоит из нескольких GGUF-частей. Размер выбранного файла не равен полному размеру весов; точное размещение поручите fit.";
    if (in.hasExtras) out.warnings<<"Проектор, LoRA и черновая модель требуют дополнительной памяти. Увеличьте резерв каждой GPU; текущий fit оценивает основную модель.";
    if (in.cacheType!="f16") out.warnings<<"Квантованный KV-кэш экономит память, но может снизить качество. Проверьте поддержку выбранного типа и Flash Attention на ваших GPU.";
    if (in.agent) out.notes<<"Агентское программирование: используйте модель с обученными вызовами инструментов и её родной Jinja-шаблон. Поддержка tools не определяется одной архитектурой GGUF. Подключайте IDE к /v1; список tools передаёт клиент. --agent и встроенные серверные инструменты для этого включать не требуется. Проверьте один реальный tool-call перед длинной задачей.";
    else out.notes<<"Чат: оставьте запас контекста для ответа, учитывайте системные инструкции и историю. Для изображений нужен совместимый проектор и дополнительная память.";
    out.notes<<QString("Контекст одного диалога: %1; слотов: %2; общий --ctx-size: %3. Сдвиг контекста отключён: клиент должен сокращать историю, когда окно заполнено.").arg(in.contextPerSlot).arg(in.slotCount).arg(out.totalContext);
    out.notes<<"Пакеты 512/128 — консервативная отправная точка. При нехватке памяти уменьшайте микропакет, затем контекст/число слотов; при достаточной памяти измеряйте скорость с большим пакетом.";
    out.estimatedKvMiB=estimateKvMiB(m,out.totalContext,in.cacheType);
    if (out.estimatedKvMiB>=0) {
        out.notes<<QString("Грубая оценка KV-кэша по стандартному attention: %1 МиБ. Не включает вычислительные буферы, выравнивание, проектор и веса; окончательный размер смотрите в журнале.").arg(out.estimatedKvMiB,0,'f',0);
        if (!in.cpuOnly && m.shards==1 && m.fileBytes>0 && m.fileBytes/1048576.0+out.estimatedKvMiB>budget) out.warnings<<"Размер файла весов плюс оценка KV превышает доступный бюджет GPU. Возможна частичная выгрузка на CPU; уменьшите контекст/слоты или используйте более компактную модель. Это не точный расчёт VRAM.";
    } else out.notes<<"Оценка KV-кэша по архитектуре недоступна. Для гибридных, MLA и других нестандартных моделей используйте fit и фактический журнал памяти.";
    out.notes<<"Применение снимает прежние ручные GPU layers / tensor overrides, но сохраняет шаблоны, модель и прочие параметры. После применения запустите подбор fit, затем проверьте скорость и качество на своей задаче.";
    return out;
}
bool Advice::apply(Config *config,QString *error) const {
    if (!this->error.isEmpty()) { if(error)*error=this->error; return false; }
    Config next=*config;
    for (const auto &c:changes) { auto p=next.find(c.name); if(!p) { if(error)*error="В каталоге нет параметра "+c.name; return false; } p->enabled=c.enabled; if(c.enabled)p->value=c.value; }
    next.fitContext=totalContext; QStringList args;
    if(!next.arguments(&args,error))return false;
    *config=next; return true;
}
QString Advice::html() const {
    QString text="<style>a{color:#96baff}td{padding:4px}h2,h3{color:#c2d4ff}</style><h2>Рекомендации настройки</h2>";
    if(!error.isEmpty())return text+"<p>"+error.toHtmlEscaped()+"</p>";
    for(const auto &w:warnings)text+="<p><b>Обратите внимание:</b> "+w.toHtmlEscaped()+"</p>";
    for(const auto &n:notes)text+="<p>"+n.toHtmlEscaped()+"</p>";
    text+="<h3>Изменения при применении</h3><table cellpadding='5'>";
    for(const auto &c:changes)text+="<tr><td>"+c.name.toHtmlEscaped()+"</td><td>"+(c.enabled?c.value.toVariant().toString().toHtmlEscaped():QString("Не передавать"))+"</td></tr>";
    return text+"</table><p>Документация: <a href='https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md'>параметры сервера</a> · <a href='https://github.com/ggml-org/llama.cpp/blob/master/docs/function-calling.md'>вызовы инструментов</a> · <a href='https://github.com/ggml-org/llama.cpp/blob/master/tools/fit-params/README.md'>подбор fit</a></p>";
}
