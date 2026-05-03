/* MSHV CpuSensor
 * Copyright 2015 Hrisimir Hristov, LZ2HV
 * May be used under the terms of the GNU General Public License (GPL)
 */
#include "cpusensorhv.h"
//#include <QtGui>

#if defined _MACOS_
#include <mach/mach.h>
#include <mach/processor_info.h>
#include <mach/mach_host.h>
#endif

#if defined _WIN32_
int GetProc_Info_(SYSTEM_INFO &sy,LPFN_NtQuerySystemInformation &nt,SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION *xspi_old,SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION *xspi)
{
    memset(xspi_old,0,sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION)*MAX_PROC);
    memset(xspi,0,sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION)*MAX_PROC);
    nt=(LPFN_NtQuerySystemInformation) GetProcAddress(GetModuleHandle(TEXT("ntdll.dll")),"NtQuerySystemInformation");
    if (!nt)
    {
        //printf("\n*** no ntquerysysteminformation api?.. bugger");
        return 0;
    }
    GetSystemInfo(&sy); 
    if (sy.dwNumberOfProcessors > MAX_PROC) sy.dwNumberOfProcessors = MAX_PROC; //2.58 max=32 protection  	
    return sy.dwNumberOfProcessors;	
}
#endif

CpuSensorHv::CpuSensorHv(QString cpuN, int cpu_number)
{
    cpu_num = cpu_number; //qDebug()<<cpu_num<<cpuN;
#if defined _WIN32_
    bytesreturned=0;//2.12
    cpu_usage = 0;
    f_function = false;
    /*memset(spi_old,0,sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION)*32);
    memset(spi,0,sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION)*32);
    ntquerysysteminformation=(LPFN_NtQuerySystemInformation) GetProcAddress(GetModuleHandle(TEXT("ntdll.dll")), "NtQuerySystemInformation");
    if (!ntquerysysteminformation)
    {
        printf("\n*** no ntquerysysteminformation api?.. bugger");
        return;
    }
    GetSystemInfo(&systeminfo); //return systeminfo.dwNumberOfProcessors;*/
    GetProc_Info_(systeminfo,ntquerysysteminformation,spi_old,spi);//2.58
       
#endif
#if defined _LINUX_
    userTicks=0;//2.12
    sysTicks=0;
    niceTicks=0;
    idleTicks=0;
    user=0;
    system=0;
    nice=0;
    idle=0;//2.12
#endif
#if defined _MACOS_
    mac_userTicks = 0;
    mac_sysTicks  = 0;
    mac_niceTicks = 0;
    mac_idleTicks = 0;
#endif
	cpuNbr = cpuN;
}

CpuSensorHv::~CpuSensorHv()
{}
#if defined _LINUX_
void CpuSensorHv::getTicks (long &u,long &s,long &n,long &i)//
{

    QFile file("/proc/stat");
    QString line;

    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QTextStream t( &file );        // use a text stream
        QRegExp rx( cpuNbr+"\\s+(\\d+)\\s+(\\d+)\\s+(\\d+)\\s+(\\d+)");
        line = t.readLine();
        //line = rx.cap(1);

        while ( (line = t.readLine()) !=0 && rx.cap(0) == "" )
        {
            rx.indexIn(line);
        }
        //user
        u = rx.cap(1).toLong();
        //nice
        n = rx.cap(2).toLong();
        //system
        s = rx.cap(3).toLong();
        //idle
        i = rx.cap(4).toLong();
        file.close();
    }
    else
    {
        u = 0;
        s = 0;
        n = 0;
        i = 0;
    }
    //qDebug()<<"u"<<u;
    //qDebug()<<"s"<<s;
    //qDebug()<<"n"<<n;
    //qDebug()<<"i"<<i;
}
#endif
int CpuSensorHv::getCPULoad()
{
#if defined _MACOS_
    // Per-CPU usage via host_processor_info() — the macOS analogue of
    // reading /proc/stat. Returns one processor_cpu_load_info_data_t per
    // logical CPU, with cumulative tick counters for user/system/nice/idle.
    natural_t cpu_count = 0;
    processor_info_array_t info_array = NULL;
    mach_msg_type_number_t info_count = 0;

    kern_return_t kr = host_processor_info(mach_host_self(),
                                           PROCESSOR_CPU_LOAD_INFO,
                                           &cpu_count,
                                           &info_array,
                                           &info_count);
    if (kr != KERN_SUCCESS || info_array == NULL) return 0;

    int load = 0;
    if (cpu_num >= 0 && cpu_num < (int)cpu_count)
    {
        processor_cpu_load_info_t load_info =
            (processor_cpu_load_info_t)info_array;
        unsigned long u = load_info[cpu_num].cpu_ticks[CPU_STATE_USER];
        unsigned long s = load_info[cpu_num].cpu_ticks[CPU_STATE_SYSTEM];
        unsigned long n = load_info[cpu_num].cpu_ticks[CPU_STATE_NICE];
        unsigned long i = load_info[cpu_num].cpu_ticks[CPU_STATE_IDLE];

        unsigned long du = u - mac_userTicks;
        unsigned long ds = s - mac_sysTicks;
        unsigned long dn = n - mac_niceTicks;
        unsigned long di = i - mac_idleTicks;
        unsigned long total = du + ds + dn + di;
        if (total > 0)
            load = (int)((100.0 * (du + ds + dn)) / (double)total + 0.5);

        mac_userTicks = u;
        mac_sysTicks  = s;
        mac_niceTicks = n;
        mac_idleTicks = i;
    }

    // host_processor_info allocates the array via vm_allocate; release it
    // back to the kernel so we don't leak per refresh.
    vm_deallocate(mach_task_self(),
                  (vm_address_t)info_array,
                  info_count * sizeof(integer_t));
    return load;
#endif
#if defined _LINUX_
    //QString str;
    long uTicks, sTicks, nTicks, iTicks;
    //cpuNbr = cpu;


    getTicks(uTicks, sTicks, nTicks, iTicks);

    const long totalTicks = ((uTicks - userTicks) +
                             (sTicks - sysTicks) +
                             (nTicks - niceTicks) +
                             (iTicks - idleTicks));

    int load  = (totalTicks == 0) ? 0 : (int) ( 100.0 * ( (uTicks+sTicks+nTicks) - (userTicks+sysTicks+niceTicks))/( totalTicks+0.001) + 0.5 );
    user = (totalTicks == 0) ? 0 : (int) ( 100.0 * ( uTicks - userTicks)/( totalTicks+0.001) + 0.5 );
    idle = (totalTicks == 0) ? 0 : (int) ( 100.0 * ( iTicks - idleTicks)/( totalTicks+0.001) + 0.5 );
    system = (totalTicks == 0) ? 0 : (int) ( 100.0 * ( sTicks - sysTicks)/( totalTicks+0.001) + 0.5 );
    nice = (totalTicks == 0) ? 0 : (int) ( 100.0 * ( nTicks - niceTicks)/( totalTicks+0.001) + 0.5 );

    userTicks = uTicks;
    sysTicks = sTicks;
    niceTicks = nTicks;
    idleTicks = iTicks;
    //qDebug()<<"SSSSSSSSSSSSs"<<load;
    return load;
#endif
#if defined _WIN32_
    if (!f_function)
    {
        ntquerysysteminformation(SystemProcessorPerformanceInformation,spi_old,(sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION)*systeminfo.dwNumberOfProcessors),&bytesreturned);
        f_function = true; //qDebug()<<"11count_function";
	}
    else
    {
        ntquerysysteminformation(SystemProcessorPerformanceInformation,spi,(sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION)*systeminfo.dwNumberOfProcessors),&bytesreturned);       
        //2.76 div by zero
        long long int t00 = ((spi[cpu_num].KernelTime.QuadPart +  spi[cpu_num].UserTime.QuadPart) - (spi_old[cpu_num].KernelTime.QuadPart + spi_old[cpu_num].UserTime.QuadPart));
        //qDebug()<<t00;
        if (t00<1) t00=1;//2.76 div by zero
        BYTE cpuusage = (BYTE) (100 - (((spi[cpu_num].IdleTime.QuadPart - spi_old[cpu_num].IdleTime.QuadPart) * 100) / t00));
        //end 2.76 div by zero         
        /*old 2.76 div by zero 
        BYTE cpuusage = (BYTE) (100 - (((spi[cpu_num].IdleTime.QuadPart - spi_old[cpu_num].IdleTime.QuadPart) * 100) /  \
                                       ((spi[cpu_num].KernelTime.QuadPart +  spi[cpu_num].UserTime.QuadPart) - (spi_old[cpu_num].KernelTime.QuadPart + spi_old[cpu_num].UserTime.QuadPart))));
        end old 2.76 div by zero*/                                             
        cpu_usage = cpuusage;
        f_function = false; //qDebug()<<"22count_function";
    } 
    return cpu_usage;
#endif
}






