# MSHV macOS port

Native macOS port of [LZ2HV/MSHV](https://github.com/LZ2HV/MSHV).
Apple Silicon (arm64) only — Homebrew Qt 5 is single-arch, so the
prebuilt binary won't run on Intel Macs. Verified end-to-end against
FlexRadio 6000-series via AetherSDR's TCI bridge for RX/TX with the
WSJT-X UDP broadcast feeding RUMlogNG's DXSpots window.

The port is a stack of `_MACOS_`-conditional patches on top of upstream
`main`. Linux and Windows builds remain untouched.

## Repo layout

- `main` — mirror of `LZ2HV/MSHV` upstream, never edited
- `macos-port` — this branch, default for working

## Building from scratch

Prereqs:

```
xcode-select --install
brew install qt@5 portaudio fftw
```

Build:

```
/opt/homebrew/opt/qt@5/bin/qmake CONFIG+=sdk_no_version_check MSHV_macOS.pro
make -j8
```

Output: `bin/MSHV.app`. Self-contained — bundle is movable anywhere
including `/Applications/`. User state lives in
`~/Library/Application Support/MSHV/` (settings, log, QSO history,
decoded-text logs); the bundle's `Contents/Resources/` is just a
first-launch seed.

Run:

```
open bin/MSHV.app
```

For diagnostic stderr (crash logs, TCI lifecycle, audio open events):

```
open --stderr /tmp/mshv.stderr bin/MSHV.app
```

## What the port does

| Commit | What it adds |
|---|---|
| `00fb1bb` | PortAudio audio backend; complex.h shim (libc++); 8 MB pthread stacks; HvAlsaMixer stub; CPU sensor stub; settings PortAudio device enum; serial _osx.cpp; Info.plist |
| `d736431` | App is self-contained — settings/, RxWavs/, log/, AllTxtMonthly/, ExportLog/, Screenshots/ live in `Contents/Resources/`; `mshv_app_path.h` redirects `App_Path` |
| `5fc0438` | Mono virtual audio device support (FlexRadio CommonRadioAudio, DAX); format/rate fallbacks (paInt24 → Float32 → Int16 → Int32); paFloat32→int32 conversion |
| `a54f93c` | `Options ▸ Settings` device dropdown actually drives the PortAudio open (was silently using OS default); cached TX stream so MSHV's per-message `new Rawplayer` doesn't churn the open/close; 200 ms TX / 1 s RX buffers |
| `4da4433` | Real CPU sensor via `host_processor_info`; QToolButton replacement for Log window's hidden QMenuBar; settings preservation in post-link rsync (read-only data wholesale, user-mutable only `cp -n`) |
| `cbb900e` | App icon (`MSHV.icns`); restore `TCI Client Input/Output` entries to device dropdown (Mac branch was returning early before they got appended) |
| `feae112` | TCI auto-connect with FlexRadio bridges. WebSocket lifecycle race fix (no `moveToThread` on Mac); `deleteLater` instead of `delete` to avoid disconnect crash; recognise AetherSDR's proactive `vfo:0,0,FREQ;` notification at connect; bumped TCI init retry from 5 to 20; deferred initial connect 1.5 s |
| `3046141` | "SDR ON/OFF" indicator refreshes when start/stop state changes (was painted once at init, then stale) |
| `f717734` | UDP broadcast identifies as `WSJT-X MSHV` so RUMlogNG's DXSpots accepts our decode messages |
| `85c1ff6` | Network configuration tabs defer IP/host validation to `editingFinished`; partial input no longer triggers DNS lookup or the red "UDP server lookup failed" flash on every keystroke |

Plus a number of small fixes during the same sessions: the
band-switcher list (just edit `def_band_bt_sw` in `ms_settings`),
Helvetica Neue + Menlo font defaults, dark theme by default, etc.

## Updating from a new LZ2HV release

One-time setup (only if the `upstream` remote isn't already there):

```
git remote add upstream https://github.com/LZ2HV/MSHV.git
git remote set-url --push upstream DISABLED
```

Per release:

```
git fetch upstream main
git checkout macos-port
git rebase upstream/main
```

If the rebase replays cleanly: rebuild, smoke-test, then
`git push origin macos-port --force-with-lease`.

If git stops on a conflict: it'll be in one of the files we touched.
The conflict is almost always **additive** — upstream changed code
near a `#if defined _MACOS_` block we own. Keep both. The pattern is:

```
<<<<<<< HEAD (upstream)
upstream's new code
=======
our existing code, plus our _MACOS_ block
>>>>>>> macos-port
```

→ keep upstream's new code, then re-apply our `#if defined _MACOS_`
block immediately after the upstream `_LINUX_`/`_WIN32_` block.
`git add <file>` + `git rebase --continue`.

Most-likely conflict sites (in rough order of likelihood):

- `src/HvMsCore/mscore.cpp` and `src/HvMsCore/mscore.h` (audio
  dispatch)
- `src/HvMsPlayer/libsound/mpegsound.h` and `rawplayer.cpp`
- `src/HvMsPlayer/msplayerhv.cpp`
- `src/SettingsMs/settings_ms.cpp` (device dropdown emit)
- `src/HvTxW/HvRadioNetW/network.cpp` (TCI client)
- `src/HvTxW/HvRadioNetW/radionetw.cpp` (UDP broadcast id)
- `src/HvRigControl/hvrigcontrol.cpp` (TCI auto-connect deferral)
- `src/HvTxW/HvLogW/hvlogw.cpp` (QToolButton menu)
- `src/HvTxW/hvmultianswermodw.cpp` and `.h` (MA dialog wiring)
- `src/HvTxW/hvtxw.cpp` (decoded-text broadcast gate)
- `src/CpuWidget/cpusensorhv.{cpp,h}` and `cpuwudget.{cpp,h}`
- `src/config.h` (`APP_NAME` for `_MACOS_`)
- `src/main_ms.{cpp,h}` (HvMixerMain include + instantiation)
- `src/HvDecoderMs/{decoderms,decoderpom,decoderq65}.h` (complex shim
  include)
- `MSHV_macOS.pro` if LZ2HV added or removed source files

### New source files in LZ2HV

LZ2HV occasionally adds files (new mode, new dialog, new
translation). They'll appear after the rebase. They have to be
mirrored into `MSHV_macOS.pro` under the right block (HEADERS /
SOURCES / RESOURCES / TRANSLATIONS) or the link will fail.

Quick diff against the Linux-x86_64 .pro:

```
diff <(grep -E '^[ ]*src/.*\.(cpp|h|qrc|ts)' MSHV_macOS.pro | sort -u) \
     <(grep -E '^[ ]*src/.*\.(cpp|h|qrc|ts)' MSHV_x86_64.pro | sort -u)
```

Anything in x86_64.pro but not macOS.pro needs adding to macOS.pro
— **except** the Linux-specific files we deliberately skip:

- `src/HvMsCore/linsound_in.cpp` (replaced by `macsound_in.cpp`)
- `src/HvMsPlayer/libsound/linsound_out.cpp` (replaced by
  `macsound_out.cpp`)
- `src/HvAlsaMixer/hvalsamixer.cpp`, `hvcbox.cpp`,
  `hvmixermain.cpp`, `hvrbutton.cpp`, `hvvtext.cpp` (the ALSA mixer
  widget; we use a header-only stub on Mac)
- `src/HvAlsaMixer/hvalsamixer.qrc` (Linux-only icons)
- `src/HvRigControl/qexsp_1_2rc/qextserialenumerator_linux.cpp`
  (replaced by `qextserialenumerator_osx.cpp`)

### Build + smoke test after rebase

```
/opt/homebrew/opt/qt@5/bin/qmake CONFIG+=sdk_no_version_check MSHV_macOS.pro
make -j8
open --stderr /tmp/mshv.stderr bin/MSHV.app
```

Run a full RX cycle, hit TUNE, check:

- TCI connects within ~3 s of launch (no manual Disconnect/Connect)
- "SDR ON" goes green when MONITOR is on
- Decodes appear on waterfall and in decode list
- RUMlog (or whichever logger) gets QSO logs and DXSpots
- TX audio is clean (no flutter)
- `Options ▸ Macros` saves the callsign
- Dark theme, Menlo decode columns

If all of that works, you're done.

```
git push origin macos-port --force-with-lease
git push origin main   # bring along any new commits on upstream main too
```

## Files we own (created by the port)

- `MSHV_macOS.pro` — Qt project file, Mac-specific
- `macos/Info.plist` — bundle metadata, `NSMicrophoneUsageDescription`,
  icon ref
- `macos/MSHV.icns` (+ `MSHV.iconset/`) — app icon
- `MACOS_PORT_README.md` — this file
- `src/mshv_app_path.h` — `~/Library/Application Support/MSHV/` redirect
  for App_Path on macOS, with first-launch seeding from the bundle's
  `Contents/Resources/`
- `src/mac_complex_shim.h` — C99 complex.h surface for libc++
- `src/mshv_thread_helper.h` — 8 MB pthread stacks
- `src/HvMsCore/macsound_in.cpp` — PortAudio capture backend
- `src/HvMsPlayer/libsound/macsound_out.cpp` — PortAudio playback
  backend

## Runtime data layout

User-mutable state at runtime:

```
~/Library/Application Support/MSHV/
├─ settings/
│  ├─ ms_settings    ← geometry, audio devices, mode, all dialog state
│  ├─ ms_macros      ← callsign, grid, macros
│  ├─ ms_mesages
│  ├─ ms_start
│  ├─ ms_stinfonet   ← UDP broadcast / per-band antenna / DAX TX buffer
│  └─ database/      ← cty.dat, msloc_db, msbcn_db, mstn_db, sat.dat
├─ log/              ← mshvlog.edim (QSO log)
├─ AllTxtMonthly/    ← ALL_YYYY_MM.TXT — monthly running text log
├─ ExportLog/        ← exported ADIF/Cabrillo
├─ RxWavs/           ← recorded WAVs
└─ Screenshots/      ← saved waterfall PNGs
```

The bundle's `Contents/Resources/` mirrors this same tree but is
**only a first-launch seed**. `mshv_app_path.h::mshv_app_data_path()`
runs at app startup, mkdir's the Library tree, and copies missing
files from Resources to Library (`cp -n` semantics — never
overwrites). After that, the app reads/writes Library exclusively.

The bundle is read-only at runtime, so it can sit safely in
`/Applications/`. Dragging a new MSHV.app over the old one preserves
all user state because user state isn't in the bundle.

The `MSHV_macOS.pro` `QMAKE_POST_LINK` step still populates the
bundle's Resources/ with seed data after each `make` so the bundle
remains self-installable on a fresh Mac. The `cp -n` vs rsync split
in that step controls which seed files get refreshed on every
rebuild vs preserved across rebuilds.

## Backing up before a major change

```
DATE=$(date +%Y%m%d-%H%M)
cp -R "$HOME/Library/Application Support/MSHV" \
      ~/Desktop/mshv-backup-$DATE/
```

That captures everything mutable.

## Known caveats

- **Icon resolution**: source PNG was 32×32; icns upscales look
  blocky at Dock-zoom sizes. Drop a 512+ px PNG in `src/pic/` and
  rerun `iconutil` if you want crisp.
- **CPU widget**: per-CPU usage via `host_processor_info`. The
  P-core/E-core split on M-series isn't labelled — they just appear
  as N independent cores. Cosmetic only.
- **User state lives in `~/Library/Application Support/MSHV/`** (not in
  the bundle). The bundle's `Contents/Resources/` is a seed that gets
  copied into Library on first launch (`cp -n` semantics — never
  overwrites). After that, settings/log/QSO history/decoded-text logs
  read and write from Library. So dragging a new MSHV.app to
  `/Applications/` is safe — your state stays put. See
  `src/mshv_app_path.h`.
- **No code-signing or notarisation.** Fine for personal use; required
  for distribution.

## Quick references

| Question | Answer |
|---|---|
| Which Qt? | qt@5 (5.15.x) — Qt 6 untested |
| Which audio API? | PortAudio over CoreAudio |
| Which complex.h? | clang built-ins (`__builtin_complex` etc.) — see `mac_complex_shim.h` |
| Which CPU API? | `host_processor_info(PROCESSOR_CPU_LOAD_INFO)` |
| Which serial enum? | `qextserialenumerator_osx.cpp` (already in upstream) + IOKit framework |
| Which TCI? | Identical to Linux — bypasses PortAudio |
| Which icon source? | `src/pic/ms_ico.png` upscaled via `iconutil` |
| Where's the build .pro? | `MSHV_macOS.pro` |
| Where's the build script? | None — just `qmake && make`. The `bin/` dir is the destination. |

## Pulling out a single fix as an upstream patch

If you ever want to send a fix to LZ2HV upstream (one of the audio,
complex.h, or PortAudio bits is genuinely broken on macOS, not just
Mac-specific), the relevant `_MACOS_` blocks are isolated enough to
extract individually with `git format-patch`. Most of our patches
are platform-conditional and won't affect Linux/Windows when applied,
so a clean pull request is feasible.

The exception: the QToolButton replacement in `hvlogw.cpp` is also
useful on Linux/Windows when running with `setNativeMenuBar(false)`,
which would make a more general upstream contribution.
