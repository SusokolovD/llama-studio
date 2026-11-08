#include "buildservice.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
class BuildTest:public QObject {
    Q_OBJECT
    bool git(const QString &where,const QStringList &args){QProcess p;p.start("git",QStringList{"-C",where}+args);if(!p.waitForFinished(10000))return false;if(p.exitCode()!=0)qWarning()<<p.readAllStandardError();return p.exitCode()==0;}
    void write(const QString &path,const QByteArray &data){QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly));QCOMPARE(f.write(data),qint64(data.size()));}
    QByteArray read(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return f.readAll();}
    bool prepare(const QString &root,BuildSettings *s){
        const auto origin=QDir(root).filePath("origin");QDir().mkpath(origin);
        if(!git(origin,{"init","-b","main"}))return false;
        write(QDir(origin).filePath("CMakeLists.txt"),"# v1\n");
        if(!git(origin,{"add","CMakeLists.txt"})||!git(origin,{"-c","user.name=Test","-c","user.email=test@example.invalid","commit","-m","initial"}))return false;
        const auto source=QDir(root).filePath(QString::fromUtf8("исходники с пробелом"));
        QProcess clone;clone.start("git",{"clone",origin,source});if(!clone.waitForFinished(10000)||clone.exitCode()!=0)return false;
        write(QDir(origin).filePath("CMakeLists.txt"),"# v2\n");
        if(!git(origin,{"add","CMakeLists.txt"})||!git(origin,{"-c","user.name=Test","-c","user.email=test@example.invalid","commit","-m","update"}))return false;
        *s={source,"out",QDir(root).filePath("llama-build"),BuildRecipe::fromLegacy(QString("\"%1\" --make-build \"%2\"").arg(QString(FIXTURE_PATH),QDir(source).filePath("out")))};return true;
    }
private slots:
    void pullSelectedFork_data(){QTest::addColumn<QString>("branch");QTest::newRow("explicit branch")<<QString("main");QTest::newRow("repository HEAD")<<QString();}
    void pullSelectedFork(){
        QFETCH(QString,branch);QTemporaryDir root;BuildSettings s;QVERIFY(prepare(root.path(),&s));
        const auto origin=QDir(root.path()).filePath("origin"),fork=QDir(root.path()).filePath(QString::fromUtf8("форк с пробелом"));
        QVERIFY(git(root.path(),{"clone",origin,fork}));write(QDir(fork).filePath("CMakeLists.txt"),"# fork v3\n");
        QVERIFY(git(fork,{"add","CMakeLists.txt"}));QVERIFY(git(fork,{"-c","user.name=Test","-c","user.email=test@example.invalid","commit","-m","fork update"}));
        s.repositoryUrl=fork;s.repositoryBranch=branch;s=BuildSettings::fromJson(s.toJson());QCOMPARE(s.repositoryUrl,fork);QCOMPARE(s.repositoryBranch,branch);
        BuildService service;QSignalSpy done(&service,&BuildService::finished);QString error;QVERIFY2(service.start(s,&error),qPrintable(error));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,20000);QVERIFY2(done[0][0].toBool(),qPrintable(done[0][1].toString()));
        QCOMPARE(read(QDir(s.sourceDirectory).filePath("CMakeLists.txt")).trimmed(),QByteArray("# fork v3"));QCOMPARE(read(QDir(origin).filePath("CMakeLists.txt")).trimmed(),QByteArray("# v2"));
        QProcess remote;remote.start("git",{"-C",s.sourceDirectory,"remote","get-url","origin"});QVERIFY(remote.waitForFinished(10000));QCOMPARE(QString::fromUtf8(remote.readAllStandardOutput()).trimmed(),origin);
        QVERIFY(QFileInfo(QDir(s.targetDirectory).filePath("zz-runtime.dll")).isFile());
    }
    void invalidRepositoryStopsBeforeBuild(){
        QTemporaryDir root;BuildSettings s;QVERIFY(prepare(root.path(),&s));QString error;
        s.repositoryBranch="main";QVERIFY(!s.validate(&error));s.repositoryBranch.clear();s.repositoryUrl="--upload-pack=unexpected";QVERIFY(!s.validate(&error));s.repositoryUrl="bad\nrepository";QVERIFY(!s.validate(&error));
        s.repositoryUrl=root.filePath("missing-repository");BuildService service;QSignalSpy done(&service,&BuildService::finished);QVERIFY(service.start(s,&error));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,20000);QVERIFY(!done[0][0].toBool());QVERIFY(!QFileInfo::exists(s.targetDirectory));QVERIFY(!QFileInfo::exists(QDir(s.sourceDirectory).filePath("out")));
    }
    void jsonRecipeAndMigration(){
        const QByteArray json=R"({"schemaVersion":1,"commands":[{"program":"C:\\Program Files\\cmake.exe","arguments":["-DNAME=a b","C:\\папка с пробелом\\build",""]}]})";
        QVector<BuildCommand> commands;QString error;QVERIFY2(BuildRecipe::parse(json,&commands,&error),qPrintable(error));QCOMPARE(commands.size(),1);QCOMPARE(commands[0].arguments[0],QString("-DNAME=a b"));QCOMPARE(commands[0].arguments[1],QString::fromUtf8("C:\\папка с пробелом\\build"));QCOMPARE(commands[0].arguments[2],QString());
        auto snapshot=commands;QVERIFY(!BuildRecipe::parse(R"({"schemaVersion":1,"commands":[{"program":"cmake","arguments":[7]}]})",&commands,&error));QCOMPARE(commands.size(),snapshot.size());
        QVERIFY(!BuildRecipe::parse(R"({"schemaVersion":2,"commands":[]})",nullptr,&error));
        auto old=BuildSettings::fromJson({{"commands","cmake --build \"build folder\"\n# note\ncmake --version"}});QVERIFY(BuildRecipe::parse(old.commands.toUtf8(),&commands,&error));QCOMPARE(commands.size(),2);QCOMPARE(commands[0].arguments[1],QString("build folder"));QVERIFY(old.toJson().value("recipe").isObject());
        QVERIFY(old.repositoryUrl.isEmpty());QVERIFY(old.repositoryBranch.isEmpty());
        old.commands="{broken JSON";auto restored=BuildSettings::fromJson(old.toJson());QCOMPARE(restored.commands,old.commands);
    }
    void pullBuildDeployAndReplace(){
        QTemporaryDir root;BuildSettings s;QVERIFY(prepare(root.path(),&s));QDir().mkpath(s.targetDirectory);
#ifdef Q_OS_WIN
        QString server="llama-server.exe";
#else
        QString server="llama-server";
#endif
        write(QDir(s.targetDirectory).filePath(server),"old executable");write(QDir(s.targetDirectory).filePath("keep.txt"),"keep");
        BuildService service;QSignalSpy done(&service,&BuildService::finished);QString error;QVERIFY2(service.start(s,&error),qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,20000);QVERIFY2(done[0][0].toBool(),qPrintable(done[0][1].toString()));
        QCOMPARE(read(QDir(s.sourceDirectory).filePath("CMakeLists.txt")).trimmed(),QByteArray("# v2"));
        QCOMPARE(read(QDir(s.targetDirectory).filePath("zz-runtime.dll")),QByteArray("new runtime"));QCOMPARE(read(QDir(s.targetDirectory).filePath("keep.txt")),QByteArray("keep"));QVERIFY(read(QDir(s.targetDirectory).filePath(server))!="old executable");
        auto serialized=BuildSettings::fromJson(s.toJson());QCOMPARE(serialized.commands,s.commands);QCOMPARE(serialized.targetDirectory,s.targetDirectory);
    }
    void buildFailureAndCancelPreserveTarget(){
        QTemporaryDir root;BuildSettings s;QVERIFY(prepare(root.path(),&s));QDir().mkpath(s.targetDirectory);write(QDir(s.targetDirectory).filePath("keep.txt"),"old");
        s.commands=BuildRecipe::fromLegacy(QString("\"%1\" --fail-build").arg(QString(FIXTURE_PATH)));BuildService service;QSignalSpy done(&service,&BuildService::finished);QString error;QVERIFY(service.start(s,&error));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,20000);QVERIFY(!done[0][0].toBool());QCOMPARE(read(QDir(s.targetDirectory).filePath("keep.txt")),QByteArray("old"));
        done.clear();s.commands=BuildRecipe::fromLegacy(QString("\"%1\" --sleep-build").arg(QString(FIXTURE_PATH)));
        connect(&service,&BuildService::phaseChanged,&service,[&service](const QString &phase){if(phase.startsWith(QString::fromUtf8("Сборка ·")))QTimer::singleShot(150,&service,&BuildService::cancel);});
        QVERIFY(service.start(s,&error));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,15000);QVERIFY(!done[0][0].toBool());QCOMPARE(read(QDir(s.targetDirectory).filePath("keep.txt")),QByteArray("old"));
    }
    void rollbackAndPathValidation(){
        QTemporaryDir root;BuildSettings s;QVERIFY(prepare(root.path(),&s));auto bad=s;QString error;bad.targetDirectory=s.sourceDirectory;QVERIFY(!bad.validate(&error));bad=s;bad.targetDirectory=QDir(s.sourceDirectory).filePath("out/nested");QVERIFY(!bad.validate(&error));
        QDir().mkpath(QDir(s.targetDirectory).filePath("zz-runtime.dll"));
#ifdef Q_OS_WIN
        QString server="llama-server.exe";
#else
        QString server="llama-server";
#endif
        write(QDir(s.targetDirectory).filePath(server),"old executable");BuildService service;QSignalSpy done(&service,&BuildService::finished);QVERIFY(service.start(s,&error));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,20000);QVERIFY(!done[0][0].toBool());QCOMPARE(read(QDir(s.targetDirectory).filePath(server)),QByteArray("old executable"));
    }
};
QTEST_GUILESS_MAIN(BuildTest)
#include "test_build.moc"
