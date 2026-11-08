#pragma once
#include "advisor.h"
#include <QDialog>
class QLineEdit;
class QComboBox;
class QTableWidget;
class QLabel;
class QPushButton;
class ModelPickerDialog:public QDialog{
    Q_OBJECT
public:
    explicit ModelPickerDialog(const QString &title,const QString &current,QWidget *parent=nullptr);
    static QString choose(QWidget *parent,const QString &title,const QString &current);
    QString selectedPath()const{return selected;}
private:
    QVector<ModelInfo> models;
    QLineEdit *filter;
    QComboBox *source,*typeFilter;
    QTableWidget *table;
    QLabel *status;
    QPushButton *use;
    QString currentPath,selected;
    void filterRows();
    void selectModel();
};
