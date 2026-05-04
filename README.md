# MSHV — macOS port

Native macOS build of [MSHV](https://lz2hv.com/) by Hrisimir Hristov,
**LZ2HV**. All credit for MSHV itself goes to LZ2HV — this repository
is a Mac-only port that tracks his upstream releases.

Decodes and operates **FT8, FT4, FT2, MSK144, JT65 A/B/C, JT6M,
PI4, FSK441/315, ISCAT, Q65 A/B/C/D**, and MA (Multi-Answer) modes
on macOS 11 or later. Separate prebuilt binaries for Apple Silicon
(arm64) and Intel (x86_64) are attached to each release.

## Download

Pre-built bundles are attached to each release on the
[Releases page](../../releases) — no build tools required.

- `MSHV-Apple-Silicon.app.zip` — for **Apple Silicon** Macs (M1, M2,
  M3, M4 — released late 2020 onwards)
- `MSHV-Intel-Mac-x86.app.zip` — for **Intel** Macs (older Mac Pro /
  iMac / MacBook Pro / Mini / Air models with Intel processors)

Not sure which? Apple menu → **About This Mac** → look for
"Chip" (M-series → Apple Silicon) or "Processor" (Intel ... → Intel).

First launch on macOS:
- The bundle is signed with an Apple Developer ID and notarised by
  Apple, so Gatekeeper accepts it without complaint.
- On first open after download, macOS may show the standard *"This
  app was downloaded from the internet — are you sure you want to
  open it?"* prompt. Click **Open**. (This is normal for any app
  downloaded outside the App Store and isn't specific to MSHV.)
- Subsequent launches: normal double-click, no prompts.

User state (settings, callsign, QSO log, monthly text logs, recorded
WAVs, screenshots) lives in
`~/Library/Application Support/MSHV/`. Replacing the bundle with a
new release does **not** wipe your data.

## Build from source

Prerequisites (Apple Silicon — Homebrew at `/opt/homebrew`):

```
brew install qt@5 portaudio fftw
```

Build:

```
/opt/homebrew/opt/qt@5/bin/qmake CONFIG+=sdk_no_version_check MSHV_macOS.pro
make -j8
make standalone   # embeds Qt frameworks, signs the bundle
```

Output: `bin/MSHV.app`. Movable to `/Applications/`.

For the rebase workflow, conflict-likely files when LZ2HV releases a
new version, and architecture notes (PortAudio backend, Library state
migration, complex.h shim, 8 MB pthread stacks, etc.), see
[`MACOS_PORT_README.md`](MACOS_PORT_README.md).

For day-to-day development conventions (commit hygiene, where state
lives, the `_MACOS_` guard pattern), see
[`DEVELOPING.md`](DEVELOPING.md).

## License

GPL v3, inherited from [MSHV upstream](https://lz2hv.com/). See
[`COPYING.txt`](COPYING.txt).

## Credit

- **MSHV** itself — Hrisimir Hristov, **LZ2HV**, with permission to
  publish this Mac port.
- **WSJT-X authors** — Joe Taylor K1JT, Steven Franke K9AN, and
  contributors — for the original mode implementations MSHV builds
  on.
- **macOS port** — VU2CPL, with contributions from VU3ESV.

## Contributing

Mac-specific fixes welcome — bug reports, build issues on different
Mac configurations, audio device quirks. Feature work for the modes
themselves goes to LZ2HV upstream.

Issues and pull requests on this repo. Keep changes Mac-only or
guarded behind `#if defined _MACOS_` — see [`DEVELOPING.md`](DEVELOPING.md).
