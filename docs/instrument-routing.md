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
2. Start Carla or another plugin host.
3. Add one Surge-XT plugin instance per LoopMidi track.
4. Connect `LoopMidi Output` to the host's MIDI input.
5. Route MIDI channel 1 to the first Surge-XT instance, channel 2 to the second, and so on.
6. Pick a different preset in each Surge-XT instance.
7. In LoopMidi, select each track and choose its MIDI channel from the track controls.

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

The current implementation covers step 1. It keeps LoopMidi focused as a sequencer while enabling different instruments immediately through an external host.
