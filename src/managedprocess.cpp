#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "managedprocess.h"
#include <vector>

struct ManagedProcess::NativeState {
    QString error;
#ifdef Q_OS_WIN
    HANDLE job=nullptr;
    STARTUPINFOEXW startup{};
    LPPROC_THREAD_ATTRIBUTE_LIST list=nullptr;
    std::vector<unsigned char> storage;
    NativeState(){
        auto fail=[this]{error=QString("Не удалось включить защиту дочернего процесса Windows (код %1). Запуск отменён.").arg(GetLastError());};
        job=CreateJobObjectW(nullptr,nullptr);if(!job){fail();return;}
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))){fail();return;}
        SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);if(!size){fail();return;}
        storage.resize(size);auto candidate=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if(!InitializeProcThreadAttributeList(candidate,1,0,&size)){fail();return;}list=candidate;
        // PROC_THREAD_ATTRIBUTE_JOB_LIST (Windows 10+); older MinGW headers lack the name.
        constexpr DWORD_PTR jobListAttribute=0x0002000d;
        if(!UpdateProcThreadAttribute(list,0,jobListAttribute,&job,sizeof(job),nullptr,nullptr)){fail();return;}
    }
    ~NativeState(){if(list)DeleteProcThreadAttributeList(list);if(job)CloseHandle(job);}
#endif
};
ManagedProcess::ManagedProcess(QObject *parent):QProcess(parent){
#ifdef Q_OS_WIN
    setCreateProcessArgumentsModifier([this](CreateProcessArguments *args){
        native->startup.StartupInfo=*reinterpret_cast<STARTUPINFOW*>(args->startupInfo);
        native->startup.StartupInfo.cb=sizeof(STARTUPINFOEXW);native->startup.lpAttributeList=native->list;
        args->startupInfo=reinterpret_cast<Q_STARTUPINFO*>(&native->startup.StartupInfo);
        args->flags|=EXTENDED_STARTUPINFO_PRESENT;
    });
    connect(this,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int,QProcess::ExitStatus){if(native&&native->job)TerminateJobObject(native->job,0);});
#endif
}
ManagedProcess::~ManagedProcess(){
    killTree();if(state()!=QProcess::NotRunning)waitForFinished(3000);
    disconnect(this,nullptr,this,nullptr);
}
void ManagedProcess::start(OpenMode mode){
    if(state()!=QProcess::NotRunning)return;
    native=std::make_unique<NativeState>();
    if(!native->error.isEmpty()){setErrorString(native->error);emit errorOccurred(QProcess::FailedToStart);return;}
    QProcess::start(mode);
}
void ManagedProcess::start(const QString &program,const QStringList &arguments,OpenMode mode){
    if(state()!=QProcess::NotRunning)return;setProgram(program);setArguments(arguments);start(mode);
}
void ManagedProcess::killTree(){
#ifdef Q_OS_WIN
    if(native&&native->job)TerminateJobObject(native->job,1);
#endif
    if(state()!=QProcess::NotRunning)QProcess::kill();
}
