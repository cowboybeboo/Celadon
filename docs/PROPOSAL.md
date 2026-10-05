# Plugin #4 — Design Proposal
### A retro soundfont synth · 2026-07-16 · Faber

The research is done (see RESEARCH.md — every claim sourced). This is the
part where you make three decisions: **the name, the look, the feel of the
knobs.** Everything else below is my recommendation, ready to build.

---

## 1. What it is, in one paragraph

You load a `.sf2` soundfont — starting with your Pokemon RSE HD file — and
play it like a proper polysynth. The plugin honors what the soundfont says
(key maps, loops, stereo pairs, tuning), then hands you real synth controls
on top: full ADSR, a resonant filter with its own envelope, an LFO you can
aim at pitch, filter or volume, and a four-slot retro effects chain
(CRUSH → CHORUS → DELAY → REVERB). 16 voices, VST3 + AU + Standalone,
Apple Silicon, same build system as Tessera/Atrium/Strata.

## 2. Name — pick one

All scanned against existing audio products today; sources in chat.

1. **Celadon** — the grey-green ceramic glaze. Quietly nods at both the greige
   shell and the green screen; players will also recognize a certain city.
   Scanned clear. My pick.
2. **Cartouche** — French for *cartridge*, and an ornamental frame around a
   name. A soundfont slots in like a cart. Scanned clear.
3. **Viridis** — Latin for green, fits the Tessera/Atrium/Strata lineage.
   Clear, but Acustica sells a "Viridian" channel strip nearby.
4. **Ludus** — Latin for play/game. Scanned clear.
5. **Pallet** — the town every journey starts from, plus the palette pun.
   Clear, but reads like a typo to non-fans.

(Using "Celadon" as the placeholder below — swap freely.)

## 3. The look — a handheld lying on its side

Modern take on the DMG, GBA-landscape proportions (fits a plugin window
better than the tall original, and your soundfont is a GBA soundfont).
**Not** connected to the Tessera/Atrium design system — new identity.
See `mockup.html` for the full picture. The moves:

- **Warm greige shell** (#C4BEBB family), soft-cornered, drawn in code.
- **The screen is the centerpiece**: a dark inset bezel holding a chunky
  dot-matrix display in the four DMG greens (#0F380F → #9BBC0F), rendered
  hi-DPI with a subtle procedural pixel grid (the Analogue Pocket trick —
  crisp, not blurry-nostalgic). It shows the loaded cart + preset, a live
  waveform of what's actually playing, the envelope shape, filter curve,
  and voice count. Everything on it animates from real audio, like Atrium's
  dome.
- **Original caption, twin pinstripes**: the bezel reads
  "SOUNDFONT · 16 VOICE · STEREO SOUND" flanked by the magenta/indigo
  pinstripe pair (#9B2256 / #484686). Legal line respected: we quote the
  palette and details, never the Game Boy wordmark or the full DMG silhouette
  (that's registered trade dress — details in RESEARCH.md).
- **Burgundy is the new orange**: #9B2256 is this plugin's single accent —
  active states, value arcs, ALT mode.
- **A/B buttons do MAIN/ALT**: two round burgundy buttons flip every knob
  between its two functions — the Tessera dual-knob system wearing a d-pad
  costume. B = ALT, A = back to MAIN. Labels stay clickable too.
- **Angled pill buttons** → GUIDE and PANIC (all notes off).
- **Speaker slashes** in the corner, six slots, one blind — the DMG had only
  five drilled through. Decoration is allowed one joke.
- **Edge wheels**: a knurled VOLUME wheel on the left edge; a CONTRAST wheel
  on the right that swaps the screen palette (DMG green / Pocket grey /
  backlit). Cosmetic, saved with the project, pure delight.
- **Cartridge slot** = soundfont loader: the top edge shows the loaded .sf2
  as a cart ("POKEMON RSE HD"); click to load another file. Presets inside
  the cart step with ◄ ► and program change, like every plugin before it.
- **Pixel fonts, legally**: Press Start 2P for the wordmark, Silkscreen for
  labels, VT323 for readouts — all SIL OFL, safe to embed (verified).

## 4. The feel — how knobs meet the soundfont

The interesting design problem: every soundfont preset already carries its
own envelope, filter and LFO settings per zone. Two ways a knob can work:

- **Absolute**: the knob *is* the envelope; soundfont values are ignored.
  Predictable, but flattens multi-zone presets (a drumkit where every drum
  had its own decay now has one).
- **Relative** (the Atrium resonance pattern, my recommendation): centre =
  play the soundfont as its maker intended; turning scales *around* those
  values (times ×0.25–×4, cutoff ±4 octaves). Multi-zone character survives,
  and the readout always shows the effective value in real units.

Recommendation: **relative for A, D, R, cutoff and LFO depth; absolute for
sustain and resonance** (those two are musically "set a level" controls).
A tiny INIT action snaps everything to neutral. If it feels wrong in play,
flipping any control to absolute is a small change — the parameters are
already separate host parameters either way.

## 5. Control surface (all automatable, CC 20+, macro-ready)

| Section | Primary | ALT |
|---|---|---|
| ENVELOPE | ATTACK · DECAY · SUSTAIN · RELEASE | mod-env A · D · S · R (drives filter/pitch) |
| FILTER | CUTOFF · RESONANCE | ENV AMT · KEYTRACK |
| LFO | RATE (sync/free, DOT/TRI like Tessera) · DEPTH | TARGET (pitch/filter/amp) · DELAY |
| SOUND | GLIDE · TUNE | VEL SENS · STEREO WIDTH |
| FX | CRUSH · CHORUS · DELAY · REVERB (one macro-knob each) | crush rate · chorus rate · delay time+feedback · reverb size |

- **CRUSH** — bit depth + sample-rate reduction, the "make it sound like the
  handheld actually played it" knob. Original DSP, tested.
- **CHORUS + REVERB** — honoring the SF2 send generators; reverb is Atrium's
  FDN math in a small room, chorus is a classic 2-voice.
- **DELAY** — tempo-synced echo, Tessera's sync/free + DOT/TRI pattern.
- GUIDE overlay covering everything, day one. README ships the macro map
  (suggested rack: Cutoff, Attack, Release, LFO Depth, Crush, Delay mix,
  Reverb mix, Glide).

## 6. Under the hood (the short version)

Own SF2 parser (the format is nine fixed-size tables — my Python prototype
already parses your file completely), own 16-voice engine in JUCE-free
headers: 4-point Catmull-Rom interpolation (FluidSynth-default quality),
TPT state-variable filter built for fast modulation, SF2-correct envelope
curves. Zero third-party code, zero license risk — full reasoning and the
rejected alternatives (TinySoundFont, FluidSynth/LGPL, SFZero) in RESEARCH.md.

Tests from the first line, count reported every turn, behavioral not smoke:
pitch accuracy in cents across the key range, loop continuity (no clicks at
the seam), envelope timing against the spec's math, filter stability at every
extreme, stereo-pair sync, voice-stealing under 16-voice pressure — plus a
"plays the actual Pokemon file" integration test using a checked-in slice of
the hydra.

## 7. What happens next

1. You pick a name (§2) and react to the mockup — layout moves are cheap now,
   expensive after eight rebuild rounds.
2. I scaffold the folder next to Tessera/Atrium/Strata, carry the Strata
   build system forward, and build the parser + voice engine with tests.
3. First hand-off: a build that loads your cart and plays every preset with
   working ADSR — then we layer filter, LFO, FX in short rounds.

Open question I couldn't answer from the brief: any feeling about **where
the pitch wheel range should sit** (±2 semitones classic vs ±12 for
whammy tricks)? ALT-able either way.

— Faber
