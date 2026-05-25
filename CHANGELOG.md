# Changelog

All notable changes to LoopMidi are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## [0.2.0] — 2026-05-25

### Added
- App icon (purple loop arrow with MIDI note bars, dark background).
- `Ctrl+Q` keyboard shortcut to quit the application.
- BPM control in the transport bar: `−`/`+` buttons (step ±5) and scroll-wheel on the value (step ±1), usable during playback.
- MIDI device hotplug detection: input and output port lists update automatically every second when devices are connected or disconnected. No manual refresh needed.

### Fixed
- Record, Play, and Clear transport buttons showed a duplicate icon (icon appeared in both `iconText` and inside the `label` string). Labels are now icon-free; `iconText` is the single icon source.
- `Ctrl+Q` now reliably quits by calling `QGuiApplication::quit()` via a context property instead of `Qt.quit()` which was silently ignored.
- `flake.nix` meta now declares `licenses.gpl3Only` instead of the incorrect `licenses.mit`.

### Changed
- Manual "Refresh Ports" button removed from the MIDI input row — hotplug detection makes it redundant. The sidebar "Refresh Ports" button is retained for manual use.
- License updated from MIT to GNU General Public License v3 (GPLv3).

---

## [0.1.0] — 2024-01-01

### Added
- Initial release of LoopMidi.
- 16-step MIDI note sequencer.
- Virtual MIDI output port ("LoopMidi Output") visible to Surge-XT and other DAWs.
- Live MIDI passthrough from input keyboard to virtual output.
- Adjustable BPM (40–240) with smooth slider.
- Auto-start playback after 16 notes recorded.
- MIDI Learn for Record, Play, Stop, Clear actions (CC or note).
- Hardware MIDI input/output port selector with refresh.
- Keyboard shortcuts: `R` record, `Space` play/stop, `C` clear, `Esc` stop all.
- Dark QML UI with step grid, velocity bars, animated step indicator, live note visualizer.
- Nix flake build with version injected via CMake.
- GitHub Actions CI workflow (Ubuntu).
