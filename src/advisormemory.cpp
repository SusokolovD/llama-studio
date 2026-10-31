#include "advisor.h"
#include <QSet>
#include <algorithm>

namespace {
QString sizeText(double mib){return mib<0?QString("Неизвестно"):QString("%1 МиБ · %2 ГиБ").arg(mib,0,'f',0).arg(mib/1024,0,'f',2);}
qint64 dimension(const ModelInfo &m,bool key){qint64 explicitValue=key?m.keyLength:m.valueLength;if(explicitValue>0)return explicitValue;if(m.heads>0&&m.embedding>0&&m.embedding%m.heads==0)return m.embedding/m.heads;return 0;}
}
double estimateKvMiB(const ModelInfo &m,qint64 totalTokens,const QString &cacheType){
    if(!m.error.isEmpty()||!QStringList{"llama","qwen2","qwen3","mistral"}.contains(m.architecture)||m.slidingWindow>0||m.layers<=0||m.layers>1000||m.heads<=0||m.kvHeads<=0||m.kvHeads>m.heads||totalTokens<=0||totalTokens>2147483647)return -1;
    const auto dk=dimension(m,true),dv=dimension(m,false);if(dk<=0||dv<=0||dk>65536||dv>65536)return -1;
    double bytes=cacheType=="f16"?2.0:cacheType=="q8_0"?34.0/32:cacheType=="q4_0"?18.0/32:-1;
    if(bytes<0)return -1;
    return double(m.layers)*m.kvHeads*(dk+dv)*totalTokens*bytes/1048576;
}
QString memoryHtml(const AdviceInput &in){
    QString html="<style>a{color:#96baff}h2,h3{color:#c2d4ff}td,th{padding:5px}th{background:#293246}</style><h2>Сколько памяти нужно контексту</h2>";
    const auto &m=in.model;const qint64 total=qint64(in.contextPerSlot)*in.slotCount;
    const auto kv=estimateKvMiB(m,total,in.cacheType);
    html+=QString("<p>Сейчас: <b>%1 токенов на диалог × %2 слотов = %3 токенов KV-окна</b>, тип %4.</p>").arg(in.contextPerSlot).arg(in.slotCount).arg(total).arg(in.cacheType.toHtmlEscaped());
    html+="<p><b>Оценка KV-кэша: "+sizeText(kv)+".</b> Это ёмкость для заданного окна, а не показание текущей занятой VRAM. Способ и момент выделения памяти зависят от backend.</p>";
    if(in.cpuOnly)html+="<p>Выбран CPU: кэш и веса размещаются в оперативной памяти. Контролируйте свободную RAM; запас VRAM для fit здесь не задаёт лимит RAM.</p>";
    if(kv>=0){
        html+=QString("<p>В GGUF: слоёв L=%1, KV-голов H=%2, размер ключа Dk=%3, значения Dv=%4. Для этого формата примерно %5 КиБ на один токен общего окна.</p>").arg(m.layers).arg(m.kvHeads).arg(dimension(m,true)).arg(dimension(m,false)).arg(kv*1024/total,0,'f',2);
        html+="<p>Формула: L × H × (Dk + Dv) × все токены окна × байт на элемент. Для f16 — 2 байта; для q8_0 и q4_0 учтены блоковые коэффициенты 34/32 и 18/32. Выравнивание и служебные буферы не включены.</p>";
        html+="<h3>Контекст и тип кэша: сравнение</h3><table border='1' cellspacing='0'><tr><th>Токенов на диалог</th><th>Всего × слоты</th><th>f16, МиБ</th><th>q8_0, МиБ</th><th>q4_0, МиБ</th></tr>";
        QSet<int> unique{4096,8192,16384,32768,65536,in.contextPerSlot};auto contexts=unique.values();std::sort(contexts.begin(),contexts.end());
        for(int context:contexts){bool unsupported=m.trainingContext>0&&context>m.trainingContext;auto tokenCount=qint64(context)*in.slotCount;
            html+=QString("<tr><td>%1%2%3</td><td>%4</td>").arg(context).arg(context==in.contextPerSlot?" ← выбран":"").arg(unsupported?" *":"").arg(tokenCount);
            for(auto type:{"f16","q8_0","q4_0"}){auto amount=estimateKvMiB(m,tokenCount,type);html+="<td>"+(amount<0?QString("—"):QString::number(amount,'f',0))+"</td>";}html+="</tr>";
        }
        html+="</table><p>* Выше обучающего контекста модели: строка показывает только математический рост памяти и не рекомендует такое окно. Форматы кэша должны поддерживаться моделью и backend.</p>";
    }else{
        html+="<p>Численный прогноз для выбранной модели недоступен: прочитайте GGUF. Модели MLA, гибридные, со скользящим вниманием или неизвестной геометрией требуют расчёта самим llama.cpp. Размер файла и имя модели не определяют размер KV-кэша.</p>";
        html+="<h3>Учебный пример, не ваша модель</h3><p>32 слоя, 8 KV-голов, Dk=Dv=128, f16: 4096 токенов ≈ 512 МиБ, 8192 ≈ 1 ГиБ, 16384 ≈ 2 ГиБ. Два независимых слота по 8192 требуют примерно 2 ГиБ только на KV. У другой архитектуры расход может сильно отличаться.</p>";
    }
    html+="<h3>Веса, контекст и прочие расходы</h3>";
    if(m.fileBytes>0){html+="<p>Размер выбранного GGUF: <b>"+sizeText(m.fileBytes/1048576.0)+"</b>"+(m.shards>1?QString("; это одна из %1 частей, не весь набор весов.").arg(m.shards):QString(". Это размер файла, а не точный объём весов в VRAM."))+"</p>";
        if(m.shards==1&&kv>=0)html+="<p>Файл весов + оценка KV: <b>"+sizeText(m.fileBytes/1048576.0+kv)+"</b>. Ориентир для попытки полной выгрузки; ещё нужны вычислительные буферы. При частичном offload часть весов/кэша находится в RAM.</p>";
    }
    html+="<p>Сверх весов и KV нужны буферы обработки запроса, графы вычислений и память backend. Проектор, draft-модель и LoRA добавляют расходы. Квантование весов GGUF и квантование KV — разные настройки.</p>";
    html+="<h3>Резерв на каждой видеокарте</h3><p>--fit-target задаёт целевой запас для подбора; он не выделяет этот объём заранее и не блокирует его от других программ. Он нужен для колебаний использования VRAM: рабочий стол, браузер, другие GPU-приложения и дополнительные компоненты модели.</p>";
    if(!in.cpuOnly&&!in.devices.isEmpty()){
        html+="<table border='1' cellspacing='0'><tr><th>GPU</th><th>Свободно, МиБ</th><th>Резерв, МиБ</th><th>Бюджет fit, МиБ</th></tr>";double sum=0;
        for(const auto &d:in.devices){auto budget=qMax<qint64>(0,d.freeMiB-in.reserveMiB);sum+=budget;html+=QString("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td></tr>").arg(d.id.toHtmlEscaped()).arg(d.freeMiB).arg(in.reserveMiB).arg(budget);}
        html+="</table><p>Сумма бюджетов: <b>"+sizeText(sum)+"</b>. Она не превращает карты в одну общую VRAM: распределение слоёв, KV и буферов должно помещаться на каждой карте. Точные доли рассчитывайте fit.</p>";
    }
    html+="<p>Пример: свободно 10 240 МиБ, резерв 1024 МиБ → бюджет fit 9216 МиБ. Можно начать с 1024 МиБ на GPU и увеличить запас для карты с монитором или дополнительными моделями; это отправная точка, не гарантия отсутствия OOM. Резерв учитывается сверх уже занятой памяти из столбца «Свободно».</p>";
    html+="<h3>Если не помещается</h3><ol><li>Остановите сервер и обновите список устройств.</li><li>Уменьшите число слотов или контекст. Для стандартного KV удвоение окна примерно удваивает KV.</li><li>Уменьшите физический пакет, если ошибка возникает при обработке длинного запроса.</li><li>Проверьте поддерживаемый q8_0 для KV либо более компактные веса. Проверьте качество после сжатия.</li><li>Запустите fit; сравните расчёт с фактическим журналом памяти и своими запросами.</li></ol><p><a href='https://github.com/ggml-org/llama.cpp/blob/master/src/llama-kv-cache.cpp'>Реализация KV-кэша</a> · <a href='https://github.com/ggml-org/llama.cpp/blob/master/tools/fit-params/README.md'>Подбор памяти fit</a></p>";
    return html;
}
