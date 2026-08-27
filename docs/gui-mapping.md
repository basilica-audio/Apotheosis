# M3 photoreal GUI — parameter mapping (victorian design)

Status: implementation for the basilica-audio wave shared with
requiem/tenebrae (pilot: `basilica-audio/aureate`'s "tubecomp" M3 GUI).
**Owner visual sign-off pending — see the PR. Merge is forbidden until
Yves has looked at `docs/gui-preview.png` and approved it.**

## Design source

`brand/mocks/victorian/` (repo-relative to the suite root, one level above
this repo): `master-01-base.png` (the design's *only* production
background — unlike tubecomp, there is no later "clean" render generation),
`components/needle-{mainVU,smallMeterTop,smallMeterMid,smallMeterBottom}.
{png,json}`, `components/tube-glow.{png,json}`, `layout-manifest.json`.

Embedded into the plugin as `resources/gui/master_victorian.png`,
`resources/gui/needle_{mainVU,smallMeterTop,smallMeterMid,
smallMeterBottom}_victorian.png`, `resources/gui/tube_glow_victorian.png`.

## Superseded files (left in place, not deleted)

This repo previously shipped a different, non-owner-approved "M3" editor
(a generic filmstrip-knob/AnalogMeter GUI against a placeholder faceplate,
not the approved victorian mock). This PR replaces `src/PluginEditor.{h,
cpp}` and `src/PluginEditorLayout.h` with the victorian design entirely.
Per this change's own constraints (no `rm`/`mv`), the previous generation's
files are **left in the tree, unreferenced, not deleted**:

- `src/gui/AnalogMeter.{h,cpp}`, `src/gui/BasilicaLookAndFeel.{h,cpp}`,
  `src/gui/FilmstripKnob.{h,cpp}`, `src/gui/FilmstripToggle.{h,cpp}`,
  `src/gui/ImageDensity.h`
- `resources/gui/faceplate_apotheosis_{900x600,1800x1200}.png`,
  `resources/gui/knob_brass_strip_{160px,320px}_128f.png`,
  `resources/gui/toggle_brass_strip_{100px,200px}_4f.png`,
  `resources/gui/vu_brass_{face,glass,needle}_{480x270,960x540}.png`
- `tests/gui/AnalogMeterAccessibilityTests.cpp`,
  `tests/gui/AnalogMeterBallisticsTests.cpp`,
  `tests/gui/BasilicaLookAndFeelContrastTests.cpp`,
  `tests/gui/FilmstripFrameMathTests.cpp`

These files still compile and their own unit tests (which exercise the
standalone components directly, not the editor) still pass — they are
simply dead code from this editor's point of view. A future housekeeping
pass with explicit `rm` permission should remove them.

`src/gui/ToggleZoneSwap.h` is also copied into this repo (matching every
sibling plugin's shared component family) but is **genuinely unused** —
the victorian design has no lever/switch element at all (see
`brand/mocks/victorian/prompts.md`).

## Meter semantics

| Position | Reading | Native units | Data source |
|---|---|---|---|
| Grand meter (left, large) | **Gain Reduction** | dB, `<= 0` | `ApotheosisAudioProcessor::getGainReductionDb()` |
| Small meter, top | **Input level** | dBFS peak, post-input-gain/pre-limiting | `getInputLevelDb()` |
| Small meter, mid | **Output level** | dBFS peak, post-dither/final output | `getOutputLevelDb()` |
| Small meter, bottom | **True-peak margin** | dB headroom to the Ceiling | `Ceiling − getOutputTruePeakDb()` |

`getInputLevelDb()`/`getOutputLevelDb()` are new additions to
`TruePeakLimiterEngine` in this PR (the engine already published gain
reduction, output true peak, and Momentary/Short-Term/Integrated LUFS —
see `TruePeakLimiterEngine.h`'s pre-existing metering getters — but had no
plain dBFS peak reading for input or final output, which the design's two
level meters need). Both are relaxed atomics, updated once per processed
block, real-time safe (see `TruePeakLimiterEngine.cpp`'s `processChunk()`).

**True-peak margin is intentionally a *different* measurement from the
output-level meter**, not a duplicate: `getOutputTruePeakDb()` is measured
in the *oversampled* domain, immediately after the limiter's gain/clip/hard-
clamp stage (before downsampling/dither) — the actual inter-sample
true-peak reading the limiter's own guarantee is built on. `getOutputLevelDb
()` is a plain base-rate peak of the *final* signal (post-dither). They
will normally read within a fraction of a dB of each other, but are
conceptually distinct: one is "how loud is the output", the other is "how
close is the output to violating the true-peak ceiling".

## Rest/deflection convention (binding for all 4 needles)

The negative (left) end of every sweep is the **calmest/safest** reading (0
dB gain reduction / quiet signal / large true-peak margin); the needle
deflects toward the **positive (right)** end — which is where the grand
meter's own baked red zone physically sits — as the reading gets **hotter**
(deeper gain reduction / louder signal / shrinking true-peak margin). This
mirrors a classic VU meter's own "rests left, swings right when it gets
loud" reading direction, applied consistently to all four needles
(including the two that are not literally "loudness", GR and margin) so an
operator's eye only has to learn one direction.

## Measured dial sweeps (`analysis/measure_dial_ticks.py`)

This design's dials carry **no baked numerals at all** — a deliberate,
documented render decision (`brand/mocks/victorian/prompts.md`'s own
"IMPORTANT SCALE CORRECTION" note removed them; the tick marks alone
remain). Per the briefing, this script measures each dial's tick arc as a
single angular **SPAN**, and `PluginEditorLayout.h` maps each meter's own
chosen value range onto that span **linearly** — not a calibrated
per-label piecewise table (contrast with the tubecomp pilot's dial, which
does have 9 printed labels).

Method (full docs in the script itself): tick ink is located by sweeping a
candidate radius (from the needle's own hub pivot — the tick arc is
concentric with the pivot, standard analog-meter geometry, *not* with the
dial's own geometric centre) and finding the radius with peak
sample-to-sample colour variation (discrete tick dashes vs. this render's
own smooth inner-bezel shadow gradient, which is comparably dark on average
but has near-zero high-frequency variation — visually confirmed to be a
lighting artifact, not tick ink, via `overlay_*_band.png`, generated during
development). An unconditional glass-boundary disk mask (radius from the
dial's own centre) excludes the brass bezel/rivets at wide angles.

| Dial | Measured tick extent | Notes |
|---|---|---|
| Grand meter (mainVU) | **≈ −50° to +40°** (span 90°) | 13 evenly-spaced ticks (~7.5° apart); baked red zone occupies roughly the last third, **≈ +10° to +40°** (visually cross-checked against `mainVU_tickarc_grid.png`, an angle-ray overlay generated during development) |
| smallMeterTop | ≈ −40° to +33° | |
| smallMeterMid | ≈ −42° to +34° | |
| smallMeterBottom | ≈ −38° to +33° | |

The three small dials are visibly the *same* gauge design repeated three
times and measured independently to within ~5° of each other — a single
shared sweep (`smallMeterRestAngleDeg = −40°`, `smallMeterFullScaleAngleDeg
= +32°`, both in `PluginEditorLayout.h`) is used for all three rather than
three near-identical tables that would imply more measurement precision
than the render actually supports. The grand meter keeps its own, wider,
independently-measured sweep (`grRestAngleDeg = −50°`,
`grFullScaleAngleDeg = +40°`).

**Honesty note**: because there are no numerals, the exact dB values these
sweeps represent are a *design choice*, not a measurement — the script only
measures *where the ticks physically are*, not *what number each one would
have said*. The value ranges chosen (documented below) are reasoned
engineering choices, flagged as such rather than presented as measured:

- **Gain Reduction**: 0 dB (rest) to **−12 dB** (full scale/pinned). 12 dB
  is a common upper bound for a mastering-limiter GR meter's useful range —
  not derived from this render.
- **Input/Output level**: Standard-A suite convention, 0 VU = **−18 dBFS**
  (same reference `basilica-audio/aureate`'s own VU needle uses), classic
  VU range **−20 to +3** VU-referenced dB mapped onto the shared small-
  meter sweep.
- **True-peak margin**: **12 dB** (comfortably safe, rest) down to **0 dB**
  (right at the Ceiling, full scale). 12 dB is a chosen "this is clearly
  fine" reference, not measured.

## Knob mapping (3 of 11 automatable parameters)

| # | Master px (cx, cy, r) | Parameter | Why here |
|---|---|---|---|
| 1 (left) | (698.0, 545.0, 57.0) | **Input Gain** (`inputGain`) | The single most-reached-for control of any limiter — "how hard am I hitting the ceiling". |
| 2 (centre) | (867.0, 546.0, 55.0) | **Ceiling** (`ceiling`) | The limiter's defining parameter — the never-exceed true-peak target. |
| 3 (right) | (1038.0, 543.0, 57.0) | **Release** (`release`) | The primary time-domain "feel" control. |

Rotation convention: `MasterCropKnob`'s ±135° sweep maps parameter
proportion 0.5 → 0° (12 o'clock, the master's own baked rest pose) — a
knob at exactly its parameter's *range midpoint* renders pixel-identical to
the baked art with zero live rotation. Input Gain's range (−12…+24 dB) has
its midpoint at +6 dB, not its 0 dB default — so the baked master's own
12-o'clock knob pose corresponds to +6 dB Input Gain, not the plugin's
actual 0 dB default; the knob still opens at the *correct* rotation for 0
dB (a small counter-clockwise offset from 12 o'clock), honestly reflecting
the parameter's real value, exactly like a real rotary control — "rest at
12 o'clock at the default" was never the pilot's convention either (see
`basilica-audio/aureate`'s own docs on this point).

This design provides only 3 knob positions against Apotheosis's 11
automatable parameters (8 `AudioParameterFloat` — `inputGain`, `ceiling`,
`release`, `lookahead`, `clipMix`, `attack`, `autoRelease`, `stereoLink` —
plus 3 `AudioParameterChoice` — `releaseCurve`, `dither`, `ditherShape`) —
a much smaller fraction than the tubecomp pilot's 10-of-21. The rest remain
fully functional via automation, host generic UI, and presets; nothing is
removed from the APVTS.

### Not wired to a knob (automation/preset-only)

`lookahead` (prepare-time-latched "setup" parameter — see
`ParameterIds.h::lookahead`), `releaseCurve`, `dither`, `clipMix`,
`attack`, `autoRelease`, `stereoLink`, `ditherShape` — all 8 remain fully
functional APVTS parameters (automatable, saved/restored in state and
presets), simply without a dedicated physical control in this pass. This
design has no baked toggle/switch element and no discrete-choice rotary
switch shape at all (contrast with the tubecomp pilot's 3-way `Character`
knob), so none of the three `AudioParameterChoice` parameters have a
natural physical-control fit here — a real, honest limitation of this
particular faceplate's asset set, not an oversight.

## Tube bay behaviour

The 4 tubes' glow follows **limiting intensity** (`SubtractiveGlow`,
subtractive runtime model per `tube-glow.json`):

```
depthDb   = max(0, -getGainReductionDb())        // 0 = idle, rising as GR deepens
t         = idleBreath(0.75 +/- 0.05, multi-sine) + signalPush(depthDb, 0..12dB -> 0..1)
t         = clamp(t, 0, 1)                        // hard ceiling: t=1 == the baked master itself
frame     = base - glow_rgb * alpha * (1 - t)      // SubtractiveGlow.h's own formula
```

- **Idle** (no gain reduction, or between processing bursts): breathes
  around `t ≈ 0.75`, with a ±0.05 slow multi-sine wander
  (`Flicker.h::slowDriftLayers`) — matches the suite's standing "idle
  flicker must never read as fully off" rule.
- **Working**: as gain reduction deepens toward the grand needle's own
  full-scale depth (12 dB — `tubeGlowCeilingDb == -grFullScaleReductionDb`,
  asserted as a structural invariant in `EditorLayoutTests.cpp`), `t` rises
  toward 1.0 — the tubes reach full glow (identical to the baked master,
  the same ceiling the grand needle itself pins at) exactly when the
  limiter is working as hard as the GR meter can show.
- **Ballistics**: `stepGlowMix()`'s own one-pole smoothing, `τ = 0.15 s`.

## Tests

`tests/gui/EditorLayoutTests.cpp` (layout invariants against
`PluginEditorLayout.h`'s own constants), `EditorSnapshotTests.cpp`
(construction/destruction, non-blank render written to
`docs/gui-preview.png`, knob rotation proof, needle rotation proof, idle
tube-glow time-variance proof, tube-glow hard-ceiling proof),
`EditorAccessibilityTests.cpp` (every knob/needle/scale-button a11y
coverage), `HubNeedleTests.cpp` (ballistics + the new linear value→angle
mapping, both orderings of rest/full-scale value), `MasterCropKnobTests.cpp`
/ `SubtractiveGlowTests.cpp` (copied verbatim from the pilot — fully
design-agnostic components, unchanged).

## Typography pass (suite typo phase)

Owner decision 2026-07-26 ("Weg 2", made on THIS design after its scale
correction failed 3x in the render loop): text is never baked into the AI
master - it is set locally as a sharp JUCE text layer in the suite serif
(EB Garamond, embedded via BinaryData, OFL). Implementation:
`src/gui/PlateTypography.h` (copied verbatim from the aureate pilot's
typography pass), drawn LAST within `paint()` - over the tube-glow blit,
under the needle child components, exactly like a printed dial face
beneath its needle.

What is lettered:

- **Grand dial numerals** `0 3 6 9 12` (unsigned, classic GR-meter
  convention) on the open parchment OUTSIDE the baked tick arc, each at
  the EXACT angle the needle deflects to for that reading - derived at
  draw time from the same `grRest*`/`grFullScale*` constants the needle
  maps through, so numerals and needle can never disagree.
- **`GAIN REDUCTION` caption** inside the arc, above the baked gear
  bridge - the honest wordmark for this dial (it reads gain reduction,
  not VU).
- **Small-meter legends** `INPUT` / `OUTPUT` / `MARGIN`, gilded, printed
  ON each gauge's own face mound below the pivot (the way real gauges
  print their function on the dial; the plate bands between the bezels
  proved narrower than measured - the bezels' rings fade gradually - and
  below the bottom dial the oak rail begins immediately).
- **Gilded knob labels** `INPUT GAIN` / `CEILING` / `RELEASE` under each
  knob's interactive hit-area.

Two lettering treatments, both through the one shared
`PlateTypography::drawEngraved` path: printed sepia ink on the bright
parchment (numerals/caption), gilded gold with a dark drop shadow on the
dark bronze (legends/labels - incision ink would vanish on a ~50-90
luminance ground; gold-leaf lettering is the period-correct treatment and
matches the brand's antique-gold-on-charcoal system).

Tests: `tests/gui/EditorTypographyTests.cpp`.
