/* MSHV CpuWudgetW
 * Copyright 2015 Hrisimir Hristov, LZ2HV
 * May be used under the terms of the GNU General Public License (GPL)
 */
#include "cpuwudget.h"
#if defined _MACOS_
#include <QProgressBar>
#endif
//#define MAX_H_DISPLY_CPU 2

////////////////////////////////////////////////////////////////////////////////
CpuWudget_W::CpuWudget_W(int CpuN,int cpu_count,/*bool all_in_one,*/QWidget *parent)
        : QWidget(parent)
{
    s_cpu_count = cpu_count;

    QHBoxLayout *H_mcpu = new QHBoxLayout(this);
    H_mcpu->setContentsMargins ( 0, 0, 0, 0);

    QHBoxLayout *H_mcpuv = new QHBoxLayout();
    H_mcpuv->setContentsMargins ( 0, 0, 0, 0);
    H_mcpuv->setSpacing(1);

    QLabel *l_cpu;
    l_cpu = new QLabel();
    int h_150 = 13;//1.56=150% disp res

    /*if (!all_in_one)
    {
        //l_cpu->setFixedSize(38, 12);//38
        l_cpu->setFixedHeight(h_150);
        l_cpu->setText("CPU"+QString("%1").arg(CpuN+1)+":");//THD CPU THRD
        //l_cpu->setText("THD"+QString("%1").arg(CpuN+1)+":");//THD CPU THRD
        H_mcpu->addWidget(l_cpu);
        TCpuSensorHv = new CpuSensorHv("cpu"+QString("%1").arg(CpuN), CpuN);
        THvProgBarSlowH = new HvProgBarSlowH(0,0,QPixmap(":pic/progress_on39.png"), QPixmap(":pic/progress_off39.png"), this);
        H_mcpu->addWidget(THvProgBarSlowH);
        H_mcpu->setAlignment(THvProgBarSlowH,Qt::AlignBottom);
    }
    else*/
    //{
    //l_cpu->setFixedSize(60, 12);//60
    l_cpu->setFixedHeight(h_150);
    l_cpu->setText("CPU "+QString("%1").arg(CpuN+1)+"-"+QString("%1").arg(cpu_count)+":");//THD CPU THRD
    //l_cpu->setText("THD "+QString("%1").arg(CpuN+1)+"-"+QString("%1").arg(cpu_count)+":");//THD CPU THRD
    H_mcpu->addWidget(l_cpu);
    for (int i = 0; i < s_cpu_count; i++)
    {
        CpuSensorHv *TTCpuSensorHv = new CpuSensorHv("cpu"+QString("%1").arg(i), i);
        ListCpuSensor.append(TTCpuSensorHv);
#if !defined(_MACOS_)
        // 12-vertical-bar layout — works fine on Linux/Windows. On macOS the
        // PNG assets render near-invisible in dark mode, so we ditch them
        // and use a single theme-aware QProgressBar (built below).
        HvProgBarSlowV *TTHvProgBarSlowV = new HvProgBarSlowV(0,0,QPixmap(":pic/progress_on_v12.png"), QPixmap(":pic/progress_off_v12.png"), this);
        ListProgBarV.append(TTHvProgBarSlowV);
        H_mcpuv->addWidget(TTHvProgBarSlowV);
        H_mcpuv->setAlignment(TTHvProgBarSlowV,Qt::AlignBottom);
#endif
    }
#if defined _MACOS_
    MacCpuBar = new QProgressBar();
    MacCpuBar->setRange(0, 100);
    MacCpuBar->setTextVisible(false);
    MacCpuBar->setOrientation(Qt::Horizontal);
    MacCpuBar->setFixedHeight(h_150 - 1);
    MacCpuBar->setMinimumWidth(60);
    // Green → yellow → red gradient — standard CPU-meter aesthetic, plays
    // nicely with both light and dark backgrounds. Chunk fills left to
    // right so the colour at any given fill level is meaningful (low =
    // green, near full = red).
    MacCpuBar->setStyleSheet(
        "QProgressBar {"
        "  background-color: rgba(40,40,40,180);"
        "  border: 1px solid rgb(80,80,80);"
        "  border-radius: 3px;"
        "  margin: 1px;"
        "}"
        "QProgressBar::chunk {"
        "  background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "    stop:0    rgb(46,204,64),"   // green
        "    stop:0.55 rgb(255,220,0),"   // yellow
        "    stop:0.85 rgb(255,133,27),"  // orange
        "    stop:1    rgb(255,65,54));"  // red
        "  border-radius: 2px;"
        "}");
    H_mcpu->addWidget(MacCpuBar);
#endif
    H_mcpu->addLayout(H_mcpuv);
    //}

    l_cpu0 = new QLabel();
    l_cpu0->setFixedSize(38, h_150);//38 h_150=for->150% font size
    l_cpu0->setText("0%");

    H_mcpu->addWidget(l_cpu0);
    H_mcpu->setAlignment(Qt::AlignRight);
    H_mcpu->setSpacing(2);
    setLayout(H_mcpu);

}
CpuWudget_W::~CpuWudget_W()
{}
/*void CpuWudget_W::SetValue()
{
    int  cpu0 = TCpuSensorHv->getCPULoad();
    l_cpu0->setText(QString("%1%").arg(cpu0));//.arg(cpu0));
    THvProgBarSlowH->setValue(cpu0, 100);
}*/
void CpuWudget_W::SetValueAllInOne()
{
    int cpus_losd = 0;
    for (int i = 0; i < s_cpu_count; i++)
    {
        int load = ListCpuSensor.at(i)->getCPULoad();
        cpus_losd += load;
#if !defined(_MACOS_)
        ListProgBarV.at(i)->setValue(load, 100);
#endif
    }
    if (s_cpu_count<1) s_cpu_count=1;//2.76 protection div by zero
    cpus_losd = cpus_losd/s_cpu_count;
#if defined _MACOS_
    MacCpuBar->setValue(cpus_losd);
#endif
    l_cpu0->setText(QString("%1%").arg(cpus_losd));//.arg(cpu0));
}
////////////////////////////////////////////////////////////////////////////////////////
/*
#if defined _WIN32_
SYSTEM_INFO   systeminfo;
LPFN_NtQuerySystemInformation ntquerysysteminformation;
#pragma pack(push,8)

SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION spi_old[32];
SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION spi[32];
#pragma pack(pop)
int GetProc_Info_()
{
    memset(spi_old,0,sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION)*32);
    memset(spi,0,sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION)*32);
    ntquerysysteminformation=(LPFN_NtQuerySystemInformation) GetProcAddress(GetModuleHandle(TEXT("ntdll.dll")), "NtQuerySystemInformation");

    if (!ntquerysysteminformation)
    {
        printf("\n*** no ntquerysysteminformation api?.. bugger");
        return -1;
    }
    GetSystemInfo(&systeminfo);
    return systeminfo.dwNumberOfProcessors;
}
#endif
*/
CpuWudget::CpuWudget( QWidget *parent )
        : QWidget(parent)
{
    s_cpu_c = 1;
    H_B = new QHBoxLayout(this);
    H_B->setContentsMargins(0,0,0,0);
    //H_B->setAlignment(Qt::AlignRight);
    H_B->setSpacing(0);
    this->setLayout(H_B);

    ScanForCpus();

    timer_refreshcpu = new QTimer();
    connect(timer_refreshcpu, SIGNAL(timeout()), this, SIGNAL(Refresh()));
#if defined _WIN32_
    timer_refreshcpu->start(500);
#endif
#if defined _LINUX_
#if defined _FREEBSDHV_
    //no timer needed
#else
    timer_refreshcpu->start(1000);
#endif
#endif
#if defined _MACOS_
    timer_refreshcpu->start(1000);
#endif
}
CpuWudget::~CpuWudget()
{}
/*void CpuWudget::AddWidgets(int number)
{
    CpuWudget_W *TCpuWudget_W = new CpuWudget_W(number,0,false);
    H_B->addWidget(TCpuWudget_W);
    connect(this, SIGNAL(Refresh()), TCpuWudget_W, SLOT(SetValue()));
}*/
#if defined _FREEBSDHV_
#include <QProcess>
#endif
#if defined _MACOS_
#include <QThread>
#endif
void CpuWudget::ScanForCpus()
{
#if defined _LINUX_
#if defined _FREEBSDHV_
    QProcess cmd;
    cmd.setProcessChannelMode(QProcess::MergedChannels);
    cmd.start("sysctl",QStringList() << "hw.ncpu");
    if (!cmd.waitForFinished()) return;
    QByteArray ba = cmd.readAll(); //hw.ncpu: 4
    QString res = QString(ba.data());
    res.remove("hw.ncpu:");
    //printf("----%d----\n",res.toInt());
    s_cpu_c = res.toInt();
#else
    QDir tempdir = QDir("/sys/devices/system/cpu/");
    if (!tempdir.exists())
    {
        QMessageBox::warning(this, "MSHV",
                             "No cpu info from /sys/devices/system/cpu/",
                             QMessageBox::Close);
        return;
    }

    tempdir.setSorting(QDir::Name);
    QStringList filetype;
    filetype <<"cpu*";
    QStringList Files = tempdir.entryList(filetype, QDir::Dirs); /// | QDir::NoSymLinks

    if (Files.isEmpty())
    {
        QMessageBox::warning(this, "MSHV",
                             "No cpu info from /sys/devices/system/cpu/cpu??",
                             QMessageBox::Close);
        return;
    }
    int cpu_count = 0;
    for (QStringList::iterator it =  Files.begin(); it != Files.end(); it++)
    {
        QRegExp rx;
        rx.setPattern("cpu(\\d+)");
        if (rx.indexIn(*it) != -1) cpu_count++;
    }
    /*if (cpu_count <= MAX_H_DISPLY_CPU)
    {
    for (int i = 0; i < cpu_count; i++) AddWidgets(i);
    }
    else*/
    //{
    CpuWudget_W *TCpuWudget_W = new CpuWudget_W(0,cpu_count/*,true*/);
    H_B->addWidget(TCpuWudget_W);
    connect(this, SIGNAL(Refresh()), TCpuWudget_W, SLOT(SetValueAllInOne()));
    //}
    s_cpu_c = cpu_count;
#endif
#endif
#if defined _MACOS_
    {
        // macOS: ask sysctl how many logical CPUs we have, so the widget
        // displays a single all-in-one bar with the right scale.
        int cpu_count = QThread::idealThreadCount();
        if (cpu_count < 1) cpu_count = 1;
        CpuWudget_W *TCpuWudget_W = new CpuWudget_W(0, cpu_count);
        H_B->addWidget(TCpuWudget_W);
        connect(this, SIGNAL(Refresh()), TCpuWudget_W, SLOT(SetValueAllInOne()));
        s_cpu_c = cpu_count;
    }
#endif
#if defined _WIN32_
    SYSTEM_INFO   systeminfo;
    LPFN_NtQuerySystemInformation ntquerysysteminformation;
    SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION spi_old[MAX_PROC];
    SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION spi[MAX_PROC];
    int cpu_count = GetProc_Info_(systeminfo,ntquerysysteminformation,spi_old,spi);//2.58 GetProc_Info_();
    if (cpu_count<=0)
    {
        QMessageBox::warning(this, "MSHV",
                             "No cpu info from ntdll.dll",
                             QMessageBox::Close);
    }
    else
    {
        /*if (cpu_count <= MAX_H_DISPLY_CPU)
        {
            for (int i = 0; i < cpu_count; i++) AddWidgets(i);
        }
        else*/
        //{
        CpuWudget_W *TCpuWudget_W = new CpuWudget_W(0,cpu_count/*,true*/);
        H_B->addWidget(TCpuWudget_W);
        connect(this, SIGNAL(Refresh()), TCpuWudget_W, SLOT(SetValueAllInOne()));
        //}
    }
    s_cpu_c = cpu_count;
#endif
}




