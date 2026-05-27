# Instrument Routing And Surge-XT Hosting

LoopMidi currently acts as a MIDI sequencer. It records notes into four internal tracks and sends playback through the virtual MIDI port named `LoopMidi Output`.

That virtual MIDI port can carry all 16 MIDI channels, but a single standalone Surge-XT instance normally behaves as one instrument. Different LoopMidi tracks only become different instruments when the receiving host routes separate MIDI channels or ports to separate synth instances.

## Current Practical Routing

The first direct step inside LoopMidi is per-track MIDI channel routing:

| LoopMidi track | Default MIDI channel | Typical host routing            |
| -------------- | -------------------: | ------------------------------- |
| Track 1        |                    1 | Surge-XT bass instance          |
| Track 2        |                    2 | Surge-XT pad instance           |
| Track 3        |                    3 | Surge-XT lead instance          |
| Track 4        |                    4 | Surge-XT pluck or keys instance |

LoopMidi still outputs one virtual MIDI port, but playback forces each track onto its configured output channel. A plugin host such as Carla, Ardour, Reaper, Bitwig, or another DAW can then subscribe to `LoopMidi Output` and route each channel to a different instrument.

## Recommended Setup With Carla

1. Start LoopMidi.
2. Select a track.
3. Press `Scan` in the instrument slot row if the plugin list is empty.
4. Choose `Surge XT (LV2)`, `Surge XT (CLAP)`, or `Surge XT (VST3)` from the plugin selector.
5. Start Carla or another plugin host.
6. Add one Surge-XT plugin instance per LoopMidi track.
7. Connect `LoopMidi Output` to the host's MIDI input.
8. Route MIDI channel 1 to the first Surge-XT instance, channel 2 to the second, and so on.
9. Pick a different preset in each Surge-XT instance.
10. In LoopMidi, select each track and choose its MIDI channel from the track controls.

This gives separate instruments without requiring LoopMidi to become an audio plugin host yet.

## Native Surge-XT Hosting

LoopMidi can eventually load Surge-XT directly as a plugin, but that is a larger architectural change. Surge-XT is available as LV2, VST3, and CLAP on Linux, so native hosting would require LoopMidi to manage plugin discovery, plugin instantiation, real-time audio rendering, plugin state, presets, audio output, buffer size, sample rate, latency, and possibly embedded plugin UIs.

A future internal architecture could look like this:

```text
LoopMidi
  Track 1 -> Surge-XT instance 1 -> audio mixer
  Track 2 -> Surge-XT instance 2 -> audio mixer
  Track 3 -> Surge-XT instance 3 -> audio mixer
  Track 4 -> Surge-XT instance 4 -> audio mixer
                                  -> JACK/PipeWire/ALSA output
```

## Implementation Roadmap

1. Add per-track MIDI channel routing inside LoopMidi.
2. Use Carla or a DAW as the external instrument rack.
3. Add optional per-track virtual MIDI outputs if channel routing is not enough.
4. Add an internal instrument rack abstraction.
5. Implement native LV2 or CLAP hosting for Surge-XT and other synth plugins.

The current implementation covers step 1 and adds the step 4 data model. Each track now owns an instrument slot with a plugin format, plugin ID, preset label, and enabled state. The app scans common LV2, CLAP, and VST3 plugin paths plus `LV2_PATH`, `CLAP_PATH`, and `VST3_PATH`, then lets each track choose a discovered plugin. The default Nix wrapper exposes Surge-XT plugin paths so Surge-XT appears in the selector when running the packaged app. These rack settings are saved in `.loopmidi` projects and persisted in application settings.

The rack abstraction does not render audio or open plugin UIs yet. It gives the next LV2 or CLAP host layer a stable place to attach one real plugin instance per track without changing the sequencer or project format again.

## Opening Surge-XT Inside LoopMidi

Selecting `Surge XT` in the rack is not the same as opening the plugin. Opening it directly inside LoopMidi requires a plugin host engine. The next implementation step is to add an LV2 or CLAP host that can instantiate the selected plugin, feed each track's MIDI into its own plugin instance, render audio in a real-time callback, and show either a native plugin editor or a minimal internal editor.
