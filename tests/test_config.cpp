#include "config.h"
#include "advisor.h"
#include "hardwaremonitor.h"
#include "modelperformance.h"
#include "modelcache.h"
#include "gpupower.h"
#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
class ConfigTest : public QObject {
    Q_OBJECT
    Config catalog() {
        QFile f(CATALOG_PATH); f.open(QIODevice::ReadOnly); Config c; QString e;
        if (!c.load(f.readAll(), &e)) qFatal("%s", qPrintable(e)); return c;
    }
private slots:
    void deniedPowerWriteKeepsOriginalAndAllowsReset(){
        QTemporaryDir dir;const auto exe=dir.filePath(QFileInfo(FIXTURE_PATH).fileName());QVERIFY(QFile::copy(FIXTURE_PATH,exe));QFile state(dir.filePath("power-state.json"));QVERIFY(state.open(QIODevice::WriteOnly));state.write("{\"GPU-aa\":180,\"GPU-bb\":300}");state.close();QFile denied(dir.filePath("power-denied"));QVERIFY(denied.open(QIODevice::WriteOnly));denied.close();GpuPowerService service(nullptr,exe);QSignalSpy done(&service,&GpuPowerService::completed);service.refresh();QTRY_VERIFY(!service.busy());service.apply("GPU-aa",150);QTRY_VERIFY(!service.busy());QVERIFY(!done.last()[0].toBool());QSignalSpy reset(&service,&GpuPowerService::resetFinished);service.restoreAllDefaults();QTRY_VERIFY(!service.busy());QVERIFY(reset.last()[0].toBool());QCOMPARE(service.devices()[0].limit,180.0);QVERIFY(!service.needsReset());QFile commands(dir.filePath("power-commands.jsonl"));QVERIFY(commands.open(QIODevice::ReadOnly));QCOMPARE(commands.readAll().count("\"-pl\""),1);
    }
    void modelRolesAndCache(){
        ModelInfo main;main.path="Qwen-VL.gguf";main.architecture="qwen2vl";QCOMPARE(modelRole(main),QString("--model"));auto graphics=main;graphics.path="mmproj-qwen.gguf";QCOMPARE(modelRole(graphics),QString("--mmproj"));auto draft=main;draft.path="model-draft.gguf";QCOMPARE(modelRole(draft),QString("--spec-draft-model"));draft.userRole="--model";QCOMPARE(modelRole(draft),QString("--model"));
        QTemporaryDir dir;const auto cachePath=dir.filePath("cache.json"),modelPath=dir.filePath("model.gguf");QFile file(modelPath);QVERIFY(file.open(QIODevice::WriteOnly));QDataStream stream(&file);stream.setByteOrder(QDataStream::LittleEndian);stream.writeRawData("GGUF",4);stream<<quint32(3)<<quint64(0)<<quint64(0);file.close();ModelCache cache(cachePath);const auto info=cache.get(modelPath);cache.setDirectory(dir.path(),true,{info});cache.setRole(modelPath,"--spec-draft-model");QString error;QVERIFY(cache.save(&error));ModelCache reload(cachePath);QCOMPARE(modelRole(reload.get(modelPath)),QString("--spec-draft-model"));QCOMPARE(modelRole(reload.directory(dir.path(),true)[0]),QString("--spec-draft-model"));reload.get(modelPath,true);QVERIFY(reload.save(&error));ModelCache afterRefresh(cachePath);QCOMPARE(modelRole(afterRefresh.allModels()[0]),QString("--spec-draft-model"));afterRefresh.setRole(modelPath,{});QVERIFY(afterRefresh.save(&error));QCOMPARE(modelRole(ModelCache(cachePath).get(modelPath)),QString("--model"));
    }
    void gpuPowerServiceValidationAndReadback(){
        auto parsed=parseGpuPowerInfo("GPU-aa, \"GPU, A\", 150, 200, 200, 100, 250, 60, 90\nGPU-cc, Other, 20, N/A, N/A, N/A, N/A, 40, 0");QCOMPARE(parsed.size(),2);QCOMPARE(parsed[0].name,QString("GPU, A"));QVERIFY(parsed[0].adjustable());QVERIFY(!parsed[1].adjustable());
        QTemporaryDir dir;const auto exe=dir.filePath(QFileInfo(FIXTURE_PATH).fileName());QVERIFY(QFile::copy(FIXTURE_PATH,exe));GpuPowerService service(nullptr,exe);QSignalSpy results(&service,&GpuPowerService::completed);service.refresh();QTRY_VERIFY(!service.busy());QCOMPARE(service.devices().size(),3);QVERIFY(results.last()[0].toBool());
        service.apply("GPU-aa",180);QTRY_VERIFY(!service.busy());QVERIFY2(results.last()[0].toBool(),qPrintable(results.last()[1].toString()));QCOMPARE(service.devices()[0].limit,180.0);QCOMPARE(service.devices()[1].limit,300.0);
        QFile log(dir.filePath("power-commands.jsonl"));QVERIFY(log.open(QIODevice::ReadOnly));const auto commands=log.readAll();log.close();QVERIFY(commands.contains("GPU-aa"));QVERIFY(commands.contains("180.0"));auto count=commands.count('\n');
        service.apply("GPU-aa",99);QVERIFY(!results.last()[0].toBool());service.apply("GPU-cc",100);QVERIFY(!results.last()[0].toBool());QVERIFY(log.open(QIODevice::ReadOnly));QCOMPARE(log.readAll().count('\n'),count);log.close();
        service.restoreDefault("GPU-aa");QTRY_VERIFY(!service.busy());QVERIFY(results.last()[0].toBool());QCOMPARE(service.devices()[0].limit,200.0);
        QFile denied(dir.filePath("power-denied"));QVERIFY(denied.open(QIODevice::WriteOnly));denied.close();service.apply("GPU-bb",200);QTRY_VERIFY(!service.busy());QVERIFY(!results.last()[0].toBool());QVERIFY(results.last()[1].toString().contains("Insufficient Permissions"));QCOMPARE(service.devices()[1].limit,300.0);QVERIFY(denied.remove());
        QFile ignore(dir.filePath("power-ignore"));QVERIFY(ignore.open(QIODevice::WriteOnly));ignore.close();service.apply("GPU-aa",175);QTRY_VERIFY(!service.busy());QVERIFY(!results.last()[0].toBool());QVERIFY(results.last()[1].toString().contains(QString::fromUtf8("не подтверждено")));QCOMPARE(service.devices()[0].limit,200.0);
        QVERIFY(ignore.remove());QFile hang(dir.filePath("power-hang"));QVERIFY(hang.open(QIODevice::WriteOnly));hang.close();service.apply("GPU-aa",175);QTRY_VERIFY_WITH_TIMEOUT(!service.busy(),8000);QVERIFY(!results.last()[0].toBool());QVERIFY(results.last()[1].toString().contains(QString::fromUtf8("время ожидания")));
        QVERIFY(hang.remove());QSignalSpy applied(&service,&GpuPowerService::profileFinished);service.applyProfile(QJsonObject{{"GPU-aa",110.0},{"GPU-bb",210.0}});QTRY_VERIFY(!service.busy());QVERIFY(applied.last()[0].toBool());QCOMPARE(service.devices()[0].limit,110.0);QCOMPARE(service.devices()[1].limit,210.0);QSignalSpy reset(&service,&GpuPowerService::resetFinished);QVERIFY(denied.open(QIODevice::WriteOnly));denied.close();service.restoreAllDefaults();QTRY_VERIFY(!service.busy());QVERIFY(!reset.last()[0].toBool());QVERIFY(service.needsReset());QVERIFY(denied.remove());service.restoreAllDefaults();QTRY_VERIFY(!service.busy());QVERIFY(reset.last()[0].toBool());QCOMPARE(service.devices()[0].limit,200.0);QCOMPARE(service.devices()[1].limit,300.0);QCOMPARE(service.profileLimits.value("GPU-aa").toDouble(),110.0);QVERIFY(!service.needsReset());
    }
    void modelJsonCache(){
        QTemporaryDir dir;const auto path=dir.filePath("model.gguf"),cachePath=dir.filePath("models-cache.json");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QDataStream s(&file);s.setByteOrder(QDataStream::LittleEndian);auto string=[&](const QByteArray &v){s<<quint64(v.size());s.writeRawData(v.constData(),v.size());};s.writeRawData("GGUF",4);s<<quint32(3)<<quint64(0)<<quint64(3);string("general.name");s<<quint32(8);string("Cached model");string("general.architecture");s<<quint32(8);string("llama");string("general.file_type");s<<quint32(4)<<quint32(15);file.close();
        bool hit=true;ModelCache initial(cachePath);auto info=initial.get(path,false,&hit);QVERIFY(!hit);QVERIFY(info.error.isEmpty());initial.setDirectory(dir.path(),true,{info});QString error;QVERIFY2(initial.save(&error),qPrintable(error));QFile json(cachePath);QVERIFY(json.open(QIODevice::ReadOnly));QVERIFY(QJsonDocument::fromJson(json.readAll()).object().value("models").toObject().size()==1);json.close();
        const auto stamp=QFileInfo(path).lastModified();QVERIFY(file.open(QIODevice::ReadWrite));QCOMPARE(file.write("BAD!",4),qint64(4));QVERIFY(file.setFileTime(stamp,QFileDevice::FileModificationTime));file.close();
        ModelCache reopened(cachePath);auto cached=reopened.get(path,false,&hit);QVERIFY(hit);QCOMPARE(cached.name,QString("Cached model"));QCOMPARE(cached.fileType,qint64(15));QVERIFY(cached.error.isEmpty());bool found=false;QCOMPARE(reopened.directory(dir.path(),true,&found).size(),1);QVERIFY(found);QVERIFY(!reopened.get(path,true,&hit).error.isEmpty());QVERIFY(!hit);
        QVERIFY(file.open(QIODevice::ReadWrite));QCOMPARE(file.write("GGUF",4),qint64(4));QVERIFY(file.seek(file.size()));file.write("x");file.close();auto changed=reopened.get(path,false,&hit);QVERIFY(!hit);QVERIFY(changed.error.isEmpty());QVERIFY(reopened.save(&error));
        const auto other=dir.filePath("other.gguf");QVERIFY(QFile::copy(path,other));ModelCache a(cachePath),b(cachePath);a.get(path,true);b.get(other);QVERIFY(a.save(&error));QVERIFY(b.save(&error));ModelCache merged(cachePath);merged.get(path,false,&hit);QVERIFY(hit);merged.get(other,false,&hit);QVERIFY(hit);
        QVERIFY(json.open(QIODevice::WriteOnly|QIODevice::Truncate));json.write("broken JSON");json.close();ModelCache broken(cachePath);QVERIFY(broken.get(path,false,&hit).error.isEmpty());QVERIFY(!hit);QVERIFY(broken.save(&error));
    }
    void modelTimingReportsAndLifecycle(){
        const QString prefix="slot print_timings: id 0 | task 7 | ";auto report=parseModelTiming(prefix+"prompt eval time = 100.00 ms / 100 tokens (1.00 ms per token, 1000.00 tokens per second)");QCOMPARE(int(report.kind),int(TimingKind::Prompt));QCOMPARE(report.tokensPerSecond,1000.0);QCOMPARE(report.milliseconds,100.0);QVERIFY(report.task.contains("7"));
        report=parseModelTiming("slot update_slots: id 0 | task 7 | n_gen = 123, tg = 49.00 t/s, tg_3s = 50.00 t/s");QCOMPARE(int(report.kind),int(TimingKind::Generation));QCOMPARE(report.tokensPerSecond,50.0);QVERIFY(report.live);
        report=parseModelTiming("prompt processing, n_tokens = 1000, progress = 0.50, t = 3.00 s / 333.33 tokens per second");QCOMPARE(int(report.kind),int(TimingKind::Prompt));QCOMPARE(report.tokensPerSecond,333.33);QVERIFY(report.live);QCOMPARE(int(parseModelTiming("prompt eval time = 0 ms / 0 tokens (nan ms per token, inf tokens per second)").kind),int(TimingKind::None));
        ModelPerformance stats;stats.start("A.gguf");stats.readLine(prefix+"prompt eval time = 100.00 ms / 100 tokens (1.00 ms per token, 1000.00 tokens per second)");stats.readLine("slot print_timings: id 1 | task 8 | eval time = 100.00 ms / 2 tokens (50.00 ms per token, 20.00 tokens per second)");QCOMPARE(stats.snapshot().promptSpeed,-1.0);QCOMPARE(stats.snapshot().generationSpeed,20.0);
        stats.readLine(prefix+"eval time = 200.00 ms / 10 tokens (20.00 ms per token, 50.00 tokens per second)");QCOMPARE(stats.snapshot().promptSpeed,1000.0);QCOMPARE(stats.snapshot().generationSpeed,50.0);stats.readLine(prefix+"total time = 300.00 ms / 110 tokens");QCOMPARE(stats.snapshot().requestMs,300.0);
        QTest::qWait(20);stats.stop();const auto stopped=stats.snapshot().uptimeMs;QVERIFY(stopped>0);QTest::qWait(20);QCOMPARE(stats.snapshot().uptimeMs,stopped);QVERIFY(!stats.snapshot().running);stats.start("B.gguf");QCOMPARE(stats.snapshot().model,QString("B.gguf"));QCOMPARE(stats.snapshot().promptSpeed,-1.0);QCOMPARE(stats.snapshot().generationSpeed,-1.0);QVERIFY(stats.snapshot().task.isEmpty());
    }
    void monitoringReadings(){
        auto rows=parseNvidiaReadings("GPU-a, \"GPU, one\", 52, 75, 4096, 8192, 120.5, 0\nGPU-b, GPU two, N/A, [Not Supported], 2000, 12000, N/A, N/A\ninvalid\n");
        QCOMPARE(rows.size(),2);QCOMPARE(rows[0].name,QString("GPU, one"));QCOMPARE(rows[0].temperature,52.0);QCOMPARE(rows[0].fan,0.0);QCOMPARE(rows[1].temperature,-1.0);QCOMPARE(rows[1].load,-1.0);
        QVERIFY(parseNvidiaReadings("GPU-a, broken, nan, 101, -2, 100, inf, 200")[0].load<0);QCOMPARE(modelQuantization(15),QString("Q4_K_M"));QVERIFY(modelQuantization(999).contains("999"));QCOMPARE(modelQuantization(-1),QString::fromUtf8("Не указано"));
    }
    void zeroFitContextRoundTrip(){
        auto c=catalog();c.fitContext=0;Config restored;QString error;QVERIFY2(restored.load(c.save(),&error),qPrintable(error));QCOMPARE(restored.fitContext,0);
        c.fitContext=-1;QVERIFY(!restored.load(c.save(),&error));QCOMPARE(restored.fitContext,0);
        QVERIFY(restored.applyFit("-c 0 -ngl 0",&error));QCOMPARE(restored.find("--ctx-size")->value.toInt(),0);
    }
    void kvMemoryScaling(){
        ModelInfo m;m.architecture="llama";m.layers=32;m.heads=32;m.kvHeads=8;m.embedding=4096;
        QCOMPARE(estimateKvMiB(m,8192,"f16"),1024.0);QCOMPARE(estimateKvMiB(m,16384,"f16"),2048.0);QCOMPARE(estimateKvMiB(m,8192,"q8_0"),544.0);QCOMPARE(estimateKvMiB(m,8192,"q4_0"),288.0);
        m.keyLength=64;m.valueLength=128;QCOMPARE(estimateKvMiB(m,8192,"f16"),768.0);
        m.slidingWindow=4096;QCOMPARE(estimateKvMiB(m,8192,"f16"),-1.0);m.slidingWindow=0;m.architecture="unknown";QCOMPARE(estimateKvMiB(m,8192,"f16"),-1.0);
        AdviceInput input;input.model=m;input.devices={{"CUDA0","A",10240,12288}};auto html=memoryHtml(input);QVERIFY(html.contains("9216"));QVERIFY(html.contains(QString::fromUtf8("Учебный пример")));
    }
    void russianCatalog() {
        auto c=catalog();
        for(const auto &p:c.parameters){QVERIFY(QRegularExpression("[А-Яа-я]").match(p.definition.value("label").toString()).hasMatch());QVERIFY(QRegularExpression("[А-Яа-я]").match(p.definition.value("description").toString()).hasMatch());}
    }
    void ggufMetadataAndInvalidInput() {
        QTemporaryDir dir;QFile f(dir.filePath("tiny.gguf"));QVERIFY(f.open(QIODevice::WriteOnly));QDataStream s(&f);s.setByteOrder(QDataStream::LittleEndian);
        auto string=[&](const QByteArray &v){s<<quint64(v.size());s.writeRawData(v.constData(),v.size());};
        s.writeRawData("GGUF",4);s<<quint32(3)<<quint64(0)<<quint64(14);
        for(const auto &pair:QVector<QPair<QByteArray,QByteArray>>{{"general.architecture","llama"},{"general.name","test model"},{"tokenizer.chat_template","{{ messages }}"}}){string(pair.first);s<<quint32(8);string(pair.second);}
        string("tokenizer.ggml.tokens");s<<quint32(9)<<quint32(8)<<quint64(2);string("token A");string("token B");
        for(const auto &pair:QVector<QPair<QByteArray,quint32>>{{"llama.context_length",32768},{"llama.block_count",32},{"llama.embedding_length",4096},{"llama.attention.head_count",32},{"llama.attention.head_count_kv",8}}){string(pair.first);s<<quint32(4)<<pair.second;}
        for(const auto &pair:QVector<QPair<QByteArray,quint32>>{{"llama.attention.key_length",64},{"llama.attention.value_length",128},{"llama.attention.sliding_window",0}}){string(pair.first);s<<quint32(4)<<pair.second;}
        string("general.file_type");s<<quint32(4)<<quint32(15);string("general.size_label");s<<quint32(8);string("7B");
        f.close();auto info=readModelInfo(f.fileName());QVERIFY2(info.error.isEmpty(),qPrintable(info.error));QCOMPARE(info.trainingContext,qint64(32768));QCOMPARE(info.layers,qint64(32));QVERIFY(info.hasChatTemplate);QCOMPARE(info.keyLength,qint64(64));QCOMPARE(info.valueLength,qint64(128));QCOMPARE(info.fileType,qint64(15));QCOMPARE(info.sizeLabel,QString("7B"));
        AdviceInput input;input.model=info;input.agent=true;input.contextPerSlot=16384;input.slotCount=2;input.devices={{"CUDA0","A",24000,24000},{"CUDA1","B",12000,12000}};
        auto advice=makeAdvice(input);QVERIFY(advice.error.isEmpty());QCOMPARE(advice.totalContext,32768);QVERIFY(advice.estimatedKvMiB>0);
        auto c=catalog();QString error;QVERIFY2(advice.apply(&c,&error),qPrintable(error));QCOMPARE(c.find("--device")->value.toString(),QString("CUDA0,CUDA1"));QCOMPARE(c.find("--split-mode")->value.toString(),QString("layer"));QVERIFY(!c.find("--tensor-split")->enabled);QCOMPARE(c.fitContext,32768);QVERIFY(!c.find("--agent")->enabled);QVERIFY(!c.find("--temp")->enabled);
        input.contextPerSlot=65536;QVERIFY(!makeAdvice(input).error.isEmpty());input.contextPerSlot=8192;input.devices[1].id="Vulkan0";QVERIFY(!makeAdvice(input).error.isEmpty());
        input.cpuOnly=true;auto cpu=makeAdvice(input);QVERIFY(cpu.apply(&c,&error));QCOMPARE(c.find("--device")->value.toString(),QString("none"));
        QFile broken(dir.filePath("broken.gguf"));QVERIFY(broken.open(QIODevice::WriteOnly));broken.write("GGUF");broken.close();QVERIFY(!readModelInfo(broken.fileName()).error.isEmpty());
    }
    void deviceParsing() {
        auto devices=parseDevices("Available devices:\n  CUDA0: Card A (24576 MiB, 22000 MiB free)\n  CUDA1: Card B (12288 MiB, 10000 MiB free)\nlog noise\n");
        QCOMPARE(devices.size(),2);QCOMPARE(devices[1].id,QString("CUDA1"));QCOMPARE(devices[1].freeMiB,qint64(10000));
        AdviceInput input;input.devices=devices;input.devices[1].freeMiB=512;QVERIFY(!makeAdvice(input).error.isEmpty());
        input.cpuOnly=true;auto advice=makeAdvice(input);auto c=catalog();c.find("--temp")->value=1.1;c.find("--temp")->enabled=true;QString error;QVERIFY(advice.apply(&c,&error));QCOMPARE(c.find("--temp")->value.toDouble(),1.1);
    }
    void completeCatalog() {
        auto c = catalog(); QVERIFY(c.parameters.size() > 250);
        QVERIFY(c.find("--model")); QVERIFY(c.find("--spec-draft-model")); QVERIFY(c.find("--control-vector-layer-range"));
        QStringList args; QString error; QVERIFY(c.arguments(&args, &error)); QVERIFY(args.isEmpty());
        for (int i = 1; i < c.parameters.size(); ++i) QVERIFY(c.parameters[i - 1].priority <= c.parameters[i].priority);
    }
    void profileRoundTrip() {
        auto c = catalog(); c.directory = "C:/Модели/llama cpp"; c.fitContext = 32768;
        auto m = c.find("--model"); m->value = "C:/Модели/тест model.gguf"; m->enabled = true;
        c.find("--ctx-size")->value = 32768; c.find("--ctx-size")->enabled = true;
        Config loaded; QString error; QVERIFY2(loaded.load(c.save(), &error), qPrintable(error)); QCOMPARE(loaded.directory, c.directory); QCOMPARE(loaded.fitContext, 32768);
        QStringList args; QVERIFY(loaded.arguments(&args, &error)); QCOMPARE(args.value(1), m->value.toString()); QCOMPARE(args.value(3), QString("32768"));
    }
    void booleansAndArgumentBoundaries() {
        auto c = catalog(); auto p = c.find("--warmup"); p->enabled = true; p->value = false;
        auto path = c.find("--model"); path->enabled = true; path->value = "C:\\models with spaces\\m.gguf";
        QStringList args; QString error; QVERIFY(c.arguments(&args, &error)); QVERIFY(args.contains("--no-warmup")); QVERIFY(args.contains(path->value.toString()));
        auto range = c.find("--control-vector-layer-range"); range->enabled = true; range->value = "4 12";
        QVERIFY(c.arguments(&args, &error)); int i = args.indexOf(range->name); QCOMPARE(args[i + 1], QString("4")); QCOMPARE(args[i + 2], QString("12"));
        range->value = "4"; QVERIFY(!c.arguments(&args, &error));
    }
    void fitRegexAndTransactionalFailure() {
        auto c = catalog(); QString error;
        QVERIFY(c.applyFit("-c 8192 -ngl 35 -ts 1,2 -ot \"blk\\.14\\.ffn_(up|down)=CPU\"\n", &error));
        QCOMPARE(c.find("--override-tensor")->value.toString(), QString("blk\\.14\\.ffn_(up|down)=CPU"));
        QCOMPARE(c.find("--ctx-size")->value.toInt(), 8192);
        auto original = c.save(); QVERIFY(!c.applyFit("-c 16384 -unknown 2", &error)); QCOMPARE(c.save(), original);
        QVERIFY(!c.applyFit("-c 8192 -ngl", &error));
        QVERIFY(c.applyFit("-c 4096 -ngl 20", &error)); QVERIFY(!c.find("--override-tensor")->enabled); QVERIFY(!c.find("--tensor-split")->enabled);
    }
    void rejectsInvalidProfiles() {
        auto c = catalog(); auto original = c.save(); QString error;
        auto root = QJsonDocument::fromJson(original).object(); auto params = root["parameters"].toArray(); auto p = params[0].toObject(); p["type"] = 99; params[0] = p; root["parameters"] = params;
        QVERIFY(!c.load(QJsonDocument(root).toJson(), &error)); QCOMPARE(c.save(), original);
        p["type"] = 1; p["value"] = "wrong"; params[0] = p; root["parameters"] = params;
        QVERIFY(!c.load(QJsonDocument(root).toJson(), &error));
    }
    void redactsDisplay() {
        auto display = Config::displayCommand("llama-server", {"--api-key", "secret", "--hf-token", "private"});
        QVERIFY(!display.contains("secret")); QVERIFY(!display.contains("private"));
    }
};
QTEST_GUILESS_MAIN(ConfigTest)
#include "test_config.moc"
