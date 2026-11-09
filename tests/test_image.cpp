#include "imageengine.h"
#include "imagecatalog.h"
#include "image_test_assets.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QTcpServer>
#include <QJsonDocument>
class ImageTest:public QObject {
    Q_OBJECT
    void write(const QString &path,const QByteArray &bytes){QDir().mkpath(QFileInfo(path).absolutePath());QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(bytes),qint64(bytes.size()));}
    ImageSettings fixture(const QString &directory){auto s=ImageSettings::fromJson({});s.rootDirectory=directory;s.executablePath=FIXTURE_PATH;QDir().mkpath(s.resolve(s.modelDirectory));for(auto value:s.profiles){auto p=value.toObject();for(auto key:{"model","llm","vae"})if(!p.value(key).toString().isEmpty())write(s.resolve(p.value(key).toString()),"fixture weights");}QTcpServer probe;if(probe.listen(QHostAddress::LocalHost,0))s.port=probe.serverPort();return s;}
private slots:
    void catalogRecognizesRenamedComponents(){
        QTemporaryDir dir;const auto model=dir.filePath("random/a.gguf"),encoder=dir.filePath("unknown/b.gguf"),vae=dir.filePath("scattered/c.safetensors");
        QVERIFY(imageGgufFixture(model,{{"general.architecture","flux"}},{{"content_mode_blocks.image.0.q_proj.weight",{3072,3072}},{"txt_in.weight",{20480,3072}}}));
        QVERIFY(imageGgufFixture(encoder,{{"general.architecture","qwen3vl"},{"qwen3vl.embedding_length",2560}},{{"token_embd.weight",{2560,151936}}}));
        QVERIFY(imageSafeFixture(vae,{{"decoder.conv_in.weight",{512,32,3,3}},{"bn.running_mean",{128}}}));
        QCOMPARE(inspectImageAsset(model).family,QString("flux3"));QCOMPARE(inspectImageAsset(encoder).family,QString("qwen3vl4b"));QCOMPARE(inspectImageAsset(vae).family,QString("flux2vae"));
        const auto qwen=dir.filePath("renamed-q.gguf");QVERIFY(imageGgufFixture(qwen,{{"general.architecture","qwen_image21"}},{{"img_in.weight",{64,4096}}}));QCOMPARE(inspectImageAsset(qwen).family,QString("qwen21"));
        QVERIFY(imageSafeFixture(dir.filePath("q-vae.safetensors"),{{"decoder.conv1.weight",{1152,64,1,3,3}},{"encoder.conv1.weight",{96,4,1,3,3}}}));QCOMPARE(inspectImageAsset(dir.filePath("q-vae.safetensors")).family,QString("qwen21vae"));
        QVERIFY(imageGgufFixture(dir.filePath("e8.gguf"),{{"general.architecture","qwen3vl"},{"qwen3vl.embedding_length",4096}},{{"token_embd.weight",{4096,151936}}}));QCOMPARE(inspectImageAsset(dir.filePath("e8.gguf")).family,QString("qwen3vl8b"));
        QVERIFY(imageGgufFixture(dir.filePath("full.gguf"),{},{{"model.diffusion_model.input_blocks.1.1.transformer_blocks.0.attn2.to_k.weight",{1024,320}},{"cond_stage_model.transformer.text_model.embeddings.token_embedding.weight",{1024,49408}},{"first_stage_model.decoder.conv_in.weight",{3,3,4,512}}}));QCOMPARE(inspectImageAsset(dir.filePath("full.gguf")).family,QString("sd21"));
        const auto fake=dir.filePath("qwen-image-2.1-fake.gguf");QVERIFY(imageGgufFixture(fake,{},{{"unrelated.weight",{8,8}}}));QVERIFY(!inspectImageAsset(fake).trusted);QVERIFY(inspectImageAsset(fake).family.isEmpty());
        QVector<ImageAsset> assets{inspectImageAsset(encoder),inspectImageAsset(vae)};QCOMPARE(imageCandidates(assets,"qwen3vl4b").size(),1);QCOMPARE(imageCandidates(assets,"qwen3vl8b").size(),0);QCOMPARE(imageDependencies("qwen21").join('|'),QString("qwen3vl8b|qwen21vae"));
    }
    void catalogCachePartialAndInvalidHeaders(){
        QTemporaryDir dir;const auto file=dir.filePath("nested/m.gguf");QVERIFY(imageGgufFixture(file,{{"general.architecture","qwen_image21"}},{{"img_in.weight",{64,4096}}}));write(dir.filePath("other/a.gguf.part"),"partial");write(dir.filePath("broken.safetensors"),"bad header");ImageCatalog cache(dir.filePath("catalog.json"));QString error;auto result=cache.scan(dir.path(),[]{return false;},[](int,const QString &){},&error);QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(result.size(),3);int invalid=0;for(const auto &asset:result)invalid+=!asset.error.isEmpty();QCOMPARE(invalid,2);QVERIFY(QFile::remove(file));bool found=false;QCOMPARE(cache.cached(dir.path(),&found).size(),3);QVERIFY(found);result=cache.scan(dir.path(),[]{return false;},[](int,const QString &){},&error);QCOMPARE(result.size(),2);QCOMPARE(cache.cached(dir.path()).size(),2);cache.scan(dir.path(),[]{return true;},[](int,const QString &){},&error);QCOMPARE(cache.cached(dir.path()).size(),2);
        auto p=dir.filePath("bad-length.safetensors");QFile oversized(p);QVERIFY(oversized.open(QIODevice::WriteOnly));QDataStream stream(&oversized);stream.setByteOrder(QDataStream::LittleEndian);stream<<quint64(1024ULL*1024*1024);oversized.close();QVERIFY(!inspectImageAsset(p).error.isEmpty());
    }
    void fitReserveAndMultipleDevices(){
        QVector<ImageAsset> assets(2);assets[0].bytes=3LL*1024*1024*1024;assets[1].bytes=2LL*1024*1024*1024;auto fit=makeImageFit(assets,{"Vulkan0","Vulkan1"},1024,4LL*1024*1024*1024,512,512);QCOMPARE(fit.weightBytes,5LL*1024*1024*1024);QCOMPARE(fit.backend,QString("diffusion=Vulkan0&Vulkan1,te=cpu,vae=Vulkan0"));QCOMPARE(fit.maxVram,QString("Vulkan0=-1.000,Vulkan1=-1.000"));QVERIFY(fit.notes.join('\n').contains("pagefile"));fit=makeImageFit(assets,{},512,0,512,512);QCOMPARE(fit.maxVram,QString("-0.500"));QCOMPARE(fit.backend,QString("te=cpu"));fit=makeImageFit(assets,{"cpu"},1024,0,512,512);QCOMPARE(fit.backend,QString("cpu"));QCOMPARE(parseImageDevices("log\nVulkan0\tGPU A\nVulkan1\tGPU B\nCPU\tCPU\nVulkan0\tduplicate").join('|'),QString("Vulkan0|Vulkan1"));
    }
    void portableDefaultsAndArguments(){
        auto s=ImageSettings::fromJson({});QCOMPARE(s.rootDirectory,QString("."));QCOMPARE(s.profiles.size(),3);QCOMPARE(s.profile().value("maxVram").toString(),QString("0"));
        QString executable,error;QStringList args;QUrl url;
        QVERIFY2(s.command(&executable,&args,&url,&error,false),qPrintable(error));QVERIFY(args.contains("--model"));QVERIFY(!args.contains("--diffusion-model"));QCOMPARE(url,QUrl("http://127.0.0.1:9940/"));
        for(auto id:{"flux3","qwen21"}){s.selectedProfile=id;QVERIFY(s.command(&executable,&args,&url,&error,false));QVERIFY(args.contains("--diffusion-model"));QVERIFY(!args.contains("--model"));QVERIFY(args.contains("--llm"));QVERIFY(args.contains("--vae"));QVERIFY(args.contains("te=cpu"));QVERIFY(!args.join(' ').contains("Vulkan0"));}
        s.rootDirectory="../portable-images";QCOMPARE(s.resolve("models/test.gguf"),QDir::cleanPath(QCoreApplication::applicationDirPath()+"/../portable-images/models/test.gguf"));auto copy=ImageSettings::fromJson(s.toJson());QCOMPARE(copy.toJson(),s.toJson());
    }
    void missingPartialAndSizeMismatch(){
        QTemporaryDir dir;auto s=fixture(dir.path());s.selectedProfile="flux3";QVERIFY(s.componentProblems().isEmpty());const auto path=s.resolve(s.profile().value("llm").toString());QVERIFY(QFile::remove(path));write(path+".part","unfinished");QVERIFY(s.componentProblems().join('\n').contains(".part"));QString exe,error;QStringList args;QUrl url;QVERIFY(!s.command(&exe,&args,&url,&error));QVERIFY(error.contains(".part"));
        write(path,"short");write(dir.filePath("assets.json"),QJsonDocument(QJsonArray{QJsonObject{{"target",s.profile().value("llm")},{"size",1000}}}).toJson());QVERIFY(s.componentProblems().join('\n').contains("assets.json"));
    }
    void invalidArguments(){
        auto s=ImageSettings::fromJson({});QString exe,error;QStringList args;QUrl url;auto p=s.profile();p["width"]=513;s.profiles[0]=p;QVERIFY(!s.command(&exe,&args,&url,&error,false));p["width"]=512;p["extraArguments"]=QJsonArray{"--listen-port=9988"};s.profiles[0]=p;QVERIFY(!s.command(&exe,&args,&url,&error,false));p["extraArguments"]=QJsonArray{"--clip_l","C:/files with spaces/clip.gguf"};s.profiles[0]=p;QVERIFY(s.command(&exe,&args,&url,&error,false));QCOMPARE(args.last(),QString("C:/files with spaces/clip.gguf"));s.listenIp="0.0.0.0";QVERIFY(s.command(&exe,&args,&url,&error,false));QCOMPARE(url.host(),QString("127.0.0.1"));
    }
    void lifecycleStatusAndRestart(){
        QTemporaryDir dir;auto s=fixture(dir.path());write(dir.filePath("image-status.json"),"{\"state\":\"loading\"}");ImageServer server;QSignalSpy logs(&server,&ImageServer::output);QString error;QVERIFY2(server.start(s,&error),qPrintable(error));QVERIFY(server.busy());QVERIFY(!server.ready());QTRY_VERIFY(server.webAvailable());QVERIFY(!server.ready());QVERIFY(!server.start(s,&error));
        write(dir.filePath("image-status.json"),"{\"state\":\"ready\",\"origin\":\"command_line\"}");QTRY_VERIFY(server.ready());QVERIFY(server.commandText().contains("--listen-port"));QString text;for(const auto &line:logs)text+=line[0].toString();QVERIFY(text.contains(QString::fromUtf8("Журнал изображений")));
        write(dir.filePath("image-status.json"),"{\"state\":\"empty\"}");QTRY_VERIFY(!server.ready());QVERIFY(server.webAvailable());server.stop();QTRY_VERIFY(!server.busy());QVERIFY(!server.webAvailable());QVERIFY(server.start(s,&error));server.kill();QTRY_VERIFY(!server.busy());QVERIFY(!server.ready());
    }
    void occupiedPortFailureAndCrash(){
        QTemporaryDir dir;auto s=fixture(dir.path());QTcpServer occupied;QVERIFY(occupied.listen(QHostAddress::LocalHost,quint16(s.port)));ImageServer server;QString error;QVERIFY(!server.start(s,&error));QVERIFY(!server.busy());occupied.close();
        auto p=s.profile();p["model"]="models/failure.gguf";s.profiles[0]=p;write(s.resolve(p.value("model").toString()),"bad");QVERIFY(server.start(s,&error));QTRY_VERIFY(!server.busy());QVERIFY(!server.ready());QVERIFY(server.status().contains("72"));
        s=fixture(dir.path());QVERIFY(server.start(s,&error));QTRY_VERIFY(server.ready());server.process.kill();QTRY_VERIFY(!server.busy());QVERIFY(!server.webAvailable());
    }
    void realCatalogHeaders(){
        const auto root=qEnvironmentVariable("STUDIO_SD_CATALOG_ROOT");if(root.isEmpty())QSKIP("Set STUDIO_SD_CATALOG_ROOT for read-only real header inspection.");QTemporaryDir dir;ImageCatalog cache(dir.filePath("cache.json"));QString error;auto files=cache.scan(root,[]{return false;},[](int,const QString &){},&error);QVERIFY2(error.isEmpty(),qPrintable(error));QStringList families;for(const auto &file:files){qInfo()<<QFileInfo(file.path).fileName()<<file.family<<file.error;if(file.trusted)families<<file.family;}for(auto required:{"sd21","flux3","qwen21","flux2vae","qwen21vae","qwen3vl4b","qwen3vl8b"})QVERIFY2(families.contains(required),required);
    }
    void realLocalSmoke(){
        const auto root=qEnvironmentVariable("STUDIO_SD_SMOKE_ROOT");if(root.isEmpty())QSKIP("Set STUDIO_SD_SMOKE_ROOT for explicit real runtime smoke test; no generation or downloads.");
        auto s=ImageSettings::fromJson({});s.rootDirectory=root;auto p=s.profile();p["maxVram"]="6";s.profiles[0]=p;ImageServer server;QString error;QVERIFY2(server.start(s,&error),qPrintable(error));QTRY_VERIFY_WITH_TIMEOUT(server.ready()||!server.busy(),180000);QVERIFY2(server.ready(),qPrintable(server.status()));QVERIFY(server.webAvailable());server.stop();QTRY_VERIFY_WITH_TIMEOUT(!server.busy(),10000);
    }
};
QTEST_GUILESS_MAIN(ImageTest)
#include "test_image.moc"
