# M3 GUI - component notes

Apotheosis's photoreal editor is built from the suite-reusable component
family under `src/gui/`, copied verbatim from the Basilica Audio suite's M3
pilot (`basilica-audio/silentium`). This document is the "why" behind the
code comments, and records where Apotheosis's own layout/metering choices
depart from the pilot.

## Components (`src/gui/`)

| Component | Base class | Backs onto |
|---|---|---|
| `FilmstripKnob` | `juce::Slider` (RotaryVerticalDrag) | `knob-brass-v1` 128-frame filmstrip |
| `FilmstripToggle` | `juce::Button` | `toggle-brass-v1` 4-frame filmstrip |
| `AnalogMeter` | `juce::Component` + `juce::Timer` | `vu-brass-v1` face/needle/glass |
| `BasilicaLookAndFeel` | `juce::LookAndFeel_V4` | interim JUCE-drawn label styling |
| `ImageDensity.h` | (free functions) | @1x/@2x tier selection shared by all three |

All five are identical, byte-for-byte, to the pilot's copies - no
Apotheosis-specific code lives in `src/gui/`. **`FilmstripToggle` is unused**
in this plugin: Apotheosis has no boolean/toggle parameters (all eleven are
either continuous floats or discrete choices), so it is carried purely for
suite consistency (every sibling plugin's `src/gui/` is the same component
family) rather than because Apotheosis's editor instantiates one.

## Layout table

`PluginEditorLayout.h` (`apth::layout`) holds the faceplate's base @1x
(900x600) bay rectangles, copied 1:1 from
`.scaffold/gui-assets/faceplate-apotheosis-v1/layout-manifest.json` - the
same JSON `render_faceplate.py` was given to bake the engraved bay outlines,
so a control positioned at these rects lands exactly inside its frame. The
header band + roundel (title/brand-icon area) are NOT part of the manifest
(they're auto-derived by the render script from the canvas size, "same
proportions as faceplate-silentium-v1") - those two constants were instead
measured directly off the rendered `faceplate_apotheosis_900x600.png` (gold
pixel bounding box, PIL/numpy), the same "verify against shipped pixels"
discipline `AnalogMeter.cpp`'s needle pivot fraction uses, to avoid
compounding any Blender-world-unit-to-pixel conversion error.

`PluginEditor.cpp`'s anonymous namespace holds two tables -
`knobLayout` (8 entries) and `choiceLayout` (3 entries) - each entry naming
a `Bay` (`limiter`/`release`/`output`), a grid column/row within that bay,
and the parameter ID/label text. Unlike the pilot (which only has knobs and
toggles), Apotheosis mixes `FilmstripKnob`s and plain `juce::ComboBox`es
within the SAME bay grid (the release bay's 3x2 grid holds four knobs plus
one combo box in its five used cells) - `bayRectFor()`/`bayGridFor()` map a
`Bay` to its rect/grid dimensions once, shared by both layout loops in
`resized()`, so the two control kinds can never disagree about where their
shared bay actually is.

**The output bay is a special case, not the generic grid.** A first pass
used a uniform 3-column split of the bay's 190px width for Stereo Link's
knob plus the Dither/Dither Shape combo boxes; that left the two combo
boxes only ~51px wide, and JUCE's `ComboBox` rendered a bare `"..."` with
**zero visible letters** for even short selections ("Off"/"Flat") - a real
legibility bug, not just tighter truncation (verified visually via a
cropped `docs/gui-preview.png` render before the fix). `resized()` now
special-cases `Bay::output` for both loops: row 0 (top) is Stereo Link's
full-width knob, row 1 (bottom) splits the remaining height into two
half-width (~95px) cells for the combo boxes - comfortably enough to show
"Off"/"16-bit"/"24-bit"/"Flat"/"Shaped" in full. `ChoiceLayoutEntry::col`
means "left half (0) / right half (1) of the bottom row" for output-bay
entries specifically, not a grid column index.

## Why plain `juce::ComboBox` for the three choice parameters

Release Curve, Dither, and Dither Shape are `AudioParameterChoice`s (3-way,
3-way, 2-way). The pilot's asset set has no photoreal rotary-knob or
toggle-switch equivalent for an N-way discrete choice, and inventing a new
filmstrip asset family for three controls was judged not worth the
Blender-pipeline cost for this pass. Instead, each combo box is styled with
`BasilicaLookAndFeel::getLabelTextColour()`/`getLabelBackingChipColour()` -
the SAME colour pair the WCAG-AA contrast test
(`tests/gui/BasilicaLookAndFeelContrastTests.cpp`) verifies clears 4.5:1 -
so the combo box's text contrast is covered by the same guarantee as every
knob caption, without a second hand-picked colour pair. This is flagged as
an open end for a future asset pass (a photoreal rotary switch/selector),
not a placeholder or stub - the combo boxes are fully functional,
accessible, and on-brand as shipped.

## Metering: three meters sharing one generic VU face

Apotheosis's meter strip has three `AnalogMeter` instances (Gain Reduction,
True Peak, LUFS) side by side in `metersBay1x`, split into three equal
columns - the pilot only has two (Gain Reduction, Input Level). All three
share the exact same `vu-brass-v1` face/tick-table asset (baked -20..+3 dB,
0 dB at rest/top-centre) - there is no per-meter dial art - so each meter's
`timerCallback()` mapping is a deliberate choice, documented in
`PluginEditor.cpp`:

- **Gain Reduction** feeds `TruePeakLimiterEngine`'s own
  `gainToDecibels(minGainAppliedThisBlock)` readout directly (already <= 0
  dB, 0 = no reduction). On the shared 0-centred face this reads as "rest at
  no reduction, sweeps away (left) as reduction increases" - a legible
  reading of "GR downward" using the generic face, per this milestone's
  explicit allowance to document a per-meter mapping rather than requiring
  bespoke dial art per metric.
- **True Peak** feeds the engine's dBTP readout directly - a well-behaved
  limiter output sits at/below Ceiling (default -1 dBTP), comfortably inside
  the face's -20..+3 dB span.
- **LUFS** feeds Momentary (400 ms integration - the engine's fastest/most
  "VU-like" loudness readout; Short-Term's 3 s and Integrated's whole-session
  average would both read as near-static on a physically-styled needle).
  Typical mastering targets (roughly -14 to -9 LUFS) sit inside the same
  face's range; quieter material pins toward the -20 dB floor - an accepted
  limitation of reusing one generic face across three different metrics
  rather than commissioning three bespoke dial renders for this pass.

## Lookahead: a "setup" parameter, visually distinguished

`ParamIDs::lookahead` is prepare-time-latched (see its own doc comment in
`ParameterIds.h` and `TruePeakLimiterEngine::setLookaheadMs()`) - changes
only take effect at the next `prepareToPlay()` cycle, not live mid-stream,
since it resizes real-time buffers and changes the plugin's reported
latency. Per the faceplate README's explicit note ("the GUI should present
it as such, not as a live performance knob"), its knob:

- gets " (Setup)" appended to both its visible label and accessible
  title/description (`configureKnob()`), spelling out WHY in the
  description text, not just the suffix;
- is enclosed in a dashed amber frame, painted from
  `lookaheadSetupFrameBounds` (computed once per `resized()`, consumed by
  `paint()` - see `PluginEditor.h`'s docs for why it's cached rather than
  recomputed inline).

This is still a fully live-draggable `FilmstripKnob` bound the normal way
via `SliderAttachment` (the parameter itself is host-automatable - only the
*engine's* consumption of it is prepare-time-latched) - the visual/a11y
treatment communicates the *timing* of the effect, not a disabled control.

## Known limitations / open ends (carried from the pilot, still true here)

- **`vu-brass-v1`'s dial content only occupies the central half of its
  canvas** - `AnalogMeter::contentFractionOfCanvas` and the editor's
  `expandMeterBounds()` compensate identically to the pilot.
- **The preset bar is not re-skinned** with photoreal assets in this pass -
  same as the pilot.
- **Stepped window scaling (100/150/200%)** has no @3x/@4x tier - same
  limitation as the pilot.
- **No photoreal combo-box/selector asset** - see above; a genuine open end
  for a future pass, not carried from the pilot (the pilot has no choice
  parameters at all).
- **Release Curve's combo box text truncates** ("Exponential" renders as
  "Expone...") - its cell (release bay, 90px wide / 3 columns) is narrower
  than "Exponential" needs at the stock combo-box font. Unlike the output
  bay's Dither/Dither Shape fix above, this is genuine truncation with
  visible partial text (not a bare `"..."` placeholder with zero
  information) and the dropdown itself shows all three options in full when
  opened, so it was judged acceptable for this pass rather than redesigning
  the release bay's grid too - flagged here as a real, not hidden,
  shortcoming for the accessibility reviewer.
