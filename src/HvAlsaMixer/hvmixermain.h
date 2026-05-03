#ifndef HVMIXERMAIN_H
#define HVMIXERMAIN_H


#include "../config.h"

#if defined _LINUX_
#include "hvalsamixer.h"
#include <QWidget>
#include <QTabWidget>
#include <QMessageBox>

#include <QDesktopWidget>
#include <QApplication>

//#include <QtGui>

class HvMixerMain : public QWidget
{
    Q_OBJECT
public:
    HvMixerMain(QWidget * parent = 0);
    virtual ~HvMixerMain();

signals:
    void CloseHandles();

public slots:
    void Start();

private:
    void ScanForAlsaDev();
    bool g_block_alsa;
    QStringList dev_names;
    QStringList card_names;
    void AddTabs();
    QTabWidget *TabCards;
    int size_w, size_h;

    void closeEvent(QCloseEvent*)
    {
    	//Spriano HV kogato e vgradeno v app i to pleyva sira zvuka i garmi  res = system("alsactl store");
        //int res;
        //res = system("alsactl store");// zapomnia mixera pri sledva6t start
        //if ( res != 0 )
            //fprintf(stderr, "GCC 4.4.0 Error command -> system(alsactl store) in hvmixermain.h line 41...\n");
        TabCards->clear();
        emit CloseHandles();
        disconnect(this, SIGNAL(CloseHandles()), 0, 0);
    }

};
#endif

#if defined _MACOS_
// macOS has no ALSA. Provide an empty stub so main_ms.cpp compiles unchanged.
// Triggering the menu item just opens System Settings ▸ Sound.
#include <QWidget>
#include <QDesktopServices>
#include <QUrl>

class HvMixerMain : public QWidget
{
    Q_OBJECT
public:
    HvMixerMain(QWidget * parent = 0) : QWidget(parent) { Q_UNUSED(parent); }
    virtual ~HvMixerMain() {}

signals:
    void CloseHandles();

public slots:
    void Start()
    {
        // Open System Settings → Sound on macOS in lieu of an in-app mixer.
        QDesktopServices::openUrl(QUrl("x-apple.systempreferences:com.apple.Sound-Settings.extension"));
    }
};
#endif
#endif


