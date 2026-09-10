// mshv_txtrace.h -- macOS-only diagnostic: who asks for TX twice?
//
// Added 2026-09-08 to close the open question behind the Flex double relay
// click.  Two Flex-only guards stopped the duplicate reaching the radio then
// -- a Static TX early return in hvrigcontrol.cpp and an fsdrs_keyed backstop
// in network.cpp -- but neither stopped it being GENERATED, and this is what
// named the caller: Multi Answer Mode, by two paths.  Both guards are gone
// now (2026-09-09 and 2026-09-10); the duplicate is stopped at the source by
// `if (f_tx_busy == f) return;` in Main_Ms::SetRigTxRx().  The trace stays,
// because it is how that fix was measured and how the next one would be.
//
// Entirely inert unless MSHV_TXTRACE is set in the environment, so it costs
// one already-cached int compare per TX transition in a normal run:
//
//     open -a /Applications/MSHV/MSHV.app \
//          --env MSHV_TXTRACE=1 --env MSHV_FLEX_TRACE=1 \
//          --stderr /tmp/mshv-flex.log
//
// New file rather than an edit to a shared header, so an upstream rebase onto
// LZ2HV's tree never sees it (CLAUDE.md Rule 2).

#ifndef MSHV_TXTRACE_H
#define MSHV_TXTRACE_H

#if defined _MACOS_

#include <cxxabi.h>
#include <execinfo.h>
#include <cstdlib>
#include <cstring>
#include <QString>
#include <QThread>

static inline bool MshvTxTraceOn()
{
    static int on = -1;
    if (on < 0)
    {
        const char *e = getenv("MSHV_TXTRACE");
        on = (e && *e && strcmp(e, "0") != 0) ? 1 : 0;
    }
    return on == 1;
}

// A demangled call stack.  backtrace_symbols() hands back
// "<idx> <image> <addr> <mangled symbol> + <offset>"; we want the symbol.
static inline QString MshvBacktrace(int skip = 2, int maxframes = 14)
{
    void *frames[64];
    int n = backtrace(frames, 64);
    char **syms = backtrace_symbols(frames, n);
    if (!syms) return QString();

    QString out;
    for (int i = skip; i < n && i < skip + maxframes; ++i)
    {
        QString line = QString::fromUtf8(syms[i]);
        // step past "<idx> <image> <addr> " to the symbol
        int addr = line.indexOf(" 0x");
        int sym  = (addr >= 0) ? line.indexOf(' ', addr + 1) : -1;
        QString name = (sym > 0) ? line.mid(sym + 1) : line;
        int plus = name.lastIndexOf(" + ");
        if (plus > 0) name = name.left(plus);

        int status = 0;
        char *dem = abi::__cxa_demangle(name.toUtf8().constData(), NULL, NULL, &status);
        if (status == 0 && dem) name = QString::fromUtf8(dem);
        if (dem) free(dem);

        out += QString("        %1\n").arg(name);
    }
    free(syms);
    return out;
}

#endif // _MACOS_
#endif // MSHV_TXTRACE_H
