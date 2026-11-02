#include "monitordialog.h"
#include "studiobutton.h"
#include <QtWidgets>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <cmath>
class MonitorHistory:public QWidget{
public:
    QVector<QPointF> samples;
    explicit MonitorHistory(QWidget *parent=nullptr):QWidget(parent){setMinimumHeight(100);setMaximumHeight(150);}
    void add(double cpu,double ram){samples.append({cpu,ram});if(samples.size()>120)samples.removeFirst();update();}
protected:
    void paintEvent(QPaintEvent *)override{
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);auto area=rect().adjusted(8,24,-8,-8);p.fillRect(rect(),QColor("#1a1f2a"));p.setPen(QColor("#354158"));for(int i=0;i<=4;++i){auto y=area.bottom()-i*area.height()/4;p.drawLine(area.left(),y,area.right(),y);}p.setPen(QColor("#b9c7dc"));p.drawText(8,17,"CPU (синий) · RAM (зелёный) · 120 измерений с шагом 2 с · шкала 0–100%");
        for(int metric=0;metric<2;++metric){QPainterPath line;bool active=false;for(int i=0;i<samples.size();++i){double value=metric?samples[i].y():samples[i].x();if(value<0){active=false;continue;}QPointF point(area.left()+double(i)*area.width()/119.0,area.bottom()-value*area.height()/100.0);if(active)line.lineTo(point);else line.moveTo(point);active=true;}p.setPen(QPen(QColor(metric?"#63d4ae":"#7d9dff"),2));p.drawPath(line);}
    }
};
namespace{
QString shown(double n,const QString &suffix,int decimals=0){return n<0?QString("Недоступно"):QString::number(n,'f',decimals)+suffix;}
void cell(QTableWidget *table,int r,int c,const QString &text){auto item=new QTableWidgetItem(text);item->setToolTip(text);table->setItem(r,c,item);}
QTableWidget *sensorTable(const QStringList &headers){auto table=new QTableWidget(0,headers.size());table->setHorizontalHeaderLabels(headers);table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->setSelectionBehavior(QAbstractItemView::SelectRows);table->verticalHeader()->hide();table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);table->horizontalHeader()->setStretchLastSection(true);return table;}
}
MonitorDialog::MonitorDialog(QWidget *parent):QDialog(parent){
    setWindowTitle("Мониторинг оборудования");setObjectName("hardwareMonitorWindow");resize(1140,820);auto root=new QVBoxLayout(this);auto title=new QLabel("Мониторинг оборудования");title->setObjectName("title");root->addWidget(title);
    auto note=new QLabel("Показатели всей системы, включая другие приложения. Обновление CPU/RAM/NVIDIA — каждые 2 секунды; дополнительных датчиков — каждые 6 секунд. Недоступные значения не заменяются нулями.");note->setWordWrap(true);root->addWidget(note);
    auto cards=new QHBoxLayout;auto card=[&](const QString &name,QProgressBar **bar,QLabel **text){auto box=new QGroupBox(name);auto layout=new QVBoxLayout(box);*text=new QLabel("Ожидание измерения…");(*text)->setWordWrap(true);layout->addWidget(*text);*bar=new QProgressBar;(*bar)->setRange(0,100);layout->addWidget(*bar);cards->addWidget(box);};card("Процессор",&cpu,&cpuText);card("Оперативная память",&ram,&ramText);root->addLayout(cards);cpu->setObjectName("monitorCpu");ram->setObjectName("monitorRam");history=new MonitorHistory;root->addWidget(history);
    auto tabs=new QTabWidget;root->addWidget(tabs,1);auto gpuPage=new QWidget;auto gpuLayout=new QVBoxLayout(gpuPage);gpuStatus=new QLabel("Ожидание NVIDIA…");gpuStatus->setWordWrap(true);gpuLayout->addWidget(gpuStatus);gpus=::sensorTable({"GPU / UUID","Температура","Загрузка GPU","VRAM занято / всего","Мощность","Вентилятор","Обновлено"});gpus->setObjectName("monitorGpuTable");gpus->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);gpuLayout->addWidget(gpus);tabs->addTab(gpuPage,"Видеокарты NVIDIA");
    auto extraPage=new QWidget;auto extra=new QVBoxLayout(extraPage);hardwareSensors=new QCheckBox("Читать датчики Libre Hardware Monitor (WMI)");hardwareSensors->setChecked(true);extra->addWidget(hardwareSensors);auto help=new QLabel("Для температуры CPU, датчиков AMD/Intel, дисков и вентиляторов запустите Libre Hardware Monitor с доступом к оборудованию и включённым WMI. Доступность зависит от платы и драйверов. Llama Studio читает датчики, но не устанавливает драйвер и не меняет обороты вентиляторов. ACPI thermal zones не выдаются за температуру CPU.");help->setWordWrap(true);extra->addWidget(help);auto link=new QLabel("<a href=\"https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases\">Официальные релизы Libre Hardware Monitor</a>");link->setOpenExternalLinks(true);extra->addWidget(link);sensorStatus=new QLabel;sensorStatus->setWordWrap(true);extra->addWidget(sensorStatus);sensorTable=::sensorTable({"Устройство / идентификатор","Датчик","Тип","Значение","Обновлено"});sensorTable->setObjectName("monitorSensorTable");extra->addWidget(sensorTable,1);tabs->addTab(extraPage,"CPU и дополнительные датчики");
    auto footer=new QHBoxLayout;auto paused=new QCheckBox("Пауза");footer->addWidget(paused);footer->addStretch();auto close=new StudioButton("Закрыть");footer->addWidget(close);root->addLayout(footer);connect(close,&QPushButton::clicked,this,&QDialog::close);connect(paused,&QCheckBox::toggled,this,[this](bool pause){if(pause){poll.stop();nvidia.killTree();sensors.killTree();}else{poll.start();sample();}});connect(hardwareSensors,&QCheckBox::toggled,this,[this](bool on){if(on){sensorTick=0;sampleSensors();}else{sensors.killTree();sensorTable->setRowCount(0);sensorStatus->setText("Опрос дополнительных датчиков выключен.");}});
    smi=QStandardPaths::findExecutable("nvidia-smi");
#ifdef Q_OS_WIN
    // A 32-bit Qt build must bypass WOW64 redirection to see 64-bit driver tools/providers.
    const auto windows=QDir(qEnvironmentVariable("SystemRoot"));
    const auto nativeDirectory=windows.filePath("Sysnative");
    const auto systemDirectory=QFileInfo(nativeDirectory).isDir()?nativeDirectory:windows.filePath("System32");
    if(smi.isEmpty()){auto candidate=QDir(systemDirectory).filePath("nvidia-smi.exe");if(QFileInfo::exists(candidate))smi=candidate;}
    if(smi.isEmpty()){auto candidate=qEnvironmentVariable("ProgramW6432")+"/NVIDIA Corporation/NVSMI/nvidia-smi.exe";if(QFileInfo::exists(candidate))smi=candidate;}
    powershell=QDir(systemDirectory).filePath("WindowsPowerShell/v1.0/powershell.exe");
#endif
    nvidiaTimeout.setSingleShot(true);sensorsTimeout.setSingleShot(true);connect(&nvidiaTimeout,&QTimer::timeout,this,[this]{nvidia.killTree();});connect(&sensorsTimeout,&QTimer::timeout,this,[this]{sensors.killTree();});
    connect(&nvidia,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){nvidiaTimeout.stop();gpus->setRowCount(0);gpuStatus->setText("NVIDIA: "+nvidia.errorString());});
    connect(&nvidia,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus exit){nvidiaTimeout.stop();auto rows=parseNvidiaReadings(QString::fromUtf8(nvidia.readAllStandardOutput()));auto error=QString::fromUtf8(nvidia.readAllStandardError()).trimmed().left(400);gpus->setRowCount(0);if(code||exit!=QProcess::NormalExit||rows.isEmpty()){gpuStatus->setText("NVIDIA: данные недоступны. "+error);return;}gpuStatus->setText(QString("Устройств: %1 · источник nvidia-smi · загрузка GPU — показатель занятости, не доля VRAM.").arg(rows.size()));const auto time=QTime::currentTime().toString("HH:mm:ss");for(const auto &r:rows){int i=gpus->rowCount();gpus->insertRow(i);cell(gpus,i,0,r.name+"\n"+r.uuid);cell(gpus,i,1,shown(r.temperature," °C"));cell(gpus,i,2,shown(r.load," %"));cell(gpus,i,3,shown(r.memoryUsed," МиБ")+" / "+shown(r.memoryTotal," МиБ"));cell(gpus,i,4,shown(r.power," Вт",1));cell(gpus,i,5,shown(r.fan," %"));cell(gpus,i,6,time);}});
    connect(&sensors,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){sensorsTimeout.stop();sensorTable->setRowCount(0);sensorStatus->setText("Датчики: "+sensors.errorString());});
    connect(&sensors,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus exit){sensorsTimeout.stop();QJsonParseError error;const auto doc=QJsonDocument::fromJson(sensors.readAllStandardOutput().trimmed(),&error);sensors.readAllStandardError();sensorTable->setRowCount(0);if(!hardwareSensors->isChecked())return;if(code||exit!=QProcess::NormalExit||error.error!=QJsonParseError::NoError||!doc.isArray()){sensorStatus->setText("Датчики недоступны: запустите Libre Hardware Monitor и включите WMI. Возможны ограничения доступа к провайдеру.");return;}const auto time=QTime::currentTime().toString("HH:mm:ss");for(const auto &v:doc.array()){auto s=v.toObject();auto reading=s.value("Value");if(!reading.isDouble()||!std::isfinite(reading.toDouble()))continue;QString kind=s.value("SensorType").toString(),unit;if(kind=="Temperature"){unit=" °C";kind="Температура";}else if(kind=="Load"){unit=" %";kind="Загрузка";}else if(kind=="Fan"){unit=" об/мин";kind="Вентилятор";}else if(kind=="Power"){unit=" Вт";kind="Мощность";}else if(kind=="Clock"){unit=" МГц";kind="Частота";}else continue;int r=sensorTable->rowCount();sensorTable->insertRow(r);cell(sensorTable,r,0,s.value("Parent").toString());cell(sensorTable,r,1,s.value("Name").toString());cell(sensorTable,r,2,kind);cell(sensorTable,r,3,QString::number(reading.toDouble(),'f',1)+unit);cell(sensorTable,r,4,time);}sensorStatus->setText(QString("Libre Hardware Monitor / WMI: датчиков %1. Температура CPU доступна только при наличии соответствующего датчика.").arg(sensorTable->rowCount()));});
    cpu->setEnabled(false);ram->setEnabled(false);poll.setInterval(2000);connect(&poll,&QTimer::timeout,this,&MonitorDialog::sample);poll.start();QTimer::singleShot(0,this,&MonitorDialog::sample);
}
MonitorDialog::~MonitorDialog(){poll.stop();nvidiaTimeout.stop();sensorsTimeout.stop();disconnect(&nvidia,nullptr,this,nullptr);disconnect(&sensors,nullptr,this,nullptr);nvidia.killTree();sensors.killTree();nvidia.waitForFinished(1000);sensors.waitForFinished(1000);}
void MonitorDialog::sample(){
    const auto r=sampler.sample();cpu->setEnabled(r.cpuPercent>=0);cpu->setValue(r.cpuPercent<0?0:qRound(r.cpuPercent));cpuText->setText(r.cpuPercent<0?"Ожидание второго измерения / недоступно":shown(r.cpuPercent," %",1));double memory=-1;if(r.memoryTotal){memory=100.0*r.memoryUsed/r.memoryTotal;ram->setEnabled(true);ram->setValue(qRound(memory));ramText->setText(QString("%1 / %2 ГиБ · %3 %").arg(r.memoryUsed/1073741824.0,0,'f',2).arg(r.memoryTotal/1073741824.0,0,'f',2).arg(memory,0,'f',1));}else{ram->setEnabled(false);ramText->setText("Недоступно на этой платформе");}history->add(r.cpuPercent,memory);
    if(smi.isEmpty())gpuStatus->setText("nvidia-smi не найден. Установите драйвер NVIDIA; для других GPU используйте вкладку дополнительных датчиков.");else if(nvidia.state()==QProcess::NotRunning){nvidia.start(smi,{"--query-gpu=uuid,name,temperature.gpu,utilization.gpu,memory.used,memory.total,power.draw,fan.speed","--format=csv,noheader,nounits"});nvidiaTimeout.start(4500);}
    if(sensorTick++%3==0)sampleSensors();
}
void MonitorDialog::sampleSensors(){
    if(!hardwareSensors->isChecked()||sensors.state()!=QProcess::NotRunning)return;if(!QFileInfo::exists(powershell)){sensorStatus->setText("Опрос WMI доступен в Windows через штатный Windows PowerShell.");return;}
    // Use the OS-supplied Windows PowerShell, not optional pwsh.exe. No shell interpolation or profile scripts.
    const QString script="[Console]::OutputEncoding=New-Object System.Text.UTF8Encoding($false); $ErrorActionPreference='Stop'; try { $s=@(Get-CimInstance -Namespace root/LibreHardwareMonitor -ClassName Sensor -OperationTimeoutSec 3 | Where-Object { $_.SensorType -in @('Temperature','Load','Fan','Power','Clock') } | Select-Object Parent,Name,SensorType,Value); ConvertTo-Json -InputObject $s -Compress } catch { exit 2 }";
    sensors.start(powershell,{"-NoLogo","-NoProfile","-NonInteractive","-Command",script});sensorsTimeout.start(5500);
}
