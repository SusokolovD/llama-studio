#include "managedprocess.h"
#include <QCoreApplication>
#include <cstdio>
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);if(app.arguments().size()!=3)return 1;
    ManagedProcess child;
    QObject::connect(&child,&QProcess::errorOccurred,&app,[&](QProcess::ProcessError){fprintf(stderr,"%s\n",qPrintable(child.errorString()));app.exit(2);});
    QObject::connect(&child,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),&app,[&](int,QProcess::ExitStatus){app.exit(3);});
    child.start(app.arguments()[1],QStringList{"--process-tree",app.arguments()[2]});
    return app.exec();
}
