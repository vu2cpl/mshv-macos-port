/* MSHV user-defined bands (macOS port) -- shared store.
 * See mshv_userbands.h for why this exists and why the apply functions are
 * per-translation-unit.
 */
#include "mshv_userbands.h"
#include "mshv_app_path.h"

#include <QFile>
#include <QTextStream>
#include <QStringList>

MshvUserBands &MshvUserBands::Inst()
{
    static MshvUserBands inst;
    return inst;
}

QString MshvUserBands::DefaultPath()
{
    return mshv_app_data_path() + "/settings/user_bands.txt";
}

int MshvUserBands::Count() const
{
    int n = 0;
    for (int i = 0; i < MSHV_USER_BANDS; ++i)
        if (m_b[i].used()) ++n;
    return n;
}

int MshvUserBands::FreeSlot() const
{
    for (int i = 0; i < MSHV_USER_BANDS; ++i)
        if (!m_b[i].used()) return i;
    return -1;
}

const MshvUserBand &MshvUserBands::At(int slot) const
{
    static const MshvUserBand empty;
    if (slot < 0 || slot >= MSHV_USER_BANDS) return empty;
    return m_b[slot];
}

void MshvUserBands::Set(int slot, const MshvUserBand &b)
{
    if (slot < 0 || slot >= MSHV_USER_BANDS) return;
    m_b[slot] = b;
}

void MshvUserBands::Clear(int slot)
{
    if (slot < 0 || slot >= MSHV_USER_BANDS) return;
    m_b[slot] = MshvUserBand();
}

// Mirrors the re-dotting in RadioAndNetW::ReadSettings (radionetw.cpp): at most
// two separators, inserted from the right, so 10489540000 -> 10489.540.000.
QString MshvUserBands::DotGroup(const QString &digits)
{
    QString d = digits;
    d.remove(".");
    d.remove(",");
    d.remove(" ");
    for (int i = 0; i < d.count(); ++i)
        if (!d.at(i).isDigit()) return digits;
    if (d.isEmpty()) return digits;

    int k = d.count();
    k -= 3;
    if (k > 0) d.insert(k, ".");
    k -= 3;
    if (k > 0) d.insert(k, ".");
    return d;
}

void MshvUserBands::FillModes(MshvUserBand &b, const QString &base)
{
    const QString v = DotGroup(base);
    for (int i = 0; i < MSHV_USER_BAND_MODES; ++i) b.frq[i] = v;
}

/* File format -- one line per SLOT, '|' separated, '#' starts a comment:
 *
 *   name|lambda|bcn|bandtofrq_kHz|fmin_Hz|fmax_Hz|MSK|FSK|FT4|FT8|JT65|Q65|FT2
 *
 * Trailing mode columns may be omitted; missing ones inherit the first one
 * given. A line of "-" is an empty slot placeholder.
 *
 * Line position IS the slot, and slot fixes the band index. Removing a band
 * writes a placeholder rather than dropping the line, so the bands after it
 * keep their index -- default_band, the per-band TX levels and def_band_bt_sw
 * all persist a band index, and repacking would silently move the operator to
 * a different band with different TX drive.
 */
void MshvUserBands::LoadFile(const QString &path)
{
    for (int i = 0; i < MSHV_USER_BANDS; ++i) m_b[i] = MshvUserBand();
    if (MSHV_USER_BANDS == 0) return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream in(&f);

    int slot = 0;
    while (!in.atEnd() && slot < MSHV_USER_BANDS)
    {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith("#")) continue;

        // Every data line consumes a slot, including placeholders, so that a
        // removed band doesn't shift the ones after it.
        const QStringList c = line.split("|");
        if (c.count() < 6 || c.at(0).trimmed().isEmpty() || c.at(0).trimmed()=="-")
        {
            ++slot;
            continue;
        }

        MshvUserBand b;
        b.name      = c.at(0).trimmed();
        b.lambda    = c.at(1).trimmed();
        b.bcn       = c.at(2).trimmed();
        b.bandtofrq = c.at(3).trimmed();
        b.fmin      = c.at(4).trimmed().toULongLong();
        b.fmax      = c.at(5).trimmed().toULongLong();
        // A zero-width window can never match a real dial frequency, which
        // would make the band undetectable rather than wrong -- but a reversed
        // one would match nothing silently, so normalise.
        if (b.fmax < b.fmin) { const unsigned long long t = b.fmin; b.fmin = b.fmax; b.fmax = t; }

        QString first;
        for (int m = 0; m < MSHV_USER_BAND_MODES; ++m)
        {
            const int col = 6 + m;
            QString v = (col < c.count()) ? c.at(col).trimmed() : QString();
            if (!v.isEmpty() && first.isEmpty()) first = v;
            b.frq[m] = v;
        }
        for (int m = 0; m < MSHV_USER_BAND_MODES; ++m)
            if (b.frq[m].isEmpty()) b.frq[m] = first;
        for (int m = 0; m < MSHV_USER_BAND_MODES; ++m)
            b.frq[m] = DotGroup(b.frq[m]);

        m_b[slot] = b;
        ++slot;
    }
    f.close();
}

bool MshvUserBands::SaveFile(const QString &path) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream out(&f);

    out << "# MSHV user-defined bands. Managed by Radio And Frequencies -> Add Band.\n";
    out << "# name|lambda|bcn|bandtofrq_kHz|fmin_Hz|fmax_Hz|MSK|FSK|FT4|FT8|JT65|Q65|FT2\n";
    out << "# lambda is the ADIF band (drives ADIF export and Club Log matching).\n";
    out << "# Max " << QString("%1").arg(MSHV_USER_BANDS) << " bands. Line position = slot = band index:\n";
    out << "# do not reorder or delete lines; \"-\" marks an empty slot.\n";
    // All slots are written, in order, empty ones as "-" -- see LoadFile.
    for (int i = 0; i < MSHV_USER_BANDS; ++i)
    {
        const MshvUserBand &b = m_b[i];
        if (!b.used()) { out << "-\n"; continue; }
        out << b.name << "|" << b.lambda << "|" << b.bcn << "|" << b.bandtofrq << "|"
            << QString("%1").arg(b.fmin) << "|" << QString("%1").arg(b.fmax);
        for (int m = 0; m < MSHV_USER_BAND_MODES; ++m) out << "|" << b.frq[m];
        out << "\n";
    }
    f.close();
    return true;
}

void MshvApplyUserBandsAll()
{
    MshvApplyUserBands_main_ms();
    MshvApplyUserBands_hvtxw();
    MshvApplyUserBands_radionetw();
    MshvApplyUserBands_hvlogw();
    MshvApplyUserBands_hvrigcontrol();
}
