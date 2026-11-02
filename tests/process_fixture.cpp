#include <QCoreApplication>
#include <QFileInfo>
#include <QTcpServer>
#include <QTcpSocket>
#include <QDir>
#include <QFile>
#include <QTimer>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv); auto args = app.arguments();
    if(args.contains("--hold-worker")){QTimer::singleShot(60000,&app,&QCoreApplication::quit);return app.exec();}
    if(args.contains("--process-tree")){
        QProcess worker;worker.start(app.applicationFilePath(),QStringList{"--hold-worker"});if(!worker.waitForStarted(5000))return 51;
        QFile file(args.value(args.indexOf("--process-tree")+1));if(!file.open(QIODevice::WriteOnly))return 52;
        file.write(QJsonDocument(QJsonObject{{"parentPid",double(app.applicationPid())},{"workerPid",double(worker.processId())}}).toJson());file.close();
        QTimer::singleShot(60000,&app,&QCoreApplication::quit);return app.exec();
    }
    if(args.contains("--fail-build"))return 42;
    if(args.contains("--sleep-build")){QTimer::singleShot(60000,&app,&QCoreApplication::quit);return app.exec();}
    if(args.contains("--make-build")){
        auto dir=args.value(args.indexOf("--make-build")+1);if(!QDir().mkpath(dir))return 44;
#ifdef Q_OS_WIN
        QString ext=".exe";
#else
        QString ext;
#endif
        for(auto name:{"llama-server","llama-fit-params"}){QString path=QDir(dir).filePath(QString(name)+ext);QFile::remove(path);if(!QFile::copy(app.applicationFilePath(),path))return 45;}
        QFile dll(QDir(dir).filePath("zz-runtime.dll"));if(!dll.open(QIODevice::WriteOnly))return 46;dll.write("new runtime");return 0;
    }
    if (args.contains("--version")) {QFile output("fixture-version.txt");if(output.open(QIODevice::ReadOnly)){const auto bytes=output.readAll();fwrite(bytes.constData(),1,size_t(bytes.size()),stdout);}else printf("version: 42 (fixture)\n"); return 0; }
    if (args.contains("--list-devices")) { printf("Available devices:\n  CUDA0: Fixture A (24576 MiB, 22000 MiB free)\n  CUDA1: Fixture B (12288 MiB, 10000 MiB free)\n"); return 0; }
    int modelIndex = args.indexOf("--model");
    if (modelIndex < 0 || !QFileInfo::exists(args.value(modelIndex + 1))) return 41;
    if (args.value(modelIndex + 1).contains("failure")) { fprintf(stderr, "fixture: deliberate failure\n"); return 42; }
    if (QFileInfo(app.applicationFilePath()).baseName().contains("fit")) {
        auto ctx = args.value(args.indexOf("--ctx-size") + 1);if(ctx=="0")ctx="32768";
        printf("-c %s -ngl 24 -ot \"blk\\.14\\.ffn_(up|down)=CPU\"\n", qPrintable(ctx)); fflush(stdout); return 0;
    }
    QTcpServer server;
    QObject::connect(&server, &QTcpServer::newConnection, [&] {
        while (auto socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, [socket] {
                socket->readAll(); socket->write("HTTP/1.1 200 OK\r\nContent-Length: 15\r\nConnection: close\r\n\r\n{\"status\":\"ok\"}"); socket->disconnectFromHost();
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
    if (!server.listen(QHostAddress::LocalHost, args.value(args.indexOf("--port") + 1).toUShort())) return 43;
    fprintf(stderr, "fixture server ready\n"); fflush(stderr);
    QTimer::singleShot(100,[]{fprintf(stderr,"slot print_timings: id 0 | task 7 | prompt eval time = 100.00 ms / 100 tokens (1.00 ms per token, ");fflush(stderr);});
    QTimer::singleShot(150,[]{fprintf(stderr,"1000.00 tokens per second)\nslot print_timings: id 0 | task 7 | eval time = 200.00 ms / 10 tokens (20.00 ms per token, 50.00 tokens per second)\n");fflush(stderr);});
    return app.exec();
}
