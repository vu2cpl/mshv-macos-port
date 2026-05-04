# MSHV macOS — Help & Quick Reference

This is the macOS-specific cheat-sheet. For general MSHV usage (modes,
operating procedure, contest exchange, etc.) see the upstream
documentation at [lz2hv.org](https://lz2hv.org/) — that part is
identical across Linux, Windows, and macOS.

If something here doesn't match what you see, [open an issue](../../issues).

---

## Recommended setup (Apple Silicon)

The path the maintainer uses every day, tested end-to-end:

> **MSHV-Mac** ← *TCI (CAT + audio)* → **AetherSDR**
>     ↓ *WSJT-X UDP broadcast*
> **RUMlogNG** (logging + DXSpots)

- **AetherSDR** runs the radio (FlexRadio 6000-series via TCI bridge,
  or any other rig AetherSDR supports). One TCI WebSocket carries
  both **frequency/mode/PTT control AND the RX/TX audio streams** —
  no separate audio device, no DAX virtual cable, no loopback. This
  is the cleanest option on macOS and the one to start with.
- **RUMlogNG** receives MSHV's WSJT-X-compatible UDP broadcast on
  port 2237 and logs decodes/QSOs to its DXSpots window.
- Microphone permission: not required on the TCI path — audio comes
  over the WebSocket, not through the system mic.

If you don't have AetherSDR, the same TCI flow works against any
software offering a TCI server (FlexRadio's SmartSDR, etc.). If
you're using a USB sound-card rig, see "Audio setup" below.

---

## First launch on macOS

The release bundles are **signed with an Apple Developer ID and
notarised by Apple**. You should see at most the standard *"This
app was downloaded from the internet — are you sure you want to
open it?"* prompt on first open. Click **Open**. Subsequent
launches: normal double-click, no prompts.

If the binary is much older than the macOS version (rare —
notarised builds usually keep working), or you've copied the .app
through a sync agent that stripped the staple:
- **System Settings → Privacy & Security → "Open Anyway"** for
  MSHV after a refused launch, or
- Terminal: `xattr -dr com.apple.quarantine /Applications/MSHV.app`
  to strip the quarantine attribute manually.

---

## Pick the right binary

| Mac type | Download | Native? |
|---|---|---|
| **Apple Silicon** (M1, M2, M3, M4) | [`MSHV-Apple-Silicon.app.zip`](../../releases) | Yes |
| **Intel** (older Macs with Intel CPUs) | [`MSHV-Intel-Mac-x86.app.zip`](../../releases) | Yes |
| Apple Silicon, only the Intel zip available | `MSHV-Intel-Mac-x86.app.zip` | Via Rosetta 2 — slower, but functional |

Not sure which you have? Apple menu → **About This Mac** → check
"Chip" (M-series → Apple Silicon) or "Processor" (Intel ... →
Intel).

---

## Where your data lives

User-mutable state lives in `~/Library/Application Support/MSHV/` —
NOT inside the app bundle. This means:

- **Updating** by drag-replacing `MSHV.app` does not wipe your
  callsign, QSO log, or any other state.
- **Switching** between Apple Silicon and Intel builds shares the
  same Library state.

```
~/Library/Application Support/MSHV/
├── settings/
│   ├── ms_settings        ← geometry, audio devices, mode, dialogs
│   ├── ms_macros          ← callsign, grid, macros
│   ├── ms_mesages
│   ├── ms_start
│   ├── ms_stinfonet       ← UDP broadcast / per-band antenna /
│   │                        DAX TX buffer
│   └── database/          ← cty.dat, msloc_db, msbcn_db, mstn_db
├── log/mshvlog.edim       ← QSO log
├── AllTxtMonthly/         ← ALL_YYYY_MM.TXT — running text log
├── ExportLog/             ← ADIF / Cabrillo exports
├── RxWavs/                ← recorded audio
└── Screenshots/           ← saved waterfall PNGs
```

**Back up** before a major change:
```
DATE=$(date +%Y%m%d-%H%M)
cp -R "$HOME/Library/Application Support/MSHV" \
      ~/Desktop/mshv-backup-$DATE/
```

**Factory defaults** — quit MSHV, then:
```
mv "$HOME/Library/Application Support/MSHV" \
   "$HOME/Library/Application Support/MSHV.bak"
```
Next launch creates a fresh `MSHV/` from the bundle's seed. Your
old state is preserved at `MSHV.bak/` — restore by reversing the
move.

---

## Audio setup

**The TCI path is recommended** — see "Recommended setup" above.
This section covers everything else.

### Device picker

`Options → Settings → Audio` shows two dropdowns: input (RX) and
output (TX). The list comes from CoreAudio and includes:

- Built-in microphone / speakers
- USB sound cards (TI PCM2900, CM108, generic class-compliant)
- Virtual devices: BlackHole, Loopback, Soundflower, Existential
  Audio
- FlexRadio's DAX TX/RX channels (when DAX is running)
- AetherSDR's CommonRadioAudio In / Out (when its virtual audio
  is enabled — note: TCI is preferable to this)

Pick devices that match the bit depth and sample rate your
hardware supports. MSHV handles 16-bit, 24-bit, and 32-bit PCM
input/output; the device picker remembers your choice across
launches.

### 24-bit audio

Verified end-to-end against AetherSDR's CommonRadioAudio (24-bit)
for both RX and TX. If you switch from 16-bit to 24-bit and the
waterfall pegs red or saturates, restart MSHV — some devices need
the stream re-opened with the new format.

### Microphone permission

If MSHV's audio input is set to **anything that's a real
microphone** (built-in, USB sound card, BlackHole patched from a
real mic), macOS will prompt for microphone access on first audio
capture. The prompt comes from `NSMicrophoneUsageDescription` in
the bundle's `Info.plist` and the `audio-input` entitlement.

If denied accidentally:
- **System Settings → Privacy & Security → Microphone** → enable MSHV.

The TCI audio path doesn't trigger this prompt because the audio
comes over the WebSocket, not through the system microphone API.

---

## Rig control on Mac

### TCI (recommended)

`Settings → CAT → TCI` — paste the TCI server URL (typically
`ws://127.0.0.1:50001` for AetherSDR, or whatever your TCI server
exposes). MSHV will:

1. Auto-connect on launch — or click **Connect** manually.
2. Receive frequency, mode, slice info, PTT state from the TCI
   server.
3. Send PTT, frequency, and mode commands back.
4. **Plus** stream the RX audio from the TCI server and the TX
   audio back to it on the same WebSocket — no separate audio
   device required.

If TCI doesn't connect:
- Verify the URL (`ws://` not `http://`, correct port).
- Check the TCI server is running (AetherSDR's TCI feature must
  be enabled in its settings).
- Some TCI servers require the radio to be powered on / SmartLink
  active before they accept connections. MSHV retries up to 20
  times at startup with backoff.

### CAT serial

For non-TCI rigs (USB-CAT cables to Yaesu, Icom, Kenwood, etc.):
`Settings → CAT → Serial` and pick the device. The dropdown lists
`/dev/tty.usbserial-*`, `/dev/tty.SLAB_USBtoUART`,
`/dev/tty.usbmodem*`, etc. Match the rig's CAT baud rate (rig's
menu) to MSHV's setting.

If your USB-CAT cable doesn't show up:
- Some cables (Silicon Labs CP210x, FTDI) need the manufacturer's
  driver. macOS Sequoia and later usually have the driver built in;
  older releases may not.
- `ls /dev/tty.*` in Terminal to confirm the OS sees the device.
- If the cable is shared with a different program (WSJT-X, RUMlog
  controlling the rig), you'll need a CAT splitter or to run
  through `flrig` / `rigctld`.

---

## External logger / spotter integration

### RUMlogNG (recommended)

`Options → Settings → Misc → UDP broadcast` — enable, set:
- IP: `127.0.0.1`
- Port: `2237` (RUMlogNG's default — verify in RUMlogNG's
  preferences)
- Identify as: `WSJT-X MSHV`

RUMlogNG's DXSpots window will start showing decoded text. QSO
Logged messages also flow through (logged contacts auto-populate
RUMlogNG's QSO entry).

### Other loggers

The same UDP broadcast feeds **JTAlert** (via a port forwarder),
**MacLoggerDX**, **N1MM+** (over the network), or anything that
speaks WSJT-X's UDP protocol. Check the receiver's expected port.

### PSK Reporter / RBN

`Options → Settings → Misc → PSK Reporter` — enable. Decodes are
uploaded to [pskreporter.info](https://pskreporter.info).

---

## Common issues

### "MSHV cannot be opened because Apple cannot check it for malicious software"

You're on a notarised build whose staple was stripped (rare — happens
when a sync agent re-wraps the .app). Run:
```
xattr -dr com.apple.quarantine /Applications/MSHV.app
```
Or right-click the .app → **Open** → confirm.

### "App is damaged and can't be opened"

Same root cause. Same fix. Don't redownload — the staple is fine,
the local quarantine attribute is the problem.

### No audio devices in the dropdown

- Ensure MSHV has microphone permission (System Settings → Privacy &
  Security → Microphone).
- Quit and relaunch. Audio devices are enumerated at startup.
- If using BlackHole / Loopback, confirm the virtual device is
  installed and visible in **Audio MIDI Setup**.

### TCI client not connecting

- Verify TCI server URL and port.
- Confirm the TCI server is running and accepting connections.
- AetherSDR-specific: ensure TCI is enabled in AetherSDR's
  preferences and the radio is on.
- Check `~/Library/Application Support/MSHV/log/` and console.app
  filtered to `MSHV` for connection errors.

### Crash on launch

Capture the crash report from **Console.app → Crash Reports**
(filter by `MSHV`), then [open an issue](../../issues) attaching:
- The full crash report (text)
- Your macOS version (Apple menu → About This Mac → version)
- Your Mac's chip (Apple Silicon vs Intel)
- Whether you launched the Apple Silicon or Intel binary

### CPU widget shows wrong / weird values

Per-CPU usage uses `host_processor_info(PROCESSOR_CPU_LOAD_INFO)`
which doesn't label P-cores vs E-cores on M-series. They appear as
N independent cores. Cosmetic only — the underlying scheduler
manages the cores correctly.

### FT8 cursors snap back to narrow when I click the waterfall

Should be fixed in v2.76.6-mac1 onwards — the AP-decode tolerance
window was hardcoded to a narrow 200..3200 Hz in upstream. The Mac
build now scales it to the full 0..4000 Hz spectrum.

---

## Reporting bugs

[Open a GitHub issue](../../issues/new) and include:

- macOS version (Apple menu → About This Mac)
- Mac chip (M-series or Intel)
- MSHV version and which zip you downloaded
- What you were doing when the bug appeared
- Crash report from **Console.app → Crash Reports** if applicable
- Whether you can reproduce it consistently

For audio / TCI issues: a dump of `Settings → Audio` and `Settings
→ CAT → TCI` (sanitised — remove call signs / passwords) helps a
lot.

---

## Updating to a new version

1. Download the new release from the
   [Releases page](../../releases).
2. Quit MSHV (Cmd-Q).
3. Drag the new `MSHV.app` to `/Applications/`. Confirm "Replace"
   when prompted.
4. Launch.

Your settings, log, and screenshots are in
`~/Library/Application Support/MSHV/` — untouched by the bundle
swap.

If a new version misbehaves, downgrade:

1. Download the prior version from the Releases page.
2. Drag-replace `/Applications/MSHV.app`.
3. Launch.

The settings format is forward-compatible across point releases —
older versions silently ignore newer fields they don't understand.

---

## Building from source

For developers / power users:

```
brew install qt@5 portaudio fftw
git clone https://github.com/vu2cpl/mshv-macos-port.git
cd mshv-macos-port
/opt/homebrew/opt/qt@5/bin/qmake CONFIG+=sdk_no_version_check MSHV_macOS.pro
make -j8
make standalone   # creates the self-contained .app
```

For the full build / release / notarise workflow including how to
produce both Apple Silicon and Intel binaries, see
[`MACOS_PORT_README.md`](MACOS_PORT_README.md) and
[`DEVELOPING.md`](DEVELOPING.md).
