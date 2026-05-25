# LoopMidi

**LoopMidi** is a Qt/QML MIDI loop sequencer. It records a session of up to 16 notes from any MIDI keyboard (e.g. Arturia MiniLab 3) and plays them back as a continuous loop through a **virtual MIDI port** that any DAW or synthesizer (e.g. Surge-XT) can subscribe to.

---

## Features

| Feature                 | Details                                                                            |
| ----------------------- | ---------------------------------------------------------------------------------- |
| **16-step recorder**    | Play up to 16 notes; recording stops automatically and playback starts             |
| **Virtual MIDI output** | Exposes "LoopMidi Output" — connect Surge-XT or any app to it                      |
| **Passthrough**         | Notes from your keyboard are forwarded live to the virtual port while playing/idle |
| **Adjustable BPM**      | 40 – 240 BPM slider for playback speed                                             |
| **MIDI Learn**          | Bind Record / Play / Stop / Clear to any CC knob or pad on your controller         |
| **Keyboard shortcuts**  | `R` record, `Space` play/stop, `C` clear, `Esc` stop all                           |
| **Beautiful dark UI**   | Step grid with velocity bars, live note visualizer, animated indicators            |

---

## Quick Start (Nix)

```bash
# Run directly
nix run github:yourusername/loopmidi

# Or build
nix build github:yourusername/loopmidi
./result/bin/loopmidi
```

### Development shell

```bash
nix develop
cmake -B build -DAPP_VERSION=0.1.0
cmake --build build -j$(nproc)
./build/loopmidi
```

---

## Setup with Arturia MiniLab 3 + Surge-XT

1. **Start LoopMidi** — a virtual port named `LoopMidi Output` appears.
2. **Select input port** — choose `Arturia MiniLab 3` in the left sidebar.
3. **Open Surge-XT** → MIDI settings → subscribe to `LoopMidi Output`.
4. Enable **Passthrough** (on by default) so live notes go straight to Surge-XT.
5. Press **Record** (or `R`) and play 16 notes on the MiniLab.  
   Recording stops automatically; playback begins immediately.
6. Surge-XT will receive the looping notes and play them back.

### MIDI Learn (MiniLab 3 pads / knobs)

1. Click the `◉` button next to "Record", "Play", "Stop", or "Clear".
2. Move a knob or press a pad on the MiniLab 3.
3. The CC number is captured and shown. That control now triggers the action.

---

## Architecture

```
Arturia MiniLab 3
      │  (RtMidi input)
      ▼
  MidiEngine (C++)
      │ passthrough / sequencer
      ▼
  LoopMidi Output  ◄── virtual ALSA MIDI port
      │
      ▼
  Surge-XT (or any DAW)
```

---

## Building manually (non-Nix)

Requirements: Qt 6, rtmidi, ALSA (Linux), CMake ≥ 3.20.

```bash
cmake -B build -DAPP_VERSION=0.1.0
cmake --build build -j$(nproc)
```

---

## License

MIT © 2024 — see [LICENSE](LICENSE).
