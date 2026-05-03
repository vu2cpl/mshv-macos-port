# Developing on this port

Notes for anyone (human or agent) maintaining the macOS port. End-user
documentation is in `README.md`. Detailed rebase + architecture notes
are in `MACOS_PORT_README.md`.

## Rule 1: User state is sacred — never lose it across rebuilds

Users keep callsign, grid, mode/band switcher selections, audio
device choice, filter ranges, QSO log, decoded-text monthly logs,
and dialog state. **A rebuild or new install must never force them
to redo any of it.**

### Where state lives

`~/Library/Application Support/MSHV/`
- `settings/{ms_settings, ms_macros, ms_mesages, ms_start, ms_stinfonet, database/}`
- `log/mshvlog.edim` — QSO log
- `AllTxtMonthly/ALL_YYYY_MM.TXT` — running decoded-text log
- `ExportLog/`, `RxWavs/`, `Screenshots/`

The bundle's `MSHV.app/Contents/Resources/` is a **first-launch seed
only**. `src/mshv_app_path.h::mshv_app_data_path()` runs at startup,
mkpaths the Library tree, and `cp -n`-copies any missing files from
the bundle. After that the app reads/writes Library exclusively.

### What this means in practice

- **Never** modify `~/Library/Application Support/MSHV/` from a
  build script, install script, or Makefile target.
- **Never** stage `bin/settings/ms_settings`, `bin/log/*`,
  `bin/AllTxtMonthly/*`, or any other user-mutable file in a commit
  unless the change is a deliberate, reviewed default for fresh
  installs. Run `git diff bin/settings/ bin/log/ bin/AllTxtMonthly/`
  before every commit; if the diff is runtime drift from testing,
  `git checkout` those files first.
- The build seed `bin/settings/ms_settings` should only carry sane
  defaults for new users — not someone's runtime state. When you
  intentionally change a default, document why in the commit message.
- Drag-replacing `MSHV.app` into `/Applications/` must remain safe.
  If a change would invalidate that property, stop and flag it
  before shipping.
- If `mshv_app_path.h` ever needs surgery, test the seeding path on
  a clean `~/Library/Application Support/MSHV/` (rename it aside,
  run the bundle, verify state appears) AND the no-op path (Library
  already has data, verify nothing gets clobbered).

## Rule 2: Mac changes go behind `#if defined _MACOS_`

This branch tracks LZ2HV upstream via rebase. To keep merges clean:

- Mac-specific behaviour changes wrap in `#if defined _MACOS_`
  blocks with the upstream code preserved in the `#else`. Don't
  replace upstream lines outright.
- New Mac-only files (e.g., `src/mshv_app_path.h`,
  `src/mac_complex_shim.h`, `src/mshv_thread_helper.h`,
  `src/HvMsCore/macsound_in.cpp`,
  `src/HvMsPlayer/libsound/macsound_out.cpp`) live in their own
  files so upstream rebase doesn't touch them.
- Build glue lives in `MSHV_macOS.pro` (separate from upstream
  `MSHV.pro`) and `macos/build-standalone.sh`.
- Where a Mac fix is also useful upstream (e.g., the `QToolButton`
  log-menu shim, ms_stinfonet preservation), it can be
  unconditional — that's a judgment call, err toward
  `_MACOS_`-guarded.

See `MACOS_PORT_README.md` for the rebase workflow and
conflict-likely file list.

## Rule 3: Commit, document, push after every unit of work

1. **Commit** with a real "why" in the message.
2. **Document** anything a future reader needs to know (this file,
   `MACOS_PORT_README.md`, code comments at non-obvious spots).
3. **Push** to `origin/<branch>` so work isn't stranded locally.

Do not skip 2 or 3 unless explicitly asked to hold.

## Build

```
/opt/homebrew/opt/qt@5/bin/qmake CONFIG+=sdk_no_version_check MSHV_macOS.pro
make -j8           # produces bin/MSHV.app linked to Homebrew Qt
make standalone    # embeds Qt frameworks, ad-hoc signs, ready for /Applications
```

`make standalone` is **idempotent** but doesn't refresh the binary —
it operates on whatever's currently in `bin/MSHV.app`. If you
re-`make`, re-run `make standalone` to re-embed the Qt frameworks.

After `make standalone` produces a clean bundle, drag
`bin/MSHV.app` to `/Applications/`. Library state is untouched.

## Verifying nothing leaks

After `make standalone` the script prints `main binary: clean (no
Homebrew paths)` and `embedded libs: all clean`. If it doesn't, the
bundle won't run on a Mac without Homebrew. Investigate before
shipping.
