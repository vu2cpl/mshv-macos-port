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

private:
    void FillCombo(QComboBox *box, QStringList items, QString current);

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
    QTimer    *timer;
    bool       filling;   // suppress the change signals while repopulating
};

#endif
