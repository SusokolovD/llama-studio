#include <QtTest>
#include <QProcess>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
class ProcessGuardTest:public QObject{
    Q_OBJECT
private slots:
    void ownerCrashKillsTree(){
#ifndef Q_OS_WIN
        QSKIP("Windows Job Object crash cleanup");
#else
        QTemporaryDir dir;const QString path=dir.filePath("pids.json");
        QProcess unrelated;unrelated.start(FIXTURE_PATH,QStringList{"--hold-worker"});QVERIFY(unrelated.waitForStarted(5000));
        QProcess owner;owner.start(HOST_PATH,QStringList{FIXTURE_PATH,path});QVERIFY(owner.waitForStarted(5000));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(path).size()>0,10000);QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));auto pids=QJsonDocument::fromJson(file.readAll()).object();
        struct Watch{HANDLE handle;~Watch(){if(handle)CloseHandle(handle);}};
        Watch root{OpenProcess(SYNCHRONIZE,FALSE,DWORD(pids["parentPid"].toDouble()))};Watch worker{OpenProcess(SYNCHRONIZE,FALSE,DWORD(pids["workerPid"].toDouble()))};
        QVERIFY(root.handle);QVERIFY(worker.handle);QCOMPARE(WaitForSingleObject(root.handle,0),DWORD(WAIT_TIMEOUT));QCOMPARE(WaitForSingleObject(worker.handle,0),DWORD(WAIT_TIMEOUT));
        // TerminateProcess bypasses destructors and application exit handlers.
        owner.kill();QVERIFY(owner.waitForFinished(5000));
        QTRY_COMPARE_WITH_TIMEOUT(WaitForSingleObject(root.handle,0),DWORD(WAIT_OBJECT_0),5000);
        QTRY_COMPARE_WITH_TIMEOUT(WaitForSingleObject(worker.handle,0),DWORD(WAIT_OBJECT_0),5000);
        QCOMPARE(unrelated.state(),QProcess::Running);unrelated.kill();QVERIFY(unrelated.waitForFinished(5000));
#endif
    }
};
QTEST_GUILESS_MAIN(ProcessGuardTest)
#include "test_process_guard.moc"
