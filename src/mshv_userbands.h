/* MSHV user-defined bands (macOS port).
 *
 * MSHV's band tables in config_band_all.h are compile-time arrays sized by
 * COUNT_BANDS, and COUNT_BANDS also sizes fixed member arrays elsewhere
 * (HvTxW::s_tx_level, HvRigControl::offset_trsv_rig). Making bands genuinely
 * dynamic would mean converting all of that to containers and auditing ~60
 * call sites across the five most rebase-sensitive files in the tree.
 *
 * Instead the tables reserve MSHV_USER_BANDS empty slots at the end
 * (indices COUNT_BANDS_STD .. COUNT_BANDS-1) which the operator fills at
 * runtime from Radio And Frequencies -> Add Band. Every existing
 * `lst_bands[i]` read keeps working untouched.
 *
 * The catch this design exists to solve: config_band_all.h declares its
 * tables `static`, and the guards (_BANDS_H_, _LAMBDA_H_, ...) are defined in
 * FIVE separate translation units -- main_ms.cpp, hvtxw.cpp, radionetw.cpp,
 * hvlogw.cpp, hvrigcontrol.cpp. Each therefore gets its OWN private copy of
 * the arrays. Patching the array in one .cpp is invisible to the other four,
 * which would leave (say) main_ms showing the band while hvlogw wrote a blank
 * ADIF band. So the definitions live here, once, and each TU exposes a small
 * MshvApplyUserBands_<tu>() that copies them into its own private tables.
 * main() calls all five before Main_Ms is constructed.
 *
 * Definitions load from <MSHV data dir>/settings/user_bands.txt rather than
 * ms_stinfonet because band names are needed before RadioAndNetW exists to
 * parse ms_stinfonet. Per-mode frequency edits and the antenna description
 * still round-trip through the existing st_info_all key, which is name-keyed
 * and so already handles bands it has never seen.
 */
#ifndef MSHV_USERBANDS_H
#define MSHV_USERBANDS_H

#include <QString>

// Kept in sync with config_band_all.h. Duplicated rather than included so this
// header stays usable from translation units that don't want the tables.
#ifndef MSHV_USER_BANDS
#if defined _MACOS_
#define MSHV_USER_BANDS 4
#else
#define MSHV_USER_BANDS 0
#endif
#endif
#ifndef COUNT_BANDS_STD
#define COUNT_BANDS_STD 33
#endif

#define MSHV_USER_BAND_MODES 7

struct MshvUserBand
{
    QString name;       // lst_bands        "QO-100"
    QString lambda;     // lst_lambda       "3CM"    -- ADIF band, drives export + Club Log
    QString bcn;        // lst_bcnband      "10G"
    QString bandtofrq;  // lst_bandtofrq    "10489000" (kHz, digits only)
    unsigned long long fmin;  // freq_min_max.min, Hz
    unsigned long long fmax;  // freq_min_max.max, Hz
    // all_bands_mods_frq row, in TABLE order: MSK FSK FT4 FT8 JT65 Q65 FT2.
    // Dotted presentation form, e.g. "10.489.540".
    QString frq[MSHV_USER_BAND_MODES];

    MshvUserBand() : fmin(0), fmax(0) {}
    bool used() const { return !name.isEmpty(); }
};

class MshvUserBands
{
public:
    static MshvUserBands &Inst();

    void LoadFile(const QString &path);
    bool SaveFile(const QString &path) const;
    static QString DefaultPath();          // <data>/settings/user_bands.txt

    int  Count()    const;                 // configured slots
    int  FreeSlot() const;                 // first unused slot index, or -1
    const MshvUserBand &At(int slot) const;
    void Set(int slot, const MshvUserBand &b);
    void Clear(int slot);

    // Fill b.frq[] with base repeated across all seven modes. Convenience for
    // the Add Band dialog, which asks for one frequency; per-mode values are
    // then editable in the normal Radio And Frequencies table.
    static void FillModes(MshvUserBand &b, const QString &base);
    // "10489540000" / "10489.540.000" -> "10.489.540.000" grouping used by the
    // frequency column. Returns the input unchanged if it isn't all digits.
    static QString DotGroup(const QString &digits);

private:
    MshvUserBands() {}
    MshvUserBand m_b[MSHV_USER_BANDS > 0 ? MSHV_USER_BANDS : 1];
};

// Per-translation-unit table patchers -- see the header comment above.
// Each is defined in the .cpp that owns that copy of the tables.
void MshvApplyUserBands_main_ms();
void MshvApplyUserBands_hvtxw();
void MshvApplyUserBands_radionetw();
void MshvApplyUserBands_hvlogw();
void MshvApplyUserBands_hvrigcontrol();

// Calls all five, in the order above.
void MshvApplyUserBandsAll();

#endif
