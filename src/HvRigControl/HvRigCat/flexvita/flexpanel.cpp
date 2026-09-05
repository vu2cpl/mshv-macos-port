/* MSHV
 * By Hrisimir Hristov - LZ2HV
 * May be used under the terms of the GNU General Public License (GPL)
 */
#include "flexpanel.h"
#include "flexvita.h"

#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
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

    connect(cb_rxant, SIGNAL(currentIndexChanged(int)), this, SLOT(RxAntChanged(int)));
    connect(cb_txant, SIGNAL(currentIndexChanged(int)), this, SLOT(TxAntChanged(int)));
    connect(cb_mode,  SIGNAL(currentIndexChanged(int)), this, SLOT(ModeChanged(int)));

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
