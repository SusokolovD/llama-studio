#pragma once
#include "imageengine.h"
#include <QWidget>
#include <QMap>
class QLineEdit;class QSpinBox;class QDoubleSpinBox;class QCheckBox;class QComboBox;
class QLabel;class QPlainTextEdit;class QPushButton;class QTabWidget;
class ImagePage : public QWidget {
    Q_OBJECT
public:
    explicit ImagePage(QWidget *parent=nullptr);
    void loadSettings(const QJsonObject &json);
    ImageSettings settings() const { return configuration; }
    ImageServer *server() { return &runner; }
    void setBlocked(bool value);
    bool validate();
    void start();
signals:
    void settingsChanged(const QJsonObject &json);
    void startRequested();
    void monitorRequested();
private:
    ImageSettings configuration;
    ImageServer runner;
    bool loading=false,blocked=false;
    QWidget *fields;
    QLineEdit *root,*executable,*models,*host;
    QSpinBox *port;
    QComboBox *profiles,*mode,*sampler,*autoFit;
    QMap<QString,QLineEdit*> texts;
    QMap<QString,QSpinBox*> integers;
    QCheckBox *offload,*tiling;
    QDoubleSpinBox *cfg;
    QPlainTextEdit *extras,*preview,*actual,*console;
    QLabel *note,*components,*state;
    QTabWidget *tabs;
    QPushButton *startButton,*stopButton,*openButton;
    void showProfile();
    void edited();
    void refresh();
    void append(const QString &text);
};
