#include <QCoreApplication>
#include <QFileInfo>
#include <QTcpServer>
#include <QTcpSocket>
#include <cstdio>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv); auto args = app.arguments();
    if (args.contains("--version")) { printf("version: 42 (fixture)\n"); return 0; }
    if (args.contains("--list-devices")) { printf("Available devices:\n  CUDA0: Fixture A (24576 MiB, 22000 MiB free)\n  CUDA1: Fixture B (12288 MiB, 10000 MiB free)\n"); return 0; }
    int modelIndex = args.indexOf("--model");
    if (modelIndex < 0 || !QFileInfo::exists(args.value(modelIndex + 1))) return 41;
    if (args.value(modelIndex + 1).contains("failure")) { fprintf(stderr, "fixture: deliberate failure\n"); return 42; }
    if (QFileInfo(app.applicationFilePath()).baseName().contains("fit")) {
        auto ctx = args.value(args.indexOf("--ctx-size") + 1);
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
    return app.exec();
}
