#include "config.h"
#include "advisor.h"
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
    void russianCatalog() {
        auto c=catalog();
        for(const auto &p:c.parameters){QVERIFY(QRegularExpression("[А-Яа-я]").match(p.definition.value("label").toString()).hasMatch());QVERIFY(QRegularExpression("[А-Яа-я]").match(p.definition.value("description").toString()).hasMatch());}
    }
    void ggufMetadataAndInvalidInput() {
        QTemporaryDir dir;QFile f(dir.filePath("tiny.gguf"));QVERIFY(f.open(QIODevice::WriteOnly));QDataStream s(&f);s.setByteOrder(QDataStream::LittleEndian);
        auto string=[&](const QByteArray &v){s<<quint64(v.size());s.writeRawData(v.constData(),v.size());};
        s.writeRawData("GGUF",4);s<<quint32(3)<<quint64(0)<<quint64(9);
        for(const auto &pair:QVector<QPair<QByteArray,QByteArray>>{{"general.architecture","llama"},{"general.name","test model"},{"tokenizer.chat_template","{{ messages }}"}}){string(pair.first);s<<quint32(8);string(pair.second);}
        string("tokenizer.ggml.tokens");s<<quint32(9)<<quint32(8)<<quint64(2);string("token A");string("token B");
        for(const auto &pair:QVector<QPair<QByteArray,quint32>>{{"llama.context_length",32768},{"llama.block_count",32},{"llama.embedding_length",4096},{"llama.attention.head_count",32},{"llama.attention.head_count_kv",8}}){string(pair.first);s<<quint32(4)<<pair.second;}
        f.close();auto info=readModelInfo(f.fileName());QVERIFY2(info.error.isEmpty(),qPrintable(info.error));QCOMPARE(info.trainingContext,qint64(32768));QCOMPARE(info.layers,qint64(32));QVERIFY(info.hasChatTemplate);
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
