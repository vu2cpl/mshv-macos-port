/* MSHV
 * By Hrisimir Hristov - LZ2HV
 * May be used under the terms of the GNU General Public License (GPL)
 */
/*
 * MSHV Native FlexRadio VITA-49 audio backend.
 *
 * PROTOCOL NOTES (measured against a FLEX-6600, not taken on trust)
 *
 * Control is the SmartSDR TCP API on 4992, commands framed as
 * "C<seq>|<text>\n", replies as "R<seq>|<hexcode>|<message>".  On connect the
 * radio sends "V<version>" and "H<handle>"; the H line is mandatory because
 * it identifies ownership of everything created afterwards.
 *
 * A bare API client owns no slice and therefore sees NO slice status lines at
 * all, even after "sub slice all" -- slices belong to GUI clients.  Receive
 * does not care, but transmit does: without slice ownership the radio reports
 * tx_client_handle=0x00000000, state=RECEIVE, tx_allowed=0 with an empty
 * reason, and nothing can key.  "client gui" is what opens that door.
 *
 * The DAX RX audio stream is 24 kHz float32 BIG ENDIAN, stereo-interleaved
 * with L == R (verified: even and odd floats are bit-identical, correlation
 * 1.000000).  It is NOT 48 kHz mono, despite carrying ~48000 floats/second.
 * Taking every second float yields exactly 24 kHz, which is one of MSHV's
 * native input rates, so it goes to the decoder with k_res = 1 and no
 * resampling happens here.
 *
 * Only slices this backend CREATED are removed on shutdown.  "client gui" can
 * hand us ownership of a pre-existing slice, and removing that would silently
 * kill whatever else was using it.
 */

#include "flexvita.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QRegExp>
#include <QStringList>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>

#include <QThread>

#include <math.h>
#include <string.h>

// Which slice the operator selected in Rig Control ("FlexRadio SmartSDR
// Slice A..H TCP"), 0..7, or -1 for any other rig.  Defined in
// hvrigcontrol.cpp beside _GetFlexNativeHost_(), so the radio address and the
// slice both come from the one place the operator configures them.
extern int _GetFlexNativeSlice_();

// ---------------------------------------------------------------- TX buffer
//
// Mirrors the ring the TCI client uses, kept separate so that neither
// transport can disturb the other.  Samples arrive from Rawplayer as
// stereo-interleaved 48 kHz ints at 24-bit scale (full = 8388607); Flex wants
// 24 kHz mono, so the read stride is 4 (2 channels x 2 for 48k -> 24k).
#define FLEX_STREAM_C   4096
#define FLEX_BUF_MAX    (50 * FLEX_STREAM_C)
#define FLEX_TX_STRIDE  4

static int  flex_txbuff[FLEX_BUF_MAX + FLEX_STREAM_C * 4 + 4096];
static int  flex_txbuff_pos = 0;
static int  flex_tx_pos     = 0;
static int  flex_tx_primed  = 0;

static FlexVita *g_flex = 0;

// ------------------------------------------------------------------- meters
//
// The radio streams FWDPWR / REFPWR / SWR as VITA packets on the same UDP
// socket as the audio, distinguished by packet class 0x534C8002.  Payload is
// a run of (uint16 meter_id, int16 raw) pairs.  Meter IDs are NOT fixed across
// radios or firmware, so they are learned from the "meter <id>.nam=" status
// lines rather than hardcoded.
#define FLEX_METER_CLASS 0x534C8002u

static double flex_fwd_watts = 0.0;
static double flex_ref_watts = 0.0;
static double flex_swr       = 0.0;
static bool   flex_meters_ok = false;

// Full meter store. The radio publishes ~48 meters and DUPLICATES names
// across instances (one set per slice, one per transmitter) -- e.g. ALC at
// both 19 and 41, LEVEL at 13 and 31. Lookup therefore takes the lowest id
// matching the name, which is deterministic and correct for a single-slice
// setup; do not assume a name maps to exactly one meter.
struct FlexMeterDef { QString nam, unit, src; };
static QHash<int, FlexMeterDef> flex_meter_defs;
static QHash<int, double>       flex_meter_raw;   // raw/128 already applied

bool _FlexVitaMeterByName_(const char *name, double *out)
{
    const QString want = QString(name).toUpper();
    int best = -1;
    for (QHash<int, FlexMeterDef>::const_iterator it = flex_meter_defs.constBegin();
         it != flex_meter_defs.constEnd(); ++it)
    {
        if (it.value().nam.toUpper() != want) continue;
        if (best < 0 || it.key() < best) best = it.key();
    }
    if (best < 0 || !flex_meter_raw.contains(best)) return false;
    if (out) *out = flex_meter_raw.value(best);
    return true;
}

bool _FlexVitaMeters_(double *fwd_w, double *ref_w, double *swr)
{
    if (!flex_meters_ok) return false;
    if (fwd_w) *fwd_w = flex_fwd_watts;
    if (ref_w) *ref_w = flex_ref_watts;
    if (swr)   *swr   = flex_swr;
    return true;
}

// ---------------------------------------------------------------- diagnostics
//
// Counters for the TX path.  A Finder-launched bundle has no visible stderr
// (see CLAUDE.md F12), so this also lands in a file next to the settings.
static unsigned long flex_push_calls  = 0;   // Rawplayer -> PushTxBlock
static unsigned long flex_push_ints   = 0;
static unsigned long flex_pkts_sent   = 0;   // VITA packets -> radio
static unsigned long flex_send_ticks  = 0;   // timer fires
static unsigned long flex_rx_pkts     = 0;
static long          flex_last_peak   = 0;   // |sample| seen while keying

static void FlexDiag(const char *what)
{
    QString path = QDir::homePath()
        + "/Library/Application Support/MSHV/flexvita_tx.log";
    QFile f(path);
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    QTextStream(&f)
        << QDateTime::currentDateTime().toString("hh:mm:ss.zzz")
        << "  " << what
        << "  push_calls=" << (qulonglong)flex_push_calls
        << " push_ints=" << (qulonglong)flex_push_ints
        << " ticks=" << (qulonglong)flex_send_ticks
        << " pkts_sent=" << (qulonglong)flex_pkts_sent
        << " tx_peak=" << (qlonglong)flex_last_peak
        << " rx_pkts=" << (qulonglong)flex_rx_pkts
        << "\n";
    f.close();
}

static void FlexTxBufferReset()
{
    for (int i = 0; i < FLEX_BUF_MAX; ++i) flex_txbuff[i] = 0;
    flex_txbuff_pos = 0;
    flex_tx_pos     = 0;
    flex_tx_primed  = 0;
}

// ------------------------------------------------------------------- hooks
bool _FlexVitaRxActive_()
{
    return (g_flex && g_flex->RxActive());
}
bool _FlexVitaTxActive_()
{
    return (g_flex && g_flex->TxActive());
}
bool _SetTxAudioFlex_(int *raw)
{
    if (!g_flex) return true;
    return g_flex->PushTxBlock(raw);
}
void _FlexVitaSetPtt_(bool on)
{
    if (g_flex) g_flex->SetPtt(on);
}
void _FlexVitaStart_(QString host, int dax_channel, bool want_tx)
{
    if (!g_flex) g_flex = new FlexVita(QCoreApplication::instance());
    g_flex->Start(host, dax_channel, want_tx);
}
void _FlexVitaStop_()
{
    if (g_flex) g_flex->Stop();
}
// Slice properties mirrored from the radio's status stream, so the UI can be
// populated from what the radio actually offers rather than a hardcoded list.
static QString     flex_rxant, flex_txant, flex_mode;
static QStringList flex_ant_list, flex_tx_ant_list, flex_mode_list;

QStringList _FlexVitaAntList_(bool tx) { return tx ? flex_tx_ant_list : flex_ant_list; }
QString     _FlexVitaAnt_(bool tx)     { return tx ? flex_txant : flex_rxant; }
QStringList _FlexVitaModeList_()       { return flex_mode_list; }
QString     _FlexVitaMode_()           { return flex_mode; }

void _FlexVitaSetAnt_(bool tx, QString ant)
{
    if (g_flex) g_flex->SetSliceProperty(tx ? "txant" : "rxant", ant);
}
void _FlexVitaSetMode_(QString mode)
{
    if (g_flex) g_flex->SetSliceProperty("mode", mode);
}

QString _FlexVitaStatus_()
{
    return g_flex ? g_flex->Status() : QString("Flex Native: idle");
}

// ------------------------------------------------------------------- helpers
static inline quint32 ReadBE32(const char *p)
{
    const unsigned char *u = (const unsigned char *)p;
    return ((quint32)u[0] << 24) | ((quint32)u[1] << 16)
           | ((quint32)u[2] << 8) | (quint32)u[3];
}

static inline float ReadBEFloat(const char *p)
{
    union { quint32 i; float f; } u;
    u.i = ReadBE32(p);
    return u.f;
}

static inline void WriteBE32(char *p, quint32 v)
{
    p[0] = (char)((v >> 24) & 0xff);
    p[1] = (char)((v >> 16) & 0xff);
    p[2] = (char)((v >>  8) & 0xff);
    p[3] = (char)( v        & 0xff);
}

static inline void WriteBEFloat(char *p, float f)
{
    union { quint32 i; float f; } u;
    u.f = f;
    WriteBE32(p, u.i);
}

// ------------------------------------------------------------------- class
FlexVita::FlexVita(QObject *parent)
    : QObject(parent)
{
    control_       = new QTcpSocket(this);
    audio_         = new QUdpSocket(this);
    tx_timer_      = new QTimer(this);
    seq_           = 1;
    client_handle_ = 0;
    have_handle_   = false;
    meter_fwd_id_  = -1;
    meter_ref_id_  = -1;
    meter_swr_id_  = -1;
    dax_channel_   = 1;
    slice_id_      = -1;
    slice_created_ = false;
    slice_mismatch_.clear();
    rx_stream_     = 0;
    tx_stream_     = 0;
    rx_active_     = false;
    tx_active_     = false;
    want_tx_       = false;
    retry_pending_ = false;
    ptt_           = false;
    tx_packet_count_ = 0;
    tx_sample_clock_ = 0;
    status_        = "Flex Native: idle";

    connect(control_, SIGNAL(readyRead()), this, SLOT(ReadControl()));
    connect(control_, SIGNAL(disconnected()), this, SLOT(ControlError()));
    connect(audio_, SIGNAL(readyRead()), this, SLOT(ReadAudio()));
    connect(tx_timer_, SIGNAL(timeout()), this, SLOT(SendTxPacket()));
}

FlexVita::~FlexVita()
{
    Stop();
}

void FlexVita::Fail(QString why)
{
    status_ = "Flex Native: " + why;
    Stop();
}

void FlexVita::HandleLine(QString line)
{
    line = line.trimmed();
    if (line.isEmpty()) return;
    log_.append(line);
    if (log_.size() > 400) log_.removeFirst();

    const QChar tag = line.at(0);
    if (tag == 'H')
    {
        bool ok = false;
        const quint32 h = line.mid(1).toUInt(&ok, 16);
        if (ok) { client_handle_ = h; have_handle_ = true; }
    }
    else if (tag == 'S')
    {
        // Status stream.  Capture the DAX RX stream id the radio assigns; the
        // create response does not always carry it.
        // Mirror the owned slice's antenna / mode state and the lists of what
        // the radio will accept, so the UI never hardcodes them.
        if (slice_id_ >= 0 && line.contains(QString("|slice %1 ").arg(slice_id_)))
        {
            QRegExp kv("\\b(rxant|txant|mode|ant_list|tx_ant_list|mode_list)=([^\\s]+)");
            int at = 0;
            while ((at = kv.indexIn(line, at)) >= 0)
            {
                const QString k = kv.cap(1), v = kv.cap(2);
                if      (k == "rxant")       flex_rxant       = v;
                else if (k == "txant")       flex_txant       = v;
                else if (k == "mode")        flex_mode        = v;
                else if (k == "ant_list")    flex_ant_list    = v.split(',', QString::SkipEmptyParts);
                else if (k == "tx_ant_list") flex_tx_ant_list = v.split(',', QString::SkipEmptyParts);
                else if (k == "mode_list")   flex_mode_list   = v.split(',', QString::SkipEmptyParts);
                at += kv.matchedLength();
            }
        }

        // Meter definitions: "meter 7.src=TX-#7.num=1#7.nam=FWDPWR#..."
        if (line.contains("|meter "))
        {
            // Units decide the scale factor, so capture them too.
            QRegExp un("(\\d+)\\.unit=([^#\\s]+)");
            int ua = 0;
            while ((ua = un.indexIn(line, ua)) >= 0)
            {
                flex_meter_defs[un.cap(1).toInt()].unit = un.cap(2);
                ua += un.matchedLength();
            }

            QRegExp def("(\\d+)\\.nam=([^#\\s]+)");
            int at = 0;
            while ((at = def.indexIn(line, at)) >= 0)
            {
                const int id = def.cap(1).toInt();
                const QString nam = def.cap(2).toUpper();
                flex_meter_defs[id].nam = nam;
                if      (nam == "FWDPWR") { if (meter_fwd_id_ < 0) meter_fwd_id_ = id; }
                else if (nam == "REFPWR") { if (meter_ref_id_ < 0) meter_ref_id_ = id; }
                else if (nam == "SWR")    { if (meter_swr_id_ < 0) meter_swr_id_ = id; }
                at += def.matchedLength();
            }
        }

        if (line.contains("type=dax_rx") && rx_stream_ == 0)
        {
            QRegExp rx("\\|stream 0x([0-9A-Fa-f]+)");
            if (rx.indexIn(line) >= 0)
                rx_stream_ = rx.cap(1).toUInt(0, 16);
        }
    }
}

QString FlexVita::Command(QString text, int wait_ms)
{
    if (!control_ || control_->state() != QAbstractSocket::ConnectedState)
        return QString();

    const int seq = seq_++;
    const QString framed = "C" + QString::number(seq) + "|" + text + "\n";
    control_->write(framed.toLatin1());
    control_->flush();

    const QString want = "R" + QString::number(seq) + "|";
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < wait_ms)
    {
        if (control_->waitForReadyRead(50)) ReadControl();
        for (int i = log_.size() - 1; i >= 0; --i)
        {
            if (log_.at(i).startsWith(want))
            {
                const QStringList f = log_.at(i).mid(want.size()).split('|');
                if (f.size() >= 2 && f.at(0).toUInt(0, 16) != 0)
                    return QString(); // radio rejected the command
                return f.size() >= 2 ? f.at(1) : QString("");
            }
        }
    }
    return QString();
}

void FlexVita::ReadControl()
{
    if (!control_) return;
    control_buf_ += control_->readAll();
    int nl;
    while ((nl = control_buf_.indexOf('\n')) >= 0)
    {
        HandleLine(QString::fromLatin1(control_buf_.left(nl)));
        control_buf_.remove(0, nl + 1);
    }
}

void FlexVita::ControlError()
{
    const bool was_up = (rx_active_ || tx_active_);
    if (was_up) status_ = "Flex Native: control connection lost, retrying";
    rx_active_ = false;
    tx_active_ = false;
    tx_timer_->stop();

    // Without this the backend stayed dead for the rest of the session after a
    // single dropped control connection -- the radio simply went quiet with no
    // indication why.  Retry on a timer rather than hammering the radio.
    if (was_up && !retry_pending_ && !host_.isEmpty())
    {
        retry_pending_ = true;
        QTimer::singleShot(3000, this, SLOT(RetryConnect()));
    }
}

void FlexVita::RetryConnect()
{
    retry_pending_ = false;
    if (rx_active_ || host_.isEmpty()) return;
    if (!Start(host_, dax_channel_, want_tx_) && !retry_pending_)
    {
        retry_pending_ = true;
        QTimer::singleShot(10000, this, SLOT(RetryConnect()));
    }
}

int FlexVita::FindOwnedSlice() const
{
    const QString want = QString("client_handle=0x%1")
                             .arg(client_handle_, 8, 16, QChar('0')).toLower();
    int found = -1;
    for (int i = 0; i < log_.size(); ++i)
    {
        const QString l = log_.at(i);
        const QString low = l.toLower();
        if (!low.contains("|slice ") || !low.contains("in_use=1")) continue;
        if (!low.contains(want)) continue;
        QRegExp rx("\\|slice (\\d+)");
        if (rx.indexIn(l) >= 0) found = rx.cap(1).toInt();
    }
    return found;
}

// Does slice <n> exist on the radio at all, whoever owns it?  Slices come and
// go, and the status stream reports both states, so the LAST line wins.
bool FlexVita::SliceExists(int n) const
{
    const QString want = QString("|slice %1 ").arg(n);
    bool exists = false;
    for (int i = 0; i < log_.size(); ++i)
    {
        const QString l = log_.at(i);
        if (!l.contains(want)) continue;
        const QString low = l.toLower();
        if (low.contains("in_use=1"))      exists = true;
        else if (low.contains("in_use=0")) exists = false;
    }
    return exists;
}

// Pick the slice this session will source DAX audio from, and transmit on.
//
// It MUST be the slice the rig control tunes and reads, otherwise MSHV shows
// one frequency and works another.  Until 2026-09-05 that was masked: the
// rig control sent "slice set <n> tx=1" on every key, which dragged the
// transmitter onto its own slice (and made the radio click its relays twice
// doing it).  With that gone, the agreement has to be built in here.
//
// Order of preference:
//   1. the slice selected in Rig Control ("Slice A..H TCP"), if it exists
//   2. any slice this session already owns  -- prior behaviour
//   3. create one
//
// Step 1 deliberately accepts a slice owned by somebody else.  The rig
// control addresses that slice by number regardless of owner, so following it
// is what keeps the two halves consistent; and `slice_created_` stays false,
// so Stop() will never remove a slice it did not make.
int FlexVita::ChooseSlice(int *created)
{
    *created = 0;

    const int want = _GetFlexNativeSlice_();
    if (want >= 0 && SliceExists(want))
    {
        slice_mismatch_.clear();
        return want;
    }

    const int owned = FindOwnedSlice();
    if (owned >= 0)
    {
        // Owning a different slice than the operator configured is a real
        // misconfiguration, not a detail -- say so rather than working the
        // wrong frequency silently.
        if (want >= 0 && owned != want)
            slice_mismatch_ = QString(" [!] rig control is set to Slice %1 "
                                      "but audio is on slice %2")
                                  .arg(QChar('A' + want)).arg(owned);
        else
            slice_mismatch_.clear();
        return owned;
    }

    Command("slice create mode=digu");
    QElapsedTimer t; t.start();
    int made = -1;
    while (t.elapsed() < 4000 && made < 0)
    {
        if (control_->waitForReadyRead(100)) ReadControl();
        made = FindOwnedSlice();
    }
    if (made >= 0)
    {
        *created = 1;
        // The radio assigns the index; we do not get to ask for one.  If it
        // is not the configured slice, the operator has to point Rig Control
        // at the matching letter -- so make that visible.
        if (want >= 0 && made != want)
            slice_mismatch_ = QString(" [!] rig control is set to Slice %1 "
                                      "but audio is on slice %2")
                                  .arg(QChar('A' + want)).arg(made);
        else
            slice_mismatch_.clear();
    }
    return made;
}

bool FlexVita::Start(QString host, int dax_channel, bool want_tx)
{
    // Idempotent.  FlexDevSelectAndRestr() is reached from a mode change as
    // well as a device change, so this is called repeatedly with unchanged
    // arguments; tearing the radio session down and rebuilding it each time
    // dropped the streams (and left the backend dead if any step then failed).
    if (rx_active_
        && host == host_
        && dax_channel == dax_channel_
        && want_tx == tx_active_
        && control_
        && control_->state() == QAbstractSocket::ConnectedState)
    {
        return true;
    }

    Stop();

    host_        = host;
    dax_channel_ = dax_channel;
    want_tx_     = want_tx;
    radio_addr_  = QHostAddress(host);
    seq_         = 1;
    have_handle_ = false;
    log_.clear();
    control_buf_.clear();

    control_->connectToHost(host, ApiTcpPort);
    if (!control_->waitForConnected(3000))
    {
        Fail("cannot reach radio at " + host);
        return false;
    }

    // Handshake: the radio volunteers V<version> and H<handle>.
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 2000 && !have_handle_)
    {
        if (control_->waitForReadyRead(100)) ReadControl();
    }
    if (!have_handle_)
    {
        Fail("no client handle from radio");
        return false;
    }

    // Bind the audio socket before telling the radio where to send.
    if (!audio_->bind(QHostAddress(QHostAddress::AnyIPv4), 0))
    {
        Fail("cannot bind UDP audio socket");
        return false;
    }
    const quint16 udp_port = audio_->localPort();

    // A bare API client owns no slice and cannot even see the slice list.
    // Receive needs a slice to source DAX audio from and transmit needs to own
    // one outright, so become a GUI client either way -- otherwise a standalone
    // MSHV with no other SmartSDR software running would just hear silence.
    gui_client_id_ = Command("client gui");
    if (gui_client_id_.isEmpty())
    {
        Fail("client gui refused - radio granted no slice rights");
        return false;
    }

    Command("sub slice all");
    Command("sub audio_stream all");
    Command("sub dax all");
    Command("sub meter all");
    if (want_tx) Command("sub tx all");
    Command("client udpport " + QString::number(udp_port));

    // Drain status so FindOwnedSlice() sees the current picture.
    t.restart();
    while (t.elapsed() < 1200)
    {
        if (control_->waitForReadyRead(100)) ReadControl();
    }

    // ---- slice ----
    // Needed for receive as well as transmit: a DAX RX stream carries audio
    // only while some slice is routed to that channel.
    int made = 0;
    slice_id_ = ChooseSlice(&made);
    slice_created_ = (made != 0);
    if (slice_id_ < 0)
    {
        Fail("no slice available for DAX audio");
        return false;
    }
    Command(QString("dax audio set %1 slice=%2").arg(dax_channel_).arg(slice_id_));

    // ---- receive ----
    rx_stream_ = 0;
    const QString rx_reply = Command(
        "stream create type=dax_rx dax_channel=" + QString::number(dax_channel_));
    if (!rx_reply.isEmpty()) rx_stream_ = rx_reply.trimmed().toUInt(0, 16);
    if (rx_stream_ == 0)
    {
        t.restart();
        while (t.elapsed() < 3000 && rx_stream_ == 0)
        {
            if (control_->waitForReadyRead(100)) ReadControl();
        }
    }
    if (rx_stream_ == 0)
    {
        Fail("DAX RX stream not granted");
        return false;
    }
    rx_active_ = true;

    // ---- transmit ----
    if (want_tx)
    {
        Command(QString("slice s %1 tx=1 mode=digu").arg(slice_id_));
        const QString tx_reply = Command("stream create type=dax_tx");
        if (!tx_reply.isEmpty()) tx_stream_ = tx_reply.trimmed().toUInt(0, 16);
        Command("transmit set dax=1");
        Command(QString("dax audio set %1 slice=%2 tx=1")
                    .arg(dax_channel_).arg(slice_id_));

        if (tx_stream_ != 0)
        {
            FlexTxBufferReset();
            tx_packet_count_ = 0;
            tx_sample_clock_ = 0;
            tx_active_ = true;
            // 128 frames at 24 kHz = 5.33 ms.  The slot sends as many packets
            // as the elapsed time calls for, so coarse timer granularity
            // cannot drift the stream.
            tx_timer_->start(4);
        }
    }

    status_ = QString("Flex Native: RX ch%1%2 slice %3%4")
                  .arg(dax_channel_)
                  .arg(tx_active_ ? " + TX" : "")
                  .arg(slice_id_)
                  .arg(slice_mismatch_);

    // Record the slice decision.  A Finder-launched bundle has no visible
    // stderr (CLAUDE.md F12), and this is the one fact that decides whether
    // MSHV works the frequency it displays -- so it goes in the log next to
    // the settings, not only in a panel the operator may never open.
    FlexDiag(qPrintable(QString("START   slice=%1 rig_wants=%2 %3%4")
                            .arg(slice_id_)
                            .arg(_GetFlexNativeSlice_())
                            .arg(slice_created_ ? "created" : "adopted")
                            .arg(slice_mismatch_)));
    return true;
}

void FlexVita::Stop()
{
    if (tx_timer_) tx_timer_->stop();

    if (control_ && control_->state() == QAbstractSocket::ConnectedState)
    {
        if (ptt_) { Command("xmit 0", 500); ptt_ = false; }
        if (tx_stream_) Command(QString("stream remove 0x%1")
                                    .arg(tx_stream_, 8, 16, QChar('0')), 500);
        if (rx_stream_) Command(QString("stream remove 0x%1")
                                    .arg(rx_stream_, 8, 16, QChar('0')), 500);
        if (tx_active_) Command("transmit set dax=0", 500);
        // Only remove a slice this session created; an adopted one belongs to
        // somebody else and removing it kills their receive.
        if (slice_id_ >= 0 && slice_created_)
            Command(QString("slice remove %1").arg(slice_id_), 500);
        control_->disconnectFromHost();
    }
    if (control_) control_->abort();
    if (audio_) audio_->close();

    rx_active_     = false;
    tx_active_     = false;
    rx_stream_     = 0;
    tx_stream_     = 0;
    slice_id_      = -1;
    slice_created_ = false;
    slice_mismatch_.clear();
    ptt_           = false;
}

bool FlexVita::ParseVita(const char *data, int len,
                         int *payload_offset, int *payload_bytes)
{
    if (len < 8) return false;

    const quint32 header = ReadBE32(data);
    const int  ptype     = (header >> 28) & 0x0F;
    const bool class_id  = (header & 0x08000000) != 0;
    const bool trailer   = (header & 0x04000000) != 0;
    const int  tsi       = (header >> 22) & 0x03;
    const int  tsf       = (header >> 20) & 0x03;
    const int  words     =  header & 0xFFFF;

    const int packet_bytes = words * 4;
    if (packet_bytes <= 0 || packet_bytes > len) return false;
    if (ptype != 1 && ptype != 3) return false;   // IF data, with stream ID

    int idx = 8;                    // header word + stream ID
    if (class_id) idx += 8;
    if (tsi)      idx += 4;
    if (tsf)      idx += 8;         // fractional timestamp is 64-bit
    if (idx > packet_bytes) return false;

    int end = packet_bytes;
    if (trailer) end -= 4;
    if (end <= idx) return false;

    *payload_offset = idx;
    *payload_bytes  = end - idx;
    return true;
}

void FlexVita::ReadAudio()
{
    static char buf[65536];
    while (audio_ && audio_->hasPendingDatagrams())
    {
        const qint64 n = audio_->readDatagram(buf, sizeof(buf));
        if (n < 8) continue;

        // Meters share this socket with the audio but are INDEPENDENT of it.
        // They must be decoded before every audio-path guard below -- in
        // particular _GetRxAudioReadTci_(), which returns 0 whenever the
        // decoder is not consuming, and would otherwise silently drop all
        // metering exactly when the operator is watching it.
        if (n >= 16 && ReadBE32(buf + 12) == FLEX_METER_CLASS)
        {
            int off = 0, bytes = 0;
            if (ParseVita(buf, (int)n, &off, &bytes))
            {
                for (int i = off; i + 4 <= off + bytes; i += 4)
                {
                    const int id  = (int)((quint8)buf[i] << 8 | (quint8)buf[i + 1]);
                    const qint16 raw =
                        (qint16)((quint8)buf[i + 2] << 8 | (quint8)buf[i + 3]);
                    // Scale depends on the UNIT, not a single divisor.
                    // Verified against the radio's own low/hi bounds:
                    //   dBm / dBFS / SWR -> /128   (SWR raw 128 = 1.00)
                    //   Volts / Amps     -> /256   (raw 3119 = 12.18 V, range 10.5-15)
                    //   degC             -> /64    (raw 2643 = 41.3 C, range 0-120)
                    //   RPM              -> /1     (raw 1008 = 1008 rpm)
                    // Using /128 throughout put volts at 24 V and the PA at
                    // half its real temperature.
                    const QString u = flex_meter_defs.value(id).unit;
                    double div = 128.0;
                    if      (u == "Volts" || u == "Amps") div = 256.0;
                    else if (u == "degC")                 div = 64.0;
                    else if (u == "RPM")                  div = 1.0;
                    flex_meter_raw[id] = raw / div;
                    // dBm meters are raw/128; SWR is raw/128 in SWR units.
                    if (id == meter_fwd_id_)
                    {
                        const double dbm = raw / 128.0;
                        flex_fwd_watts = (dbm > 0.0) ? pow(10.0, (dbm - 30.0) / 10.0) : 0.0;
                        flex_meters_ok = true;
                    }
                    else if (id == meter_ref_id_)
                    {
                        const double dbm = raw / 128.0;
                        flex_ref_watts = (dbm > 0.0) ? pow(10.0, (dbm - 30.0) / 10.0) : 0.0;
                    }
                    else if (id == meter_swr_id_)
                    {
                        flex_swr = raw / 128.0;
                    }
                }
            }
            continue;
        }

        if (!rx_active_) continue;
        if (_GetRxAudioReadTci_() == 0) continue;   // decoder not reading yet
        if (ReadBE32(buf + 4) != rx_stream_) continue;

        int off = 0, bytes = 0;
        if (!ParseVita(buf, (int)n, &off, &bytes)) continue;

        // Stereo-interleaved with L == R: take every second float to get the
        // true 24 kHz mono stream, scaled to MSHV's 24-bit convention.
        const int floats = bytes / 4;
        int mono = 0;
        for (int i = 0; i + 1 < floats && mono < 8192; i += 2)
        {
            const float f = ReadBEFloat(buf + off + i * 4);
            rx_scratch_[mono++] = (int)(f * 8388607.0f);
        }
        if (mono > 0) { flex_rx_pkts++; _SetRxAudioTci_(rx_scratch_, mono, 1); } // k_res 1 = 24000
    }
}

bool FlexVita::PushTxBlock(int *raw)
{
    if (!tx_active_) return true;

    // Block until the sender has consumed enough that we will not overrun,
    // mirroring the flow control the TCI path uses.
    int guard = 900;                       // ~4.5 s ceiling
    while (guard-- > 0)
    {
        int pending = flex_txbuff_pos - flex_tx_pos;
        if (pending < 0) pending += FLEX_BUF_MAX;
        if (pending < FLEX_BUF_MAX - FLEX_STREAM_C * 2) break;
        if (!tx_active_) return true;
        QCoreApplication::processEvents();
        QThread::usleep(5000); // portable: no unistd.h, so Windows builds too
    }

    flex_push_calls++;
    flex_push_ints += FLEX_STREAM_C;
    for (int i = 0; i < FLEX_STREAM_C; ++i)
    {
        const long a = raw[i] < 0 ? -(long)raw[i] : (long)raw[i];
        if (a > flex_last_peak) flex_last_peak = a;
        flex_txbuff[flex_txbuff_pos] = raw[i];
        if (++flex_txbuff_pos >= FLEX_BUF_MAX) flex_txbuff_pos = 0;
    }
    if (flex_tx_primed < 4) flex_tx_primed++;
    return true;
}

void FlexVita::SendTxPacket()
{
    flex_send_ticks++;
    if (!tx_active_ || tx_stream_ == 0) return;
    if (flex_tx_primed < 2) return;      // let a little audio accumulate first

    // Catch up in whole packets against the wall clock so a coarse timer
    // cannot let the radio's TX buffer starve.
    static QElapsedTimer clock;
    static qint64 sent_frames = 0;
    if (!clock.isValid()) { clock.start(); sent_frames = 0; }

    const qint64 due = (clock.elapsed() * RadioAudioRate) / 1000;
    int packets = (int)((due - sent_frames) / FramesPerPacket);
    if (packets <= 0) return;
    if (packets > 8) packets = 8;        // do not burst after a stall

    // DAX TX is NOT the mirror image of DAX RX.  Verified against the radio's
    // own FWDPWR meter (37.4 dBm / 5.5 W where every other form gave 0 W):
    //
    //                 RX (radio -> us)        TX (us -> radio)
    //   packet type   3                       1
    //   class code    0x534C03E3              0x534C0123
    //   TSI / TSF     1 / 1                   3 / 1
    //   total size    1052 bytes              284 bytes  (71 words)
    //   payload       128 x stereo float32    128 x MONO int16 BIG-ENDIAN
    //
    // Sending the RX form back at the radio -- float32, stereo, class
    // 0x534C03E3 -- is silently discarded: the radio keys happily and emits
    // nothing at all.  These constants are measured, not derived; do not
    // "tidy" them or assume symmetry with the receive path.
    //
    //   [0]  header 0x18D00047 | (count << 16)
    //   [4]  stream id      [8] OUI 0x00001C2D   [12] class 0x534C0123
    //   [16] TSI (0)        [20] TSF, 64-bit (0)
    //   [28] payload: FramesPerPacket x int16 BE
    const int payload = FramesPerPacket * 2;          // mono int16
    QByteArray pkt;
    pkt.resize(28 + payload);

    for (int p = 0; p < packets; ++p)
    {
        char *d = pkt.data();
        // 0x18D00047: type 1, C=1, T=0, TSI=3, TSF=1, 71 words.
        const quint32 header =
            0x18D00047u | ((quint32)(tx_packet_count_ & 0x0F) << 16);
        WriteBE32(d,      header);
        WriteBE32(d + 4,  tx_stream_);
        WriteBE32(d + 8,  0x00001C2D);             // FlexRadio OUI
        WriteBE32(d + 12, 0x534C0123);             // DAX *TX* packet class
        WriteBE32(d + 16, 0);                      // TSI
        WriteBE32(d + 20, 0);                      // TSF high
        WriteBE32(d + 24, 0);                      // TSF low

        for (int i = 0; i < FramesPerPacket; ++i)
        {
            // Rawplayer hands us stereo 48 kHz at 24-bit scale; stride 4 takes
            // the left channel at 24 kHz, >>8 brings 24-bit down to 16-bit.
            int s = flex_txbuff[flex_tx_pos] / 256;
            if (s >  32767) s =  32767;
            if (s < -32768) s = -32768;
            const quint16 w = (quint16)(qint16)s;
            d[28 + i * 2]     = (char)((w >> 8) & 0xff);
            d[28 + i * 2 + 1] = (char)( w       & 0xff);
            flex_tx_pos += FLEX_TX_STRIDE;
            if (flex_tx_pos >= FLEX_BUF_MAX) flex_tx_pos -= FLEX_BUF_MAX;
        }

        audio_->writeDatagram(pkt, radio_addr_, VitaUdpPort);
        flex_pkts_sent++;
        tx_packet_count_ = (quint8)((tx_packet_count_ + 1) & 0x0F);
        sent_frames += FramesPerPacket;
    }
}

void FlexVita::SetSliceProperty(QString key, QString value)
{
    if (slice_id_ < 0 || value.isEmpty()) return;
    Command(QString("slice s %1 %2=%3").arg(slice_id_).arg(key).arg(value), 800);
}

void FlexVita::SetPtt(bool on)
{
    if (!tx_active_ || ptt_ == on) return;
    ptt_ = on;
    if (on) { flex_last_peak = 0; FlexDiag("PTT-ON  "); }
    Command(on ? "xmit 1" : "xmit 0", 800);
    if (!on) FlexDiag("PTT-OFF ");
}
