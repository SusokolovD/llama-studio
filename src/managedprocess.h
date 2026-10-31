#pragma once
#include <QProcess>
#include <memory>
// Windows: create the child inside a private kill-on-close job, atomically.
// The job handle belongs only to this application, never to the child.
class ManagedProcess : public QProcess {
public:
    explicit ManagedProcess(QObject *parent=nullptr);
    ~ManagedProcess() override;
    void start(OpenMode mode=ReadWrite);
    void start(const QString &program,const QStringList &arguments,OpenMode mode=ReadWrite);
    void killTree();
private:
    struct NativeState;
    std::unique_ptr<NativeState> native;
};
