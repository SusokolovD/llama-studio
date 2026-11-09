#pragma once
#include "imagecatalog.h"
#include "imageengine.h"
#include <QDialog>
#include <QThread>
class QLineEdit;class QComboBox;class QLabel;class QTableWidget;class QListWidget;class QSpinBox;class QPushButton;
class ImageCatalogWorker:public QThread {
    Q_OBJECT
public:
    QString directory,error;QVector<ImageAsset> assets;
    bool canceled=false;
signals:
    void progress(int count,const QString &name);
protected:
    void run()override;
};
class ImageCatalogDialog:public QDialog {
    Q_OBJECT
public:
    explicit ImageCatalogDialog(const ImageSettings &settings,QWidget *parent=nullptr);
    ~ImageCatalogDialog()override;
    ImageSettings resultSettings() const { return proposedSettings; }
private:
    ImageSettings original,proposedSettings;
    ImageCatalogWorker worker;
    ManagedProcess probe;
    QTimer probeTimeout;
    QByteArray probeOutput;
    QVector<ImageAsset> assets;
    QLineEdit *directory,*search;
    QTableWidget *table;
    QComboBox *model,*encoder,*vae,*deviceMode;
    QListWidget *devices;
    QSpinBox *reserve;
    QLabel *state,*proposal;
    QPushButton *scan,*browse,*apply,*cancelScan,*detect;
    QWidget *selectionFields;
    void populate();
    void modelChanged();
    void updateProposal();
    void scanFolder();
    void loadCache();
    void detectDevices();
    void applySelection();
    QVector<ImageAsset> selectedComponents()const;
    QStringList selectedDevices()const;
};
