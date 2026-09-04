/* MSHV
 * By Hrisimir Hristov - LZ2HV
 * May be used under the terms of the GNU General Public License (GPL)
 */
/*
 * MSHV Native FlexRadio VITA-49 audio backend.
 *
 * Adds a third receive/transmit audio source alongside the two MSHV already
 * has, using the same ingress seam LZ2HV built for TCI:
 *
 *   Soundcard : SoundInput  -> mscore
 *   TCI       : TCI network audio -> _SetRxAudioTci_ -> mscore
 *   Flex      : SmartSDR TCP + DAX/VITA-49 UDP -> _SetRxAudioTci_ -> mscore
 *
 * No SmartSDR, no DAX virtual audio device and no third-party library are
 * involved -- this speaks the radio's own protocol directly.
 *
 * Measured against a FLEX-6600: the DAX RX stream is 24 kHz float32
 * big-endian, stereo-interleaved with L == R.  De-interleaved it is exactly
 * one of MSHV's native input rates, so it is handed to the decoder with
 * k_res = 1 (24000) and MSHV's own ResampleAndFilter() takes it to 12 kHz.
 * No resampling is done here.
 *
 * Nothing in this file runs, and no socket is opened, unless the operator
 * selects the Flex Native audio device.
 */

#ifndef FLEXVITA_H
#define FLEXVITA_H

#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QStringList>

class QTcpSocket;
class QUdpSocket;
class QTimer;

// Audio ingress seam, shared with the TCI client (mscore.cpp).
extern void _SetRxAudioTci_(int *, int, int);
extern int  _GetRxAudioReadTci_();

// Hooks used by the rest of MSHV.  All are safe to call when Flex is idle.
extern bool _FlexVitaRxActive_();
extern bool _FlexVitaTxActive_();
extern bool _SetTxAudioFlex_(int *raw);
extern void _FlexVitaSetPtt_(bool on);
extern void _FlexVitaStart_(QString host, int dax_channel, bool want_tx);
extern void _FlexVitaStop_();
extern QString _FlexVitaStatus_();
// Live TX metering off the radio. Returns false until the radio has sent a
// meter packet; values are forward/reflected watts and SWR.
extern bool _FlexVitaMeters_(double *fwd_w, double *ref_w, double *swr);
// Any meter by name (ALC, PATEMP, +13.8A, LEVEL, ...). Value is already
// scaled raw/128; the unit depends on the meter. False if not seen yet.
extern bool _FlexVitaMeterByName_(const char *name, double *value);
// Antenna / mode, mirrored from the radio so the UI offers only what it
// accepts. Empty list means the backend has not learned them yet.
extern QStringList _FlexVitaAntList_(bool tx);
extern QString     _FlexVitaAnt_(bool tx);
extern void        _FlexVitaSetAnt_(bool tx, QString ant);
extern QStringList _FlexVitaModeList_();
extern QString     _FlexVitaMode_();
extern void        _FlexVitaSetMode_(QString mode);

class FlexVita : public QObject
{
    Q_OBJECT
public:
    // Proven against a FLEX-6600.  See flexvita.cpp for the derivation.
    static const int RadioAudioRate  = 24000; // DAX RX/TX sample rate
    static const int FramesPerPacket = 128;   // 256 floats, L+R interleaved
    static const int VitaUdpPort     = 4991;  // radio's inbound VITA-49 port
    static const int ApiTcpPort      = 4992;  // SmartSDR command API

    explicit FlexVita(QObject *parent = 0);
    virtual ~FlexVita();

    bool Start(QString host, int dax_channel, bool want_tx);
    void Stop();

    bool RxActive() const { return rx_active_; }
    bool TxActive() const { return tx_active_; }
    QString Status() const { return status_; }

    void SetPtt(bool on);
    void SetSliceProperty(QString key, QString value);
    bool PushTxBlock(int *raw); // 4096 ints, stereo 48 kHz, 24-bit scale

private slots:
    void ReadControl();
    void ReadAudio();
    void SendTxPacket();
    void ControlError();
    void RetryConnect();

private:
    QString Command(QString text, int wait_ms = 1500);
    void    HandleLine(QString line);
    bool    ParseVita(const char *data, int len,
                      int *payload_offset, int *payload_bytes);
    int     FindOwnedSlice() const;
    void    Fail(QString why);

    QTcpSocket *control_;
    QUdpSocket *audio_;
    QTimer     *tx_timer_;

    QHostAddress radio_addr_;
    QString      host_;
    QByteArray   control_buf_;
    QStringList  log_;
    QString      status_;

    int      seq_;
    quint32  client_handle_;
    bool     have_handle_;
    QString  gui_client_id_;

    int      meter_fwd_id_;   // learned from the radio's meter definitions
    int      meter_ref_id_;
    int      meter_swr_id_;

    int      dax_channel_;
    int      slice_id_;
    bool     slice_created_;   // only ever remove a slice we made ourselves
    quint32  rx_stream_;
    quint32  tx_stream_;

    bool     rx_active_;
    bool     tx_active_;
    bool     want_tx_;        // requested TX mode, for reconnect
    bool     retry_pending_;
    bool     ptt_;

    quint8   tx_packet_count_;
    quint64  tx_sample_clock_;  // TSF fractional timestamp, in samples
    int      rx_scratch_[8192];
};

#endif
