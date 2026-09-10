/* MSHV
 * By Hrisimir Hristov - LZ2HV
 * May be used under the terms of the GNU General Public License (GPL)
 */
#include "flexpanel.h"
#include "../network/network.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

static QLabel *ValueLabel()
{
    QLabel *l = new QLabel("--");
    l->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    l->setAlignment(Qt::AlignCenter);
    l->setMinimumWidth(84);
    return l;
}

FlexPanel::FlexPanel(bool dark, QWidget *parent)
    : QDialog(parent)
{
    Q_UNUSED(dark)
    setWindowTitle(tr("FlexRadio Native"));
    setWindowFlags(windowFlags() ^ Qt::WindowContextHelpButtonHint);
    // Non-modal: the operator keeps working with this open.
    setModal(false);
    filling = false;
    // -1 = the radio has told us nothing yet, so nothing may be sent back.
    shown_rf = shown_tune = shown_max = -1;

    QVBoxLayout *V = new QVBoxLayout(this);
    V->setContentsMargins(8, 8, 8, 8);
    V->setSpacing(6);

    l_conn = new QLabel();
    l_conn->setAlignment(Qt::AlignCenter);
    V->addWidget(l_conn);

    QGroupBox *gb_m = new QGroupBox(tr("Radio meters"));
    QGridLayout *G = new QGridLayout();
    G->setContentsMargins(8, 6, 8, 6);
    G->setSpacing(5);
    int r = 0;
    l_fwd    = ValueLabel(); G->addWidget(new QLabel(tr("Forward power")), r, 0); G->addWidget(l_fwd,    r++, 1);
    l_swr    = ValueLabel(); G->addWidget(new QLabel(tr("SWR")),           r, 0); G->addWidget(l_swr,    r++, 1);
    l_ref    = ValueLabel(); G->addWidget(new QLabel(tr("Reflected")),     r, 0); G->addWidget(l_ref,    r++, 1);
    l_alc    = ValueLabel(); G->addWidget(new QLabel(tr("ALC")),           r, 0); G->addWidget(l_alc,    r++, 1);
    l_patemp = ValueLabel(); G->addWidget(new QLabel(tr("PA temperature")),r, 0); G->addWidget(l_patemp, r++, 1);
    l_volts  = ValueLabel(); G->addWidget(new QLabel(tr("Supply")),        r, 0); G->addWidget(l_volts,  r++, 1);
    gb_m->setLayout(G);
    V->addWidget(gb_m);

    QGroupBox *gb_c = new QGroupBox(tr("Slice"));
    QGridLayout *C = new QGridLayout();
    C->setContentsMargins(8, 6, 8, 6);
    C->setSpacing(5);
    cb_rxant = new QComboBox();
    cb_txant = new QComboBox();
    cb_mode  = new QComboBox();
    C->addWidget(new QLabel(tr("RX antenna")), 0, 0); C->addWidget(cb_rxant, 0, 1);
    C->addWidget(new QLabel(tr("TX antenna")), 1, 0); C->addWidget(cb_txant, 1, 1);
    C->addWidget(new QLabel(tr("Mode")),       2, 0); C->addWidget(cb_mode,  2, 1);
    gb_c->setLayout(C);
    V->addWidget(gb_c);

    // Transmit power.  These are radio-global, not slice properties, hence a
    // group of their own rather than sitting under "Slice" above.
    //
    // Spinboxes, committing on Enter or focus-out (editingFinished) rather
    // than on every keystroke or arrow-click: an amplifier is downstream, and
    // a control that fires on valueChanged would both spam the radio and let a
    // stray drag walk the drive up while you watched.
    QGroupBox *gb_p = new QGroupBox(tr("Transmit power"));
    QGridLayout *P = new QGridLayout();
    P->setContentsMargins(8, 6, 8, 6);
    P->setSpacing(5);
    sb_rfpower   = new QSpinBox();
    sb_tunepower = new QSpinBox();
    sb_maxpower  = new QSpinBox();
    QSpinBox *pw[3] = { sb_rfpower, sb_tunepower, sb_maxpower };
    for (int i = 0; i < 3; i++)
    {
        pw[i]->setRange(0, 100);
        pw[i]->setSuffix(" %");
        pw[i]->setKeyboardTracking(false);// do not emit per keystroke
    }
    sb_rfpower->setToolTip(tr("RF drive for normal transmit (transmit set rfpower).\n"
                              "Takes effect on Enter or when the field loses focus."));
    sb_tunepower->setToolTip(tr("RF drive for TUNE only (transmit set tunepower).\n"
                                "Kept separate so a tune-up does not hit the\n"
                                "amplifier at full transmit drive."));
    sb_maxpower->setToolTip(tr("Ceiling the radio enforces on the two above\n"
                               "(transmit set max_power_level)."));
    cb_hwalc = new QCheckBox(tr("Hardware ALC"));
    cb_hwalc->setToolTip(tr("Let an external amplifier's ALC line control drive\n"
                            "(transmit set hwalc_enabled)."));
    int pr = 0;
    P->addWidget(new QLabel(tr("RF power")),   pr, 0); P->addWidget(sb_rfpower,   pr++, 1);
    P->addWidget(new QLabel(tr("Tune power")), pr, 0); P->addWidget(sb_tunepower, pr++, 1);
    P->addWidget(new QLabel(tr("Max power")),  pr, 0); P->addWidget(sb_maxpower,  pr++, 1);
    P->addWidget(cb_hwalc, pr, 0, 1, 2);
    gb_p->setLayout(P);
    V->addWidget(gb_p);

    // Working the radio through MSHV otherwise means hearing it twice: once
    // from the decoded DAX stream and once out of the radio's own speaker.
    QGroupBox *gb_a = new QGroupBox(tr("Local audio"));
    QVBoxLayout *A = new QVBoxLayout();
    A->setContentsMargins(8, 6, 8, 6);
    A->setSpacing(5);
    // Keep this label SHORT: it is the widest thing in the group and the
    // panel sizes itself to it. The detail belongs in the tooltip.
    cb_localmute = new QCheckBox(tr("Mute front speaker"));
    cb_localmute->setToolTip(tr("Mutes the speaker in the radio's front panel.\n"
                                "M series only - other models have no front speaker.\n"
                                "Line-out and headphones are left alone, and the\n"
                                "DAX audio MSHV decodes is unaffected."));
    A->addWidget(cb_localmute);
    l_model = new QLabel();
    l_model->setAlignment(Qt::AlignLeft);
    A->addWidget(l_model);
    gb_a->setLayout(A);
    V->addWidget(gb_a);

    connect(cb_rxant, SIGNAL(currentIndexChanged(int)), this, SLOT(RxAntChanged(int)));
    connect(cb_txant, SIGNAL(currentIndexChanged(int)), this, SLOT(TxAntChanged(int)));
    connect(cb_mode,  SIGNAL(currentIndexChanged(int)), this, SLOT(ModeChanged(int)));
    connect(cb_localmute, SIGNAL(toggled(bool)), this, SLOT(LocalMuteToggled(bool)));
    connect(sb_rfpower,   SIGNAL(editingFinished()), this, SLOT(RfPowerEdited()));
    connect(sb_tunepower, SIGNAL(editingFinished()), this, SLOT(TunePowerEdited()));
    connect(sb_maxpower,  SIGNAL(editingFinished()), this, SLOT(MaxPowerEdited()));
    connect(cb_hwalc,     SIGNAL(toggled(bool)),     this, SLOT(HwAlcToggled(bool)));

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(Refresh()));
    timer->start(400);
    Refresh();
}

// Repopulate without the combo's own change signal firing back at the radio.
void FlexPanel::FillCombo(QComboBox *box, QStringList items, QString current)
{
    if (items.isEmpty()) return;
    // Leave a combo the operator is currently using alone.
    if (box->count() == items.count() && box->currentText() == current) return;

    filling = true;
    box->clear();
    box->addItems(items);
    const int at = items.indexOf(current);
    if (at >= 0) box->setCurrentIndex(at);
    filling = false;
}

// Mirror the radio's value into a spinbox without it echoing straight back,
// and without overwriting a number the operator is in the middle of typing --
// the poll runs every 400 ms, which is easily inside a two-keystroke edit.
void FlexPanel::FillSpin(QSpinBox *box, int value, int &shown)
{
    if (value < 0) return;              // radio has not reported it yet
    if (box->hasFocus()) return;        // being edited right now: leave alone
    shown = value;                      // remember what the radio told us
    if (box->value() == value) return;
    filling = true;
    box->setValue(value);
    filling = false;
}

// Send a power setting ONLY when the operator actually changed the number.
//
// editingFinished() fires on plain focus-out with nothing edited, so the test
// for "did this change?" carries the whole weight.  The first version compared
// the box against the RADIO's current value, which is the wrong reference in
// two reachable cases:
//
//  * at startup the box reads 0 while the radio has reported nothing (-1), so
//    "different" is true and a focus-out would push 0 at the radio;
//  * Flex RF power is PER BAND.  Change band while the box has focus and the
//    radio reports a new rfpower that FillSpin declines to write; the box then
//    holds the PREVIOUS band's number, looks "different", and a focus-out
//    sends it -- silently retuning the operator's power on the new band.
//
// Neither has been observed on the air; both fall straight out of reading the
// code, and with an amplifier downstream an unasked-for power change is worth
// designing out rather than arguing about the odds.
//
// Comparing against `shown` -- the last value WE put in the box -- makes an
// untouched box a no-op by construction, whatever the radio does meanwhile.
void FlexPanel::SendIfEdited(QSpinBox *box, int &shown, int radio_value, const char *key)
{
    if (filling) return;
    if (radio_value < 0) return;        // radio has not reported: never push a guess
    if (shown < 0) return;              // we have never displayed a real value
    if (box->value() == shown) return;  // operator did not change it
    shown = box->value();               // so a second focus-out does not resend
    _FlexVitaSetTransmit_(QString(key), shown);
}

void FlexPanel::Refresh()
{
    const bool up = _FlexVitaRxActive_() || _FlexVitaTxActive_();
    if (!up)
    {
        l_conn->setText("<b>" + tr("Flex Native: not connected") + "</b>");
        return;
    }
    // The backend's own status line carries the DAX channel and, more
    // importantly, the slice it is actually using -- plus a warning when that
    // is not the slice selected in Rig Control.  A mismatch means MSHV
    // displays one frequency and works another, so it must be visible rather
    // than buried: red, and it is the only thing on this line that changes.
    const QString st = _FlexVitaStatus_();
    if (st.contains("[!]"))
        l_conn->setText("<b><font color='#ff5050'>" + st + "</font></b>");
    else
        l_conn->setText("<b>" + st + "</b>");

    double fwd = 0.0, ref = 0.0, swr = 0.0, v = 0.0;
    if (_FlexVitaMeters_(&fwd, &ref, &swr))
    {
        l_fwd->setText(QString("%1 W").arg(fwd, 0, 'f', (fwd < 10.0) ? 2 : 1));
        l_ref->setText(QString("%1 W").arg(ref, 0, 'f', 2));
        l_swr->setText(QString::number(swr, 'f', 2));
        // Only meaningful under load -- an unkeyed radio reads exactly 1.00.
        if (fwd > 0.5 && swr >= 3.0)      l_swr->setStyleSheet("QLabel{color:rgb(255,80,80);}");
        else if (fwd > 0.5 && swr >= 2.0) l_swr->setStyleSheet("QLabel{color:rgb(255,180,60);}");
        else                              l_swr->setStyleSheet("");
    }
    if (_FlexVitaMeterByName_("ALC", &v))    l_alc->setText(QString("%1 dBFS").arg(v, 0, 'f', 1));
    if (_FlexVitaMeterByName_("PATEMP", &v)) l_patemp->setText(QString("%1 C").arg(v, 0, 'f', 1));
    if (_FlexVitaMeterByName_("+13.8A", &v)) l_volts->setText(QString("%1 V").arg(v, 0, 'f', 2));

    FillCombo(cb_rxant, _FlexVitaAntList_(false), _FlexVitaAnt_(false));
    FillCombo(cb_txant, _FlexVitaAntList_(true),  _FlexVitaAnt_(true));
    FillCombo(cb_mode,  _FlexVitaModeList_(),     _FlexVitaMode_());

    FillSpin(sb_rfpower,   _FlexVitaRfPower_(),   shown_rf);
    FillSpin(sb_tunepower, _FlexVitaTunePower_(), shown_tune);
    FillSpin(sb_maxpower,  _FlexVitaMaxPower_(),  shown_max);
    const bool hwalc = _FlexVitaHwAlc_();
    if (cb_hwalc->isChecked() != hwalc)
    {
        filling = true;
        cb_hwalc->setChecked(hwalc);
        filling = false;
    }

    // Only an M series radio has a front speaker; on anything else the
    // command does not exist, so grey the control rather than offer one that
    // silently does nothing.
    const bool has_spkr = _FlexVitaHasFrontSpeaker_();
    if (cb_localmute->isEnabled() != has_spkr) cb_localmute->setEnabled(has_spkr);

    const bool muted = _FlexVitaFrontSpeakerMute_();
    if (cb_localmute->isChecked() != muted)
    {
        filling = true;
        cb_localmute->setChecked(muted);
        filling = false;
    }

    // Model only -- no explanatory suffix. Why the box is greyed goes in the
    // tooltip, which costs no width; a sentence here stretched the whole
    // panel to fit it.
    const QString model = _FlexVitaRadioModel_();
    if (!model.isEmpty() && l_model->text().isEmpty())
    {
        l_model->setText(tr("Radio: %1").arg(model));
        cb_localmute->setToolTip(has_spkr
            ? tr("Mutes the speaker in the radio's front panel.\n"
                 "Line-out and headphones are left alone, and the\n"
                 "DAX audio MSHV decodes is unaffected.")
            : tr("%1 has no front panel speaker.\n"
                 "This control is for M series radios.").arg(model));
    }
}

void FlexPanel::RxAntChanged(int)
{
    if (filling) return;
    _FlexVitaSetAnt_(false, cb_rxant->currentText());
}

void FlexPanel::TxAntChanged(int)
{
    if (filling) return;
    _FlexVitaSetAnt_(true, cb_txant->currentText());
}

void FlexPanel::ModeChanged(int)
{
    if (filling) return;
    _FlexVitaSetMode_(cb_mode->currentText());
}

// editingFinished() also fires on focus-out with the value unchanged, so each
// of these would re-send the radio its own setting every time the panel lost
// focus. Harmless but noisy on the CAT link, so send only a real change.
void FlexPanel::RfPowerEdited()
{
    SendIfEdited(sb_rfpower, shown_rf, _FlexVitaRfPower_(), "rfpower");
}

void FlexPanel::TunePowerEdited()
{
    SendIfEdited(sb_tunepower, shown_tune, _FlexVitaTunePower_(), "tunepower");
}

void FlexPanel::MaxPowerEdited()
{
    SendIfEdited(sb_maxpower, shown_max, _FlexVitaMaxPower_(), "max_power_level");
}

void FlexPanel::HwAlcToggled(bool on)
{
    if (filling) return;
    _FlexVitaSetTransmit_("hwalc_enabled", on ? 1 : 0);
}

void FlexPanel::LocalMuteToggled(bool on)
{
    // Refresh() sets this box from the backend's own state; without the guard
    // that would bounce straight back at the radio as a fresh command.
    if (filling) return;
    _FlexVitaSetFrontSpeakerMute_(on);
}
