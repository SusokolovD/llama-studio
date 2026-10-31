#pragma once
#include "buildservice.h"
#include <QWidget>
class QLineEdit;class QPlainTextEdit;class QLabel;class QPushButton;
class BuilderPage : public QWidget {
    Q_OBJECT
public:
    explicit BuilderPage(QWidget *parent=nullptr);
    void loadSettings(const QJsonObject &settings);
    BuildSettings settings() const;
    bool busy() const{return service.busy();}
    void start();
    void cancel(){service.cancel();}
signals:
    void startRequested();
    void settingsChanged(const QJsonObject &settings);
    void busyChanged(bool busy);
    void deployed(const QString &directory);
private:
    BuildService service;
    QLineEdit *source,*binary,*target;
    QPlainTextEdit *commands,*log;
    QLabel *state;
    QPushButton *startButton,*cancelButton;
    QWidget *fields;
};
