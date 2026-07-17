# Factory presets

Eight factory presets ship with Apotheosis v0.2.0, embedded via BinaryData
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
