# Factory presets

Eleven factory presets ship with Apotheosis v0.4.0: the eight v0.2.0
presets below (unchanged, byte-for-byte) plus three that exercise the
v0.4.0 engine (see "v0.4.0 additions" at the end of this document). All are
embedded via BinaryData
from `presets/factory/*.json` (see `docs/preset-system-notes.md` for the
build wiring). All are the sourced starting points from
`docs/design-brief.md`'s "Factory Presets" section - see that document's own
Honesty section for what these numbers are and aren't calibrated against
(research/manual-derived qualitative principles, not measured against any
competitor's actual DSP).

| Preset | Category | Intent |
|---|---|---|
| **Default** | Init | The v1-compatible default: pure lookahead limiting, none of the four new v0.2.0 controls engaged (Attack 0 ms, Auto Release 0%, Stereo Link 100%, Dither Shape Flat) - matches the shipped out-of-the-box default exactly, and is this plugin's out-of-the-box default (see the M2 default-resolution order in `docs/preset-system-notes.md`). |
| **Punchy Master** | Master | The reference-class "short lookahead + long attack + fast release" loudness/punch recipe, adapted to v0.2.0's classifier-style Attack (25 ms) with a supporting Auto Release (30%) so isolated transients recover briskly while any denser passages still get a longer effective release. |
| **Dense/Loud Modern** | Master | Heavier gain reduction for dense, high-energy masters: a stronger Input Gain push, Smooth Release Curve, and a meaningful Clip Mix (35%) blend for a more "glued"/aggressive modern-loudness character. |
| **Wide Image Preserve** | Master | Loosens Stereo Link to 40% so hard-panned peaks in one channel don't pull the opposite channel's gain down with them - useful on masters with wide, deliberately asymmetric stereo content. |
| **Streaming Safe (High Loudness)** | Master | The sourced Amazon Music / "louder than -14 LUFS" -2 dBTP guidance (`docs/research-notes.md` S4), as a discoverable named starting point for masters pushed hot enough that the mainstream -1 dBTP convention leaves too little margin. |
| **Adaptive Riding** | Master | Demonstrates Auto Release at its full 100% on dynamic programme material (mixed transient/sustained content), with every other control left at its Default value, so the program-dependent release modulation is the only thing distinguishing this from Default. |
| **Bright Clipper Blend** | Master | Demonstrates the v1 Clip Mix character (60%) combined with the new fast-transient Attack path (30 ms), for an aggressive but still transient-aware "clipper" tone. |
| **Clean Export (Dithered)** | Master | The full, correctly-ordered final-stage bounce chain: 16-bit Dither on, Shaped noise-shaping engaged, at the conventional -1.0 dBTP Ceiling - the recommended starting point for a fixed-bit-depth final export. |

None of the presets change `inputGain`/`ceiling`/`release`/`lookahead`
outside the ranges already shipped in v0.1 - every new v0.2.0 control
(`attack`, `autoRelease`, `stereoLink`, `ditherShape`) is set explicitly in
every preset file, even where it matches the plugin's own default, so each
preset is a complete, self-documenting snapshot rather than relying on
implicit fallback.

## v0.4.0 additions

Three presets added in v0.4.0 to give each new engine feature a
discoverable starting point. They set all eighteen parameters explicitly,
following the same self-documenting-snapshot rule as the eight above. The
eight v0.2.0 files are untouched.

| Preset | Category | Intent |
|---|---|---|
| **Transparent Mastering** | Master | The recommended v0.4.0 mastering default: Style **Transparent** (cascaded-box FIR attack smoother across the full 10 ms lookahead, dual 50 ms/800 ms release followers with a 2 dB fast cap), **8x** oversampling, **True Peak Guard on** so the -1.0 dBTP Ceiling is a measured guarantee rather than a margin, Auto Release 50%, and 24-bit **Weighted** noise-shaped dither for the final render. |
| **Punchy Loud Style** | Master | Loudness-first: Style **Punchy** (single box over 0.4x lookahead, fast 30 ms/400 ms followers with a 4 dB cap, so transient tops survive), a 6 dB Input Gain push, a 35% Clip Mix blend for density, 8x oversampling and dither off - a mix-bus/loudness starting point rather than a delivery chain. |
| **Safe Archival (True Peak)** | Master | Maximum smoothness for classical/acoustic and archival delivery: Style **Safe** (three boxes, 60 ms/1500 ms followers, 1 dB fast cap), full 20 ms lookahead, **16x Linear Phase** oversampling, a conservative -2.0 dBTP Ceiling with **True Peak Guard on**, and 24-bit Weighted dither. This is the most expensive preset in the set (see the latency and CPU tables in `docs/manual.md`). |

Note the prepare-latch contract: `oversampling` and `osPhase` are read only
inside `prepare()`, so loading **Safe Archival (True Peak)** (or any preset
that changes them) applies its 16x Linear Phase chain at the next
host-driven `prepareToPlay()` - exactly like `lookahead` has behaved since
v0.1. See `docs/manual.md`.
