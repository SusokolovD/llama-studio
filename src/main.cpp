#include "mainwindow.h"
#include "version.h"
#include <QApplication>
#include <QFile>
#include <QTimer>
#include <QStyleFactory>
int main(int argc, char **argv) {
    QApplication app(argc, argv); app.setApplicationName("LlamaStudio"); app.setOrganizationName("LlamaStudio");
    app.setApplicationVersion(STUDIO_VERSION);app.setWindowIcon(QIcon(":/app-icon.png"));
    app.setStyle(QStyleFactory::create("Fusion"));
    QFile style(":/style.qss"); if (style.open(QIODevice::ReadOnly)) app.setStyleSheet(QString::fromUtf8(style.readAll()));
    MainWindow window(nullptr, !app.arguments().contains("--smoke-test")); window.show();
    if (app.arguments().contains("--smoke-test")) QTimer::singleShot(100, &app, [&] {
        QString screenshot; int index = app.arguments().indexOf("--screenshot"); if (index >= 0 && index + 1 < app.arguments().size()) screenshot = app.arguments()[index + 1];
        app.exit(window.smokeTest(screenshot) ? 0 : 1);
    });
    return app.exec();
}
