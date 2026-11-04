#pragma once
#include "advisor.h"
#include <QDialog>
#include <QThread>
class QLineEdit;
class QTableWidget;
class QLabel;
class QCheckBox;
class QComboBox;
class QPushButton;
class ModelScan:public QThread{
    Q_OBJECT
public:
    QString directory,cacheError;bool recursive=true,canceled=false,force=false;int cacheHits=0;
signals:
    void found(const ModelInfo &info);
protected:
    void run() override;
};
class ModelLibraryDialog:public QDialog{
    Q_OBJECT
public:
    explicit ModelLibraryDialog(const QString &directory,QWidget *parent=nullptr);
    ~ModelLibraryDialog() override;
signals:
    void directoryChanged(const QString &directory);
    void modelSelected(const QString &path,const QString &parameter);
private:
    QLineEdit *directory,*filter;
    QTableWidget *table;
    QCheckBox *recursive,*force;
    QComboBox *role;
    QLabel *status;
    QPushButton *refresh,*cancel,*use;
    ModelScan scan;
    int errors=0;qint64 totalBytes=0;
    void startScan();
    void addModel(const ModelInfo &info);
    void filterRows();
    void selectModel();
    bool loadCachedDirectory();
};
