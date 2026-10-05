# Celadon

A retro soundfont synthesizer. Load a `.sf2` cart, play it like a polysynth.
VST3 + AU + Standalone, Apple Silicon macOS, JUCE 8.
Fourth instrument in the lineup after Tessera, Atrium and Strata.

The interface is a modern take on the classic grey handheld: dot-matrix
screen in four greens showing the real output waveform, one burgundy accent,
A/B buttons flipping every knob between its two functions. The research
dossier behind the engine and the design is in `docs/RESEARCH.md`.

## Build

```
./build.sh            # checks tools, fetches pixel fonts, builds, installs VST3 + AU
./make-installer.sh   # -> dist/Celadon Installer.pkg (to share)
```

First build downloads JUCE (needs internet) and takes a few minutes.
After every rebuild: quit and reopen Ableton — it caches plugin UIs.
First install: Ableton Settings > Plug-Ins > Rescan.

## The instrument in one paragraph

Celadon reads the soundfont's own articulation — key/velocity maps, loop
points, stereo pairs, per-zone envelopes, filters and LFOs, tuning, pan,
exclusive-class chokes — and plays it through a 16-voice engine with 4-point
interpolation (FluidSynth-default quality class). On top sits the synth
layer: ADSR knobs that scale the cart's own envelopes (centre = as the maker
intended), a resonant filter offset with its own mod envelope, a global LFO
aimed at pitch, filter or amp, glide, tune, velocity sensitivity, stereo
width, and a retro FX chain — CRUSH, CHORUS, DELAY, REVERB.

## The cart

Click the cartridge at the top to load any `.sf2`. The `<` `>` buttons and
MIDI program change step presets; clicking the preset name on the screen
opens the full list. The loaded cart path and preset are saved with your
project. Start with `Pokemon_RSE_HD_Soundfont_Release.sf2` — all 24 presets
are covered by the test suite.

## The knobs

Every knob has two functions. Press **B** (or click a knob's small caption)
to flip the whole panel to ALT — readouts turn burgundy. **A** brings you
back. Primary and ALT are separate automatable parameters; flipping never
touches your values.

| Knob | MAIN | ALT (B) |
|---|---|---|
| ATTACK | scale cart attack ×0.25–×4 | MOD-ENV attack (ms) |
| DECAY | scale cart decay | MOD-ENV decay |
| SUSTAIN | sustain offset ±24 dB | MOD-ENV sustain |
| RELEASE | scale cart release | MOD-ENV release |
| GLIDE | portamento 0–500 ms | VEL SENS 0–200 % |
| TUNE | ±12 semitones | WIDTH 0–200 % |
| CUTOFF | shift all zone filters ±4 oct | ENV AMT ±4 oct (mod-env → filter) |
| RESONANCE | +0–24 dB peak | KEYTRACK 0–100 ct/key |
| RATE | LFO speed (Hz, or division when SYNC) | LFO DELAY 0–2 s |
| DEPTH | LFO amount | TARGET pitch / filter / amp |
| CRUSH | bit-depth morph 16→3 | crush RATE (downsampling) |
| CHORUS | mix | chorus RATE |
| DELAY | mix (tempo-synced echo) | TIME division |
| REVERB | mix | SIZE |

Around the shell: **VOLUME** wheel on the left edge, **CONTRAST** wheel on
the right (screen palette: DMG green / Pocket grey / backlit — saved with
the project), **GUIDE** for the full overlay, **PANIC** to silence
everything, and a red LED that blinks on MIDI.

## MIDI map

CC 20–47 → Attack, Decay, Sustain, Release, Cutoff, Resonance, Env Amount,
Keytrack, LFO Rate, LFO Depth, LFO Delay, Glide, Tune, Vel Sens, Width,
Crush, Crush Rate, Chorus, Chorus Rate, Delay Mix, Reverb Mix, Reverb Size,
Volume, ModEnv A, ModEnv D, ModEnv S, ModEnv R, Delay Feedback.
CC 102–105 → LFO Target, LFO Sync, LFO Division, Delay Division.
CC 64 sustain pedal · CC 120/123 all notes off · program change → preset ·
pitch bend ±2 semitones.

## Macro suggestions (Ableton rack)

1. Cutoff — the one you'll ride
2. Attack
3. Release
4. LFO Depth
5. Crush
6. Delay
7. Reverb
8. Glide

## Tests

```
cd tests
g++ -O2 -std=c++17 test_dsp.cpp -o celadon_test && ./celadon_test
```

82 assertions, all behavioral: SF2 unit-conversion math, envelope timing
against the spec, pitch accuracy within cents, loop continuity at the seam,
loop-until-release, exclusive-class chokes, polyphony pressure, sustain
pedal, FX transparency at zero, delay echo timing, reverb decay vs size —
plus an integration pass that loads the real Pokemon cart and confirms all
24 presets sound. The test binary finds the cart at `../../` automatically.

## Honesty box (v1 limits)

- SF2 *modulators* (pmod/imod) aren't interpreted yet — generators are.
  Your cart contains zero modulators, so nothing is lost for it. The
  default velocity→attenuation modulator is built in (scaled by VEL SENS).
- Per-zone chorus/reverb *sends* are parsed but not yet routed; the FX
  knobs act as master effects.
- Pitch bend range is fixed at ±2 semitones.

## Fonts

Press Start 2P, Silkscreen and VT323, all SIL OFL 1.1 (free to embed);
`build.sh` fetches them from the google/fonts repository into `Assets/`.
Without them the plugin falls back to system fonts.

## Sharing

Send `dist/Celadon Installer.pkg`. Needs an Apple Silicon Mac. Unsigned, so
macOS blocks the first open: System Settings → Privacy & Security → Open
Anyway. Or: `xattr -d com.apple.quarantine "Celadon Installer.pkg"`.
AirDrop usually avoids the quarantine flag; email and messaging add it.
Remind them to bring their own `.sf2` — the cart is not inside the plugin.
