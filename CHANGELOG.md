# Changelog

All notable changes to LoopMidi are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## [0.2.0] — 2026-05-25

### Added
- App icon (purple loop arrow with MIDI note bars, dark background).
  - Set as `QIcon` in `main.cpp` and via `Window { icon.source }` in QML for full window-manager coverage.
  - Installed to `share/icons/hicolor/256x256/apps/loopmidi.png` in the Nix package.
  - `.desktop` file installed to `share/applications/loopmidi.desktop` so app launchers display the icon.
- `Ctrl+Q` keyboard shortcut to quit the application.
- BPM control in the transport bar: `−`/`+` buttons (step ±5) and scroll-wheel on the value (step ±1), usable during playback.
- MIDI device hotplug detection: input and output port lists update automatically every second when devices are connected or disconnected. No manual refresh needed.
- `QSettings` persistence: BPM, selected input/output port (stored by name, not index), and MIDI Learn bindings are saved on every change and restored on next launch. Ports are matched by name against the live device list, so the correct device is reopened even if its index shifts.
- Polyphonic sequencer: the sequencer now records and replays chords (multiple simultaneous notes). Note-On events arriving within a 30 ms window are grouped into the same step. During playback, all notes in a step are sent simultaneously and stopped together on the next tick. Step cells show a `×N` badge when the step contains a chord.

### Fixed
- Record, Play, and Clear transport buttons showed a duplicate icon (icon appeared in both `iconText` and inside the `label` string). Labels are now icon-free; `iconText` is the single icon source.
- `Ctrl+Q` now reliably quits by calling `QGuiApplication::quit()` via a context property instead of `Qt.quit()` which was silently ignored.
- `flake.nix` meta now declares `licenses.gpl3Only` instead of the incorrect `licenses.mit`.
- `SectionLabel.qml` property `text` shadowed the `Text` base type's `text` property, causing binding warnings. Renamed to `labelText`.
- `StatusPill.qml` and `LoopButton.qml` bare property references resolved by adding `id: root` and using qualified `root.*` names.
- Replaced `resources.qrc` + `CMAKE_AUTORCC` with `qt_add_qml_module` (URI `LoopMidiUI`) — eliminates all "qmlRegisterType requires absolute URLs" warnings.
- Added `qt_policy(SET QTP0004 NEW)` and `qml/qmldir` to silence remaining CMake QML warnings.
- Fixed `nix run` crash where QML was not found: `qt_add_qml_module` embeds QML at `qrc:/LoopMidiUI/qml/main.qml`; `main.cpp` load URL updated accordingly.
- Hotplug: if the currently-selected port disappears, it is closed and the selection cleared. On next hotplug scan, the saved port name is matched against the new list and the port is automatically reopened.
- Port ComboBoxes no longer reset the selected device on model refresh. The binding was changed from `onCurrentIndexChanged` (fires on all changes including programmatic) to `onActivated` (user gesture only), with `Connections` syncing the index back from the engine.
- MIDI callback thread safety: `QTimer::start()` and signal emissions from the RtMidi callback thread are now marshalled to the main thread via `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`, fixing recording getting stuck on step 0.
- `nix run` taskbar icon: on first launch the `.desktop` file and PNG icon are copied to `~/.local/share`, allowing `xdg-desktop-portal` to resolve the app ID and display the icon in KDE/Wayland taskbars.

### Changed
- Manual "Refresh Ports" button removed from the MIDI input row — hotplug detection makes it redundant.
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
