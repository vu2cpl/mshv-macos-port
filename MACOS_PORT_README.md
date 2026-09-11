# MSHV macOS port

Native macOS port of [LZ2HV/MSHV](https://github.com/LZ2HV/MSHV).
Prebuilt, notarised binaries for both **Apple Silicon (arm64)** and
**Intel (x86_64)** Macs are attached to each release — see `README.md`
and `HELP.md` for which zip to pick. Verified end-to-end against
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
| `v2.76.6-mac4` | Simplified UDP Broadcast gains a Status line (green "sent hh:mm:ss" / red "not connected") and a Reconnect button, mirroring the WSJT-X path — a QSO that fails to send is no longer dropped silently; window title + About box now show the build as `MSHV macOS 2.76.6 mac4`; perf — data-dir resolver caches its bundle-seed scan (was repeated every call) and the audio capture path no longer heap-allocates per 5 ms tick |
| `v2.76.6-mac6` | Settings preservation — `SaveSettings` now carries over any `ms_stinfonet` line it doesn't itself write, so an older or differently-configured MSHV build sharing the same `~/Library/Application Support/MSHV` can no longer wipe keys it doesn't know about; window title + About box read `MSHV macOS 2.76.6 mac6` (mac5 skipped); this README no longer claims "Apple Silicon only" — both arm64 and x86_64 zips ship with every release. UDP broadcast id is unchanged (`WSJT-X MSHV`) until a public RUMlogNG release recognises `MSHV` natively |
| `v2.76.6-mac8` | **Native FlexRadio VITA-49 audio** — `Flex Native Input` / `Flex Native Output` take RX and TX audio straight off a FLEX-6000/8000 over the radio's own protocol, with no SmartSDR, TCI bridge or DAX virtual audio device in the path; `Flex Native Input DAX 2..8` selects the DAX channel; live forward power and SWR in the status row; a `Flex` panel beside it for power, SWR, reflected, ALC, PA temperature, supply volts, RX/TX antenna and mode. First public release carrying the Flex backend. Shipped with a single zip — see mac9 |
| `v2.76.6-mac9` | **Flex PTT ownership** — the native backend is now the only thing that keys the radio while it transmits. Previously both it and the Rig Control session keyed, and the rig-control path's `slice set <n> tx=1` re-designated the transmit slice mid-transmission, so the radio cycled its T/R and band relays twice: an audible **double relay click** on every transmission. The p2 RTS/DTR line (amp / SO2R) and the static-TX / QRG frequency handling are untouched. There was no settings workaround — "PTT OFF" does not stop it for a network rig. Both arm64 and x86_64 zips ship again, as this README promises and mac8 did not |
| `v2.76.6-mac10` | **Crash fix, and a change RUMlogNG users must act on.** `MsCore`'s constructor freed two never-initialised FFT pointers before allocating them; harmless on a zeroed block, a stray free on a recycled one, and macOS 26's allocator reports the damage later as *"BUG IN CLIENT OF LIBMALLOC: memory corruption of free block"* from somewhere unrelated. **The UDP client id is now the plain `MSHV`** — the `WSJT-X MSHV` prefix existed only because RUMlogNG keyed on a WSJT-X-style name, and Tom DL2RUM's native MSHV support shipped in RUMlogNG 6.5.1, so update RUMlogNG if spots stop arriving. **A second transmit request inside one transmission no longer reaches the rig** (LZ2HV's guard in `Main_Ms::SetRigTxRx()`, idempotent for every radio, not just Flex; the Flex-only backstop is gone with it). **Flex: MSHV no longer takes over another client's slice** — `index_letter` is numbered per client, so with SmartSDR connected first MSHV used to tune and key *their* slice while listening to its own, heard as a live waterfall with no decodes; the letter now resolves against slices MSHV owns, and the CAT side follows it. **Flex: MSHV puts the radio on frequency at startup** (last-used, or 14.074 FT8), instead of sitting on the 14.100 USB a fresh client is handed. **Flex panel gains transmit power, tune power, max power and hardware ALC**, and its meter is now driven from `Refresh()` rather than a second timer. **`azel.dat` is written to the Library tree, not into the bundle** — `HvAstroDataW` built its path from `applicationDirPath()`, so the `QFile` open failed silently and the port never wrote the Moon/Sun Az/El file rotator programs read, nor could it from a read-only location. It was the last bundle-relative write in the tree. **User-defined bands** arrive in the public build: Radio And Frequencies Configuration gains Add Band / Remove Band with four reserved slots for bands the standard table lacks, such as QO-100. Also in this release: the notarised zip is built with `--norsrc`, so it no longer carries the AppleDouble sidecars that made some unpackers report the app as damaged. **Re-issued 2026-09-12:** the first mac10 assets were signed without the `audio-input` entitlement, so on a Mac that had never granted MSHV microphone access the soundcard input was refused without a prompt; rebuilt from the same tag with it restored, no source change. |

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
│  ├─ azel.dat       ← Moon/Sun Az/El + Doppler for rotator / tracking programs, rewritten every 2 s while the Astronomical Data window is open
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

## Native FlexRadio VITA-49 backend (mac8)

`src/HvRigControl/HvRigCat/network/flexvita.{h,cpp}` — takes RX and TX
audio straight off a FLEX-6000/8000 over the radio's own protocol, with
no SmartSDR, TCI bridge or DAX virtual audio device in the path. (These
two files sat in their own `HvRigCat/flexvita/` directory up to mac9;
LZ2HV asked for them beside `network.cpp`, so from 2026-09-07 they live
in `HvRigCat/network/`. The control/monitor panel, `flexpanel.{h,cpp}`,
stayed behind in `flexvita/` — it is UI, not protocol.)

It reuses the network-audio seam LZ2HV already built for TCI
(`_SetRxAudioTci_`), so `mscore.cpp` needed one condition widened and the
decoder is untouched. MSHV already spoke the SmartSDR command protocol
for its eight `FlexRadio SmartSDR Slice A..H` rig models; only the audio
half was missing.

Measured protocol facts, since two of them are counter-intuitive:

- DAX **receive** is 24 kHz float32 big-endian, stereo-interleaved with
  L == R — not 48 kHz mono. De-interleaved it is natively one of MSHV's
  input rates, so it reaches the decoder with no resampling at all.
- DAX **transmit** is a different wire format, not the mirror of receive:
  packet type 1, class `0x534C0123`, 284-byte packets carrying 128 mono
  int16 big-endian samples, to UDP 4991. Send the receive form back and
  the radio keys perfectly and emits nothing at all.
- Meter scaling depends on the meter's **unit**: dBm/dBFS/SWR `/128`,
  Volts `/256`, degC `/64`, RPM `/1`.

**Exactly one thing may key the radio.** With the backend running, MSHV
holds **two** TCP sessions to the radio's API port — `flexvita`'s own
`client gui` session and the `FlexRadio SmartSDR Slice A..H TCP`
rig-control session. Two `ESTABLISHED` rows from one MSHV pid in
`lsof -nP -i TCP:4992` is the normal picture; both *keying* is not.

If both key, you get a **double relay click on every transmission**.
The rig-control Flex `set_ptt` does not send a bare `xmit` — it sends
`dax audio set N tx=1`, then `slice set <n> tx=1`, then `xmit 1`, and
that middle command re-designates the transmit slice milliseconds after
`flexvita` has already keyed, so the radio drops and re-engages its T/R
and band relays. `HvRigControl::SetPtt_p()` therefore skips the keying
when `id==0 && _FlexVitaTxActive_()`. The id matters:

| id | Only caller | Suppressed? |
|---|---|---|
| 0 "All" | `Main_Ms::SetRigTxRx()` — the app's TX transition | yes, the Flex hook already keyed |
| 1 | `TestPtt()` — the START PTT TEST button | no; nothing else keys that path |
| 2 | `TestPtt2()` — second PTT line (amp / SO2R) | never touched |

Deliberately narrow: the p2 RTS/DTR line still fires, the static-TX and
QRG frequency handling in the caller is untouched (and preserves the id
through `ss_id`, so the automatic path stays id 0), and `fsdrs_poll` is
re-armed from `set_freq` / `set_mode` / init so polling does not stall.

Note START PTT TEST on a Flex still keys over the *rig-control* session,
so its relay behaviour looks like the pre-fix behaviour — correct, since
during a deliberate test it is the only thing keying.

Note the **"PTT OFF" radio button is not a workaround** — for a network
rig, `SetPtt_p` keys through the `omnirig_active || net_active` branch,
which is tested before and independently of `rb_ptt_off` / `rb_cat`.

**The slice has to agree too.** Removing that keying also removed what
had accidentally been holding the two halves together: `slice set <n>
tx=1` on every key dragged the transmitter onto the rig control's slice.
Nothing else forced the audio backend and the rig control onto the same
slice, and a disagreement means MSHV **displays one frequency and works
another**. `FlexVita::ChooseSlice()` therefore prefers, in order: the
slice selected in Rig Control (`Slice A..H TCP` → 0..7, read through
`_GetFlexNativeSlice_()` beside `_GetFlexNativeHost_()`) if it exists on
the radio; then any slice this session already owns; then a new one.

Adopting the configured slice is allowed even when another client owns
it — the rig control addresses slices by number regardless of owner, so
following it is what keeps the two consistent, and `slice_created_`
stays false so `Stop()` never removes a slice it did not make. The radio
assigns the index when a slice *is* created, so a mismatch there is
fixed by pointing Rig Control at the matching slice letter. Either way
it is now visible instead of silent: red on the Flex panel's top line,
and a `START` record in `flexvita_tx.log` —

    11:57:15.269  START   slice=0 rig_wants=0 adopted

which matters because a Finder-launched bundle has no visible stderr.

The TX format is Dick Hale **W7PP**'s finding, from his GPLv3 WSJT-X
fork; the implementation here is independent.
