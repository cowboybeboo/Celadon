# Research Dossier — Retro SoundFont Synth (plugin #4)

Compiled 2026-07-16, before any code. Every load-bearing claim carries a source URL.
Verification notes at the end. This file becomes `docs/RESEARCH.md` in the plugin folder.

---

## A. The soundfont on the bench: Pokemon RSE HD

Parsed directly (own Python RIFF parser, this session) from
`Pokemon_RSE_HD_Soundfont_Release.sf2` in the work folder:

- SoundFont version 2.01, built in Polyphone, by "Fromixty". 18.8 MB.
- **24 presets** (all bank 0 except one drumkit on bank 1): Drumset, Piano 1,
  Glockenspiel, Xylophone, Tubular-Bell, Organ 2, Accordion Fr., Nylon-str. Gt,
  Overdrive Gt, Fingered Bs., Fretless Bs., Slap Bass 1, Synth Bass 1,
  PizzicatoStr, Harp, Timpani, Strings, Trumpet, Tuba, French Horns,
  DistortionGt, Flute, Delay Shaku, FRLG Drumset.
- 24 instruments, **80 samples — every one of them half of a stereo pair**
  (40 left + 40 right), all 48 kHz, **78/80 have loop points**.
- **Zero modulators** (pmod and imod both empty) — generator-only file.
- 288 instrument generators, 24 preset generators — small, sane, friendly.

Consequences for the engine: stereo-pair voices and loop playback are
mandatory day one; SF2 modulator support is *optional* for this file (and can
ship later); one preset ≈ one instrument here, but the engine must still walk
the full preset→instrument→zone graph because other soundfonts will use it.

## B. SF2 format essentials

Primary source: **SoundFont Technical Specification 2.04** —
http://www.synthfont.com/sfspec24.pdf (also mirrored as HTML at
https://manualmachine.com/soundfon/version204/5410804-specification/).
Cross-checked against FluidSynth's implementation (links below).

### Structure

- One RIFF chunk, form `sfbk`, three LISTs in order: `INFO` (metadata),
  `sdta` (samples), `pdta` (articulation). Little-endian throughout.
- `sdta/smpl`: 16-bit signed LE mono samples. Stereo = **linked pairs of mono
  samples** cross-referenced via `wSampleLink`; pitch governed by the right
  sample's generators; pan splits them. Every sample is followed by ≥46 zero
  guard points (safe for multi-point interpolators).
- `pdta` = the "hydra", nine fixed-size record arrays, each with a terminal
  record: `phdr` 38 B, `pbag` 4 B, `pmod` 10 B, `pgen` 4 B, `inst` 22 B,
  `ibag` 4 B, `imod` 10 B, `igen` 4 B, `shdr` 46 B.
  (Verified against the real file — the sizes divide exactly.)

### Hierarchy and note-on

- **Preset → zones → instrument → zones → sample.** A zone = generator list +
  modulator list, with optional keyRange (gen 43) / velRange (gen 44) filters —
  keyRange must come first, velRange second if present.
- A **global zone** (first zone, no terminal instrument/sampleID generator)
  supplies defaults for all sibling zones.
- **Instrument-level generators are absolute** (replace defaults);
  **preset-level generators are relative** (added to instrument values);
  ranges intersect instead of adding. Clamp results to each generator's
  legal range.
- Note-on: resolve bank/program → preset; every preset zone whose ranges
  contain (key, vel) → its instrument; every matching instrument zone →
  **one voice each**. Overlaps are legal and are the layering mechanism
  (stereo pairs, velocity switches, splits). `exclusiveClass` (gen 57)
  chokes same-class voices (hi-hats).

### The generator model — what we must honor

Units and conversions (spec §2, §9; confirmed in FluidSynth
[fluid_conv.c](https://github.com/FluidSynth/fluidsynth/blob/master/src/utils/fluid_conv.c)):

| Unit | Meaning | Conversion |
|---|---|---|
| timecents | envelope/LFO-delay times | seconds = 2^(tc/1200); −12000 ≈ 1 ms, 0 = 1 s |
| absolute cents | pitch/frequency | Hz = 8.176 × 2^(cents/1200) |
| centibels | attenuation/resonance | dB = cB/10; gain = 10^(−cB/200) |
| 0.1 % units | pan, sends, mod-env sustain | value/10 = percent |

Key generators (defaults confirmed against
[fluid_gen.c](https://github.com/FluidSynth/fluidsynth/blob/master/src/synth/fluid_gen.c)):

- **Volume envelope** (gens 33–38): DAHDSR — delay, attack, hold, decay in
  timecents; sustain (37) = **cB of attenuation below peak** (0 = full);
  release (38) timecents. Attack is convex (linear in amplitude); hold/decay/
  release are linear in dB. keynumToVolEnvHold/Decay (39/40) scale times by
  key relative to key 60.
- **Modulation envelope** (gens 25–32): second DAHDSR, output 0–1, routed
  only to pitch (gen 7 `modEnvToPitch`, cents) and filter (gen 11
  `modEnvToFilterFc`, cents).
- **Filter** (gens 8/9): `initialFilterFc` in absolute cents, range
  1500–13500 (≈19 Hz–20 kHz, default open); `initialFilterQ` 0–960 cB
  (0–96 dB). Spec model: resonant **2-pole lowpass**; DC gain drops by Q/2
  (FluidSynth subtracts a further 3.01 dB so Q=0 is Butterworth —
  [fluid_iir_filter.c](https://github.com/FluidSynth/fluidsynth/blob/master/src/rvoice/fluid_iir_filter.c)).
- **LFOs**: modLFO (delay 21, freq 22) → pitch (5), filter (10), volume
  (13, in cB); vibLFO (delay 23, freq 24) → pitch only (6). Both **triangle**,
  start at zero after delay. Freq default 0 cents = 8.176 Hz; range ≈ 0.001 Hz
  to 110 Hz.
- **Pitch**: `overridingRootKey` (58) else shdr `byOriginalPitch`;
  `coarseTune` (51, semitones) + `fineTune` (52, cents) + shdr
  `chPitchCorrection`; `scaleTuning` (56, cents/key, 100 = normal, 0 = drums).
  Playback ratio = 2^(cents/1200) × sampleRate/engineRate.
- **Loops**: `sampleModes` (54): 0 no loop, 1 continuous loop, **3 loop until
  note-off then play out the tail**; mode 2 reserved (Polyphone abuses it —
  treat as no-loop). Loop end point is "equivalent to" loop start (loop length
  = end − start). Address offset gens (0–4, 45, 50) nudge start/end/loop points.
- **Other voice gens to honor**: `initialAttenuation` (13, cB — beware: legacy
  EMU hardware applied ~0.4×; FluidSynth follows the spec literally),
  `pan` (17, ±500 = hard L/R), sends (15/16), `keynum`/`velocity`
  substitutions (46/47), exclusiveClass (57).
- **Default modulators** (spec §8.4) apply even in modulator-free files —
  the two that audibly matter: velocity→attenuation (concave, 960 cB) and
  velocity→filterFc (−2400 cents). CC7/CC10/CC11/mod-wheel-vibrato defaults
  can be handled at our synth layer instead.

Compliance reference for later A/B testing:
https://github.com/mrbumpy409/SoundFont-Spec-Test (reference renderings of
envelopes/LFO/filter behavior).

## C. Engine decision

Options evaluated (full matrix in the session record):

| Option | License | Per-voice control | Verdict |
|---|---|---|---|
| **Own hydra parser + own JUCE voice engine** | none needed | total | ✅ **chosen** |
| TinySoundFont as parser only (MIT, single header) | MIT | total | fallback/reference — its hydra structs live inside `TSF_IMPLEMENTATION`, usable but not a stable public API ([tsf.h](https://github.com/schellingb/TinySoundFont)) |
| FluidSynth as engine | LGPL-2.1 | generator-space only — can't swap in our own filter/env/LFO DSP ([voice API](https://www.fluidsynth.org/api/group__voices.html), [LicensingFAQ](https://www.fluidsynth.org/wiki/LicensingFAQ)) | dev-only A/B reference, never shipped |
| SFZero (MIT, JUCE) | MIT | high | abandoned (JUCE 4-era) — architecture reference only |
| juicysfplugin | GPLv3 | — | precedent only, code off-limits |

Rationale for own parser: the hydra is nine fixed-size record types; this
session's 60-line Python prototype parsed the real file completely. The C++
version is a day's work, keeps us dependency-free, and the semantics work
(section B) has to happen regardless. MIT cribs if stuck:
[rustysynth](https://github.com/sinshu/rustysynth),
[meltysynth](https://github.com/sinshu/meltysynth),
[SF2Lib](https://github.com/bradhowes/SF2Lib) (best modulator math reference),
[TinySoundFont](https://github.com/schellingb/TinySoundFont).

TinySoundFont's renderer would be insufficient anyway: linear interpolation
only, basic biquad filter, no modulators, no sends (its own TODO list;
[issue #14](https://github.com/schellingb/TinySoundFont/issues/14)).

### Voice-engine DSP choices

- **Interpolation**: 4-point Catmull-Rom/Lagrange inline read — the quality
  class of FluidSynth's default `FLUID_INTERP_4THORDER`
  ([synthesis params](https://www.fluidsynth.org/api/group__synthesis__params.html))
  and sfizz's "polynomial" tier ([sample_quality](https://sfzformat.com/opcodes/sample_quality/)).
  JUCE's stock SamplerVoice is linear-only — not good enough for deep pitch
  shifts. The SF2 loop constraints (≥8 points before/after loops, 46 guard
  zeros) make ±4-point reads safe everywhere. Optional windowed-sinc HQ mode
  later if extreme transposition needs it (theory:
  [J.O. Smith, CCRMA](https://ccrma.stanford.edu/~jos/resample/resample.pdf)).
- **Filter**: [juce::dsp::StateVariableTPTFilter](https://docs.juce.com/master/classdsp_1_1StateVariableTPTFilter.html)
  (Zavalishin TPT, "designed for fast modulation") as the per-voice lowpass —
  but implemented as our own JUCE-free header equivalent so the DSP stays
  sandbox-testable, per playbook. LP/BP/HP at 12 dB/oct; drive stage optional.
- **Envelopes**: our own DAHDSR in a JUCE-free header — SF2-correct curve
  shapes (convex attack, linear-in-dB decay/release), initialized from the
  zone's generator values.
- 16 voices + stereo pairs = up to 32 sample streams; trivial CPU at 4-point
  interpolation. Voice stealing: quietest-in-release first, then oldest.

## D. Design language research (Game Boy, worn lightly)

### Colors

DMG screen "greenscale" (BGB-emulator-canonical hexes;
[designpieces.com](https://www.designpieces.com/palette/game-boy-original-color-palette-hex-and-rgb/)):
**#0F380F / #306230 / #8BAC0F / #9BBC0F** (dark → light). Screen is 160×144,
10:9, reflective STN LCD — green because unlit, "greenscale" not grayscale
([Wikipedia](https://en.wikipedia.org/wiki/Game_Boy)).

DMG hardware (community-sampled, no official values;
[palette A](https://www.color-hex.com/color-palette/1041682),
[palette B](https://www.color-hex.com/color-palette/100266),
[teardown](https://b13rg.icecdn.tech/Gameboy_DMG/)):
shell warm greige **#C4BEBB/#C2C0BC**; A/B buttons burgundy-magenta
**#9B2256**; d-pad/dark plastic **#272929**; indigo-violet accent **#484686**
(the wordmark + one bezel pinstripe — the famous pinstripe pair is magenta +
indigo, not blue + red); battery LED = saturated red (no canonical hex).
Game Boy Pocket greys: #FFFFFF/#A9A9A9/#545454/#000000
([palette](https://www.color-hex.com/color-palette/45300)). GBA (the
soundfont's home): landscape 240×160 3:2 form by Gwénaël Nicolas/Curiosity
Inc.; Indigo launch color ([Wikipedia](https://en.wikipedia.org/wiki/Game_Boy_Advance)).

### Type

Nintendo used Gill Sans italic caps for the wordmark, Futura for faceplate
labels ([Fonts In Use](https://fontsinuse.com/uses/60930/game-boy-logo-console-and-packaging)) —
identify, don't copy. **Embed-safe pixel fonts, all SIL OFL 1.1** (safe to
bundle in a closed-source binary; OFL forbids only standalone font resale):

- [Press Start 2P](https://fonts.google.com/specimen/Press+Start+2P) — 8×8 caps, the "8-bit" headliner
- [Silkscreen](https://fonts.google.com/specimen/Silkscreen) — 5 px UI-label font ([OFL text](https://raw.githubusercontent.com/google/fonts/main/ofl/silkscreen/OFL.txt))
- [VT323](https://fonts.google.com/specimen/VT323) — terminal dot-matrix, good for readouts
- [DotGothic16](https://fonts.google.com/specimen/DotGothic16) — JP dot gothic

Avoid "Early GameBoy" (dafont, no formal license text).

### Motifs worth quoting (and the legal line)

Safe individually: 4-shade green palette; dark inset screen bezel with a tiny
caps caption; twin magenta/indigo pinstripes; angled pill buttons; diagonal
speaker slashes (DMG has 6 slots, only 5 drilled through —
[teardown](https://b13rg.icecdn.tech/Gameboy_DMG/)); edge-mounted knurled
volume/contrast wheels; d-pad geometry; cartridge slot as a loading metaphor;
hi-DPI procedural pixel-grid rendering (the Analogue Pocket move —
[analogue.co](https://www.analogue.co/pocket)).

Not safe: the "GAME BOY" word mark (live US reg. 4,402,983 —
[Justia](https://trademarks.justia.com/858/36/game-85836451.html)); the DMG's
overall configuration is registered trade dress (reg. 5,958,568 —
[analysis](https://www.thatsatrademark.com/blog/nintendo-handheld-consoles));
the exact "DOT MATRIX WITH STEREO SOUND" phrase in its trade-dress context;
Gill-Sans-italic wordmark lookalikes. Rule followed by the whole modern-retro
ecosystem (Analogue, Playdate, Dirtywave M8, Nanoloop): **quote the screen,
the palette and the details; invent the body.** Chiptune UI conventions worth
keeping: everything on a tile grid, full-screen pages instead of scrolling,
dense readouts (LSDJ — [littlesounddj.com](https://www.littlesounddj.com/lsd/index.php)).

## E. Verification notes

- Hydra record sizes, stereo-pair storage, loop-point presence, and the
  generator-only structure were **verified against the real soundfont** with
  an independent parser written this session (section A numbers).
- Generator defaults/ranges were cross-checked between the spec PDF and
  FluidSynth's fluid_gen.c by the research pass; envelope curve shapes and
  filter Q semantics additionally corroborated by the SoundFont-Spec-Test
  suite and fluid_iir_filter.c's spec citations.
- All four recommended fonts' OFL licenses were confirmed at the
  google/fonts repo raw OFL.txt files.
- git-cloning GitHub repos from this sandbox returned 403 today, so tsf.h's
  internals were not re-read line-by-line here; the parser-only claim rests
  on the research pass's source quotes. Moot for the chosen path (own parser),
  and checkable on the Mac at build time.
- DMG shell hexes are community samples, not official specs — treat as
  starting points and tune by eye against reference photos.
