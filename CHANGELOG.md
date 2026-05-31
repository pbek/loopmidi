# Changelog

All notable changes to LoopMidi are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [Unreleased]

### Added

- Project save/load support via `.loopmidi` JSON files, including project name, BPM, record mode, active track, and all sequenced notes across all tracks.
- New projects now default to a dated name, and Save As pre-fills the filename from the current project name.
- Per-track MIDI channel routing lets each track target a separate instrument in Carla, a DAW, or another plugin host.
- Internal instrument rack model with one Surge-XT-ready plugin slot per track, including plugin format, plugin ID, preset label, enabled state, and project/settings persistence.
- Plugin discovery scans LV2, CLAP, and VST3 paths and lets each track select an available plugin from the instrument rack UI.
- Process-backed LV2 host controls can launch one `jalv`/`jalv-qt` plugin host instance per enabled instrument slot.
- Plugin host instances now use stable per-track JACK client names and can auto-connect detected audio outputs to system playback ports.
- LV2 hosts now launch headless by default to avoid Surge-XT's native LV2 UI crashing under X11/Wayland, and plugin discovery reads LV2 `seeAlso` metadata to distinguish Surge XT from Surge XT Effects.
- Plugin host auto-routing now connects only stereo outputs 1/2 to playback and connects `LoopMidi Output` MIDI to hosted plugin inputs.
- Per-track Surge-XT program selection writes generated LV2 state and starts each hosted instance with its configured program number.
- Surge-XT factory and third-party `.fxp` patches are scanned into a named per-track patch selector; selected patches are converted into generated LV2 state and loaded by each headless `jalv` instance.
- Surge patch scanning is deferred until after startup and selected patch lookup is handled in C++ to keep track switching responsive with large patch libraries.

### Fixed

- Process-backed plugin hosting now runs through PipeWire-JACK via `pw-jack` when available, so `jalv`, `jack_lsp`, and `jack_connect` use the active PipeWire audio graph instead of expecting a separate classic JACK server.
- Plugin host diagnostics now surface `jalv` output and exit reasons in the terminal and app error banner instead of silently switching from running to stopped.
- Auto-routing no longer connects optional Surge-XT outputs 3-6 into the right playback channel, which could pollute the desktop audio graph.
- Surge patch selection no longer rebuilds the full patch model on track/channel changes, and patch scanning is limited to a practical factory subset to avoid UI stalls.
- Hosted instruments now use one virtual MIDI output per track, so active-track audition and sequencer playback target the matching Surge-XT instance instead of sending every note to every hosted synth.
- Shared `LoopMidi Output` passthrough is muted while the internal plugin host is running, and stale shared MIDI links are disconnected from hosted synth inputs before per-track links are made.
- Surge `.fxp` patch loading now reads the correct VST2 chunk header, so selected factory patches are written into LV2 state instead of falling back to ineffective numeric program values.

---

## [0.3.0] — 2026-05-26

### Added

- Tap Tempo metronome button: tap a beat in the transport bar to set BPM from the average interval of recent taps.
- MIDI Learn support for Tap Tempo, including learned CC buttons and note/pad buttons.
- Multi-track sequencing with four selectable recording targets. The visible step grid edits the active track, while playback layers all tracks together.
- Recording mode switch for choosing between sequential all-beats recording and sparse current-beat overdubbing while playback is running.

### Fixed

- Learned MIDI note/pad bindings now trigger transport actions instead of only being accepted during learning.
- Current-beat recording now targets the displayed playback cursor step instead of the previously played beat.

---

## [0.2.0] — 2026-05-25

### Added

- **Manual cursor placement**: left-click any step cell to place a teal recording cursor on that step. Clicking the same cell again clears the cursor. The cursor is shown with a teal border, teal dot indicator, and teal text; the status bar displays "CURSOR — step N (click again to clear)".
- **Cursor-aware recording**: the Record button and `R` shortcut no longer wipe the entire sequence before recording. Instead, recording starts from the manually-placed cursor step if one is set, or automatically from the first empty step if no cursor is set. If all steps are filled and no cursor is placed, recording starts from step 0 (overwrite from the beginning). The cursor is consumed (cleared) when recording starts.
- **Per-step delete**: right-click any step cell → "Delete step" clears that single step without affecting the rest of the sequence. Playback continues; that step becomes silent on its next pass.
- **Per-step re-record**: right-click any step cell → "Re-record step" arms just that step for re-recording. The step is pre-cleared, the REC indicator lights up, and the next chord played (within the 30 ms chord window) overwrites only that step. Recording stops automatically after the chord is committed; playback is uninterrupted.
- `MidiEngine::clearStep(int index)` — public slot callable from QML.
- `MidiEngine::recordStep(int index)` — arms a single step for chord capture.
- `MidiEngine::setCursorStep(int index)` — sets or clears the manual cursor; pass `-1` to revert to auto mode.
- `cursorStep` Q_PROPERTY (read-only, notifies `cursorStepChanged`) exposed to QML.
- `stepRecordTarget` Q_PROPERTY exposed to QML (indicates which step is being re-recorded, `-1` when none).
- App icon (purple loop arrow with MIDI note bars, dark background).
  - Set as `QIcon` in `main.cpp` and via `Window { icon.source }` in QML for full window-manager coverage.
  - Installed to `share/icons/hicolor/256x256/apps/loopmidi.png` in the Nix package.
  - `.desktop` file installed to `share/applications/loopmidi.desktop` so app launchers display the icon.
- `Ctrl+Q` keyboard shortcut to quit the application.
- BPM control in the transport bar: `−`/`+` buttons (step ±5) and scroll-wheel on the value (step ±1), usable during playback.
- MIDI device hotplug detection: input and output port lists update automatically every second when devices are connected or disconnected. No manual refresh needed.
- `QSettings` persistence: BPM, selected input/output port (stored by name, not index), and MIDI Learn bindings are saved on every change and restored on next launch. Ports are matched by name against the live device list, so the correct device is reopened even if its index shifts.
- Polyphonic sequencer: the sequencer now records and replays chords (multiple simultaneous notes). Note-On events arriving within a 30 ms window are grouped into the same step. During playback, all notes in a step are sent simultaneously and stopped together on the next tick. Step cells show a `×N` badge when the step contains a chord.

### Changed

- Manual "Refresh Ports" button removed from the MIDI input row — hotplug detection makes it redundant.
- License updated from MIT to GNU General Public License v3 (GPLv3).
- `startRecording()` no longer calls `clearSequence()`. Existing notes in steps before the start position are preserved.
- `StepCell.qml` now handles both left-click (cursor placement) and right-click (context menu) via a unified `MouseArea`. Accepts `Qt.LeftButton | Qt.RightButton`.
- Step cell visual priority order: recording-current > playback-current > cursor > has-note > empty.
- Status bar text updates to reflect cursor state when the engine is stopped.

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
