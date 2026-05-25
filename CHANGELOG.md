# Changelog

All notable changes to LoopMidi are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

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
