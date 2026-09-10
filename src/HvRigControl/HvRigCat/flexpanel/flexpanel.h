/* MSHV
 * By Hrisimir Hristov - LZ2HV
 * May be used under the terms of the GNU General Public License (GPL)
 */
/*
 * Native Flex VITA-49 control / monitor panel.
 *
 * A small non-modal window showing the radio's own meters and offering the
 * two controls that are awkward to reach otherwise: antenna and mode.
 *
 * Everything here is driven from what the radio reports. The antenna and mode
 * dropdowns are populated from the slice's own ant_list / tx_ant_list /
 * mode_list, so MSHV never offers a choice the radio would reject, and the
 * lists follow the hardware rather than a table that goes stale.
 *
 * The panel is inert when the Flex backend is not running: it simply shows
 * that it is not connected.
 */

#ifndef FLEXPANEL_H
#define FLEXPANEL_H

#include <QDialog>

class QLabel;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QTimer;

class FlexPanel : public QDialog
{
    Q_OBJECT
public:
    explicit FlexPanel(bool dark, QWidget *parent = 0);

private slots:
    void Refresh();
    void RxAntChanged(int);
    void TxAntChanged(int);
    void ModeChanged(int);
    void LocalMuteToggled(bool);
    void RfPowerEdited();
    void TunePowerEdited();
    void MaxPowerEdited();
    void HwAlcToggled(bool);

private:
    void FillCombo(QComboBox *box, QStringList items, QString current);
    // Show the radio's value without the spinbox echoing it straight back, and
    // without stamping on a number the operator is part-way through typing.
    // `shown` records what we last displayed, which is how an edit by the
    // operator is told apart from a value the radio gave us.
    void FillSpin(QSpinBox *box, int value, int &shown);
    // Common guard for the three power spinboxes: send only a value the
    // OPERATOR changed, never one we merely displayed.  See the comment at
    // RfPowerEdited() -- getting this wrong transmits at the wrong power.
    void SendIfEdited(QSpinBox *box, int &shown, int radio_value, const char *key);

    QLabel    *l_conn;
    QLabel    *l_fwd;
    QLabel    *l_ref;
    QLabel    *l_swr;
    QLabel    *l_alc;
    QLabel    *l_patemp;
    QLabel    *l_volts;
    QComboBox *cb_rxant;
    QComboBox *cb_txant;
    QComboBox *cb_mode;
    QCheckBox *cb_localmute;
    QSpinBox  *sb_rfpower;
    QSpinBox  *sb_tunepower;
    QSpinBox  *sb_maxpower;
    QCheckBox *cb_hwalc;
    QLabel    *l_model;
    QTimer    *timer;
    bool       filling;   // suppress the change signals while repopulating
    // What we last DISPLAYED in each power box, -1 = nothing yet.  A box whose
    // value still equals this was not touched by the operator, so it must not
    // be sent back to the radio.
    int        shown_rf;
    int        shown_tune;
    int        shown_max;
};

#endif
