#pragma once
#include "config.h"
#include "releasechecker.h"
#include "managedprocess.h"
#include "modelperformance.h"
#include "gpupower.h"
#include <QMainWindow>
#include <QProcess>
#include <QNetworkAccessManager>
#include <QTimer>
class QLineEdit;
class QSpinBox;
class QPlainTextEdit;
class QListWidget;
class QScrollArea;
class QTabWidget;
class QLabel;
class QPushButton;
class QVBoxLayout;
class BuilderPage;
class ImagePage;
class QComboBox;
class QStackedWidget;
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr, bool checkUpdatesOnStartup = true,bool restoreLastProfile=true,const QString &powerExecutable=nvidiaSmiPath());
    bool smokeTest(const QString &screenshot);
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    friend class GuiTest;
    Config config;
    ManagedProcess server, fit;
    ModelPerformance performance;
    GpuPowerService power;
    bool launchAfterPower=false,launchImageAfterPower=false,powerReady=false,powerResetStarted=false,powerResetDone=false;
    QNetworkAccessManager network;
    ReleaseChecker releases;
    QTimer healthTimer;
    QLineEdit *directory, *search;
    QSpinBox *context;
    QPlainTextEdit *preview, *runningCommand, *console;
    QListWidget *navigation;
    QScrollArea *scroll;
    QTabWidget *tabs;
    QLabel *status, *count;
    QPushButton *startButton, *stopButton, *fitButton, *cancelFitButton, *openButton;
    QWidget *editor;
    BuilderPage *buildPage=nullptr;
    ImagePage *imagePage=nullptr;
    QComboBox *engineSelector=nullptr;
    QStackedWidget *enginePages=nullptr;
    bool closeAfterBuild=false;
    bool closeConfirmed=false,closeQuestionOpen=false;
    QByteArray fitOutput, serverPending, serverErrorPending, fitPending, fitErrorPending;
    QUrl runningUrl;
    QByteArray runningKey;
    bool healthPending = false, stopping = false, fitCanceled = false;
    quint64 serverRun = 0, fitRun = 0;
    void buildUi();
    void rebuild();
    void updatePreview();
    void updateButtons();
    void loadFile(bool catalog = false);
    void saveFile();
    bool loadProfile(const QString &path,QString *error,bool catalog=false);
    bool saveProfile(const QString &path,QString *error);
    void reset();
    void showAdvisor();
    void showModels();
    void showMonitor();
    void launchServer();
    void launchImageServer();
    void launchFit();
    void stop(QProcess &process);
    void pollHealth();
    void log(const QString &text);
    void readLog(QProcess &process, bool stdoutChannel, QByteArray &pending, bool finish = false);
    void error(const QString &text);
    bool validateModel();
    QUrl endpoint() const;
};
