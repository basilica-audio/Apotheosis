#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

// Apotheosis's own @1x faceplate/control-bay geometry table for the M3
// photoreal "victorian" GUI - lives in its own header, rather than as an
// anonymous-namespace block inside PluginEditor.cpp, so
// tests/gui/EditorLayoutTests.cpp can assert layout invariants directly
// against the SAME numbers PluginEditor.cpp actually lays components out
// with (basilica-audio/aureate's own PluginEditorLayout.h convention for
// the tubecomp pilot, copied here and re-measured against this design's own
// master render/component jsons).
//
// SUPERSEDES the previous (M3-labelled but not owner-approved) generic
// filmstrip-knob/AnalogMeter editor this file used to describe (900x600
// bay-grid geometry) - that generation's own component files
// (src/gui/AnalogMeter.*, BasilicaLookAndFeel.*, FilmstripKnob.*,
// FilmstripToggle.*, ImageDensity.h) and resources
// (resources/gui/faceplate_apotheosis_*.png, knob_brass_strip_*.png,
// toggle_brass_strip_*.png, vu_brass_*.png) are left in place, unreferenced
// by this editor from this revision on - NOT deleted (repository policy for
// this change forbids rm/mv) - see docs/gui-mapping.md's "Superseded files"
// section for the full list and rationale.
//
// Every constant below is derived from the victorian design's own measured
// provenance (repo-relative to the suite root, one level above this repo):
//   - brand/mocks/victorian/layout-manifest.json - plate/meter/knob/
//     tube-bay geometry, measured against master-01-base.png (the design's
//     ONLY production background - unlike tubecomp, this design has no
//     later "clean" render generation).
//   - brand/mocks/victorian/components/needle-{mainVU,smallMeterTop,
//     smallMeterMid,smallMeterBottom}.json - each needle's own hub pivot.
//   - brand/mocks/victorian/components/tube-glow.json - the tube-bay glow
//     layer's own canvas offset/size within the master.
//   - analysis/measure_dial_ticks.py (this repo) - each dial's own tick-arc
//     angular SPAN, measured directly against master-01-base.png (this
//     design's dials carry no baked numerals at all - see that script's
//     own top-of-file docs for why a linear map, not a calibrated
//     per-label table, is what gets measured and used here).
namespace apth::layout
{
    // Master render's own canvas size (brand/mocks/victorian/*.json's own
    // "imageSize"/"masterWidth"/"masterHeight" fields, consistent across
    // every victorian-family artifact) - the scale factor below is
    // plateWidth1x / masterCanvasWidthPx.
    constexpr int masterCanvasWidthPx = 1376;
    constexpr int masterCanvasHeightPx = 768;

    constexpr int plateWidth1x = 900;
    constexpr int plateHeight1x = 502; // masterCanvasHeightPx scaled by the same factor as plateWidth1x

    //==========================================================================
    // Knobs: 3 brass knobs in a single row beneath the tube bay
    // (layout-manifest.json's "knobs" array, all row "bottom") - Input
    // Gain / Ceiling / Release, left to right (see docs/gui-mapping.md for
    // the full rationale). Two separate tables, same convention as
    // aureate/silentium's own knobSlider/knobMasterGeometry split:
    //   - knobCentreX1x + the single shared knobRowY1x/knobDiameter1x
    //     below: the INTERACTIVE juce::Slider hit-area, row-snapped to the
    //     manifest's own measured mean Y (544.7 master px; the 3 raw
    //     centres deviate at most 1.67px from that mean - layout-
    //     manifest.json's own "rowYs" field - well within a ~55-57px knob
    //     radius, so snapping is imperceptible and keeps the row-alignment
    //     invariant structurally guaranteed rather than approximately
    //     true) and mean radius (56.3 master px across the 3 knobs, which
    //     also differ by at most 2px from that mean - close enough to
    //     share one diameter cleanly, unlike aureate's two genuinely
    //     different upper/lower row radii).
    //   - knobMasterGeometry (PluginEditor.cpp): each knob's own true
    //     sub-pixel measured (cx, cy, r) in MASTER PIXELS (not row-snapped),
    //     used only to build MasterCropKnob's feathered crop from the exact
    //     baked art location - never for hit-testing.
    constexpr int knobRowY1x = 356; // 544.7 master px * scale, rounded
    constexpr int knobDiameter1x = 74; // mean(57,55,57) master px * 2 * scale, rounded

    constexpr std::array<float, 3> knobCentreX1x { 457.0f, 567.0f, 679.0f }; // 698/867/1038 master px * scale, rounded

    //==========================================================================
    // Needles: 4 independent HubNeedle child components, each centred
    // squarely on its own measured hub pivot (components/needle-*.json's
    // own pivotXInMaster/pivotYInMaster) - the dial FACE (plate, bezel,
    // tick marks, red zone, gear hub) is fully baked into the master, this
    // design draws ONLY the 4 live needle sprites on top of it.
    //
    // componentSize1x: generous margin (1.5x) over each sprite's own square
    // canvas (needle-*.json's "spriteSize") so the sprite's full rotation
    // sweep never clips against the component's own bounds - same
    // "generous margin, not the exact sprite size" convention as aureate's
    // meterComponentSize1x.
    //
    // juce::Point/Rectangle's constructors are not constexpr in JUCE 8.0.14
    // (verified against basilica-audio/aureate's own PluginEditorLayout.h
    // docs) - plain `const` namespace-scope values instead, still
    // initialised exactly once, well before any constructor runs.
    struct NeedleGeometry1x
    {
        juce::Point<int> topLeft1x; // pivot1x - componentSize1x/2
        int componentSize1x;
        float spriteSizeFraction; // spriteSize(master px) * scale / componentSize1x
        float bakedAngleDeg; // needle-*.json bakedAngleDeg
    };

    // mainVU: pivot (378.99, 456.00) master px, spriteSize 318 -> comp1x
    // 312 (318*1.5*scale, rounded), pivot1x (248, 298).
    const NeedleGeometry1x mainVUNeedle1x { { 92, 142 }, 312, 0.6667f, -0.018f };

    // smallMeterTop: pivot (1194.02, 221.0), spriteSize 96 -> comp1x 94,
    // pivot1x (781, 145).
    const NeedleGeometry1x smallMeterTopNeedle1x { { 734, 98 }, 94, 0.6667f, -0.679f };

    // smallMeterMid: pivot (1194.9, 409.0), spriteSize 120 -> comp1x 118,
    // pivot1x (782, 268).
    const NeedleGeometry1x smallMeterMidNeedle1x { { 723, 209 }, 118, 0.6667f, -0.385f };

    // smallMeterBottom: pivot (1196.18, 580.0), spriteSize 122 -> comp1x
    // 120, pivot1x (782, 379).
    const NeedleGeometry1x smallMeterBottomNeedle1x { { 722, 319 }, 120, 0.6667f, -4.714f };

    //==========================================================================
    // Dial sweep calibration (analysis/measure_dial_ticks.py's own measured
    // tick-arc angular extents - see docs/gui-mapping.md for the full
    // per-dial measured numbers and the reasoning behind the chosen
    // value ranges each sweep is mapped across). 0deg = straight up,
    // positive = clockwise (matches every needle-*.json's own
    // bakedAngleConvention).
    //
    // SUITE-WIDE REST/DEFLECTION CONVENTION (binding for every needle in
    // this design, documented once here rather than per-dial): the
    // negative (left) end of each sweep is the CALMEST/SAFEST reading (0 dB
    // gain reduction / quiet signal / large true-peak margin) and the
    // needle deflects toward the positive (right) end - which is where the
    // grand meter's own baked red zone physically sits - as the reading
    // gets HOTTER (deeper gain reduction / louder signal / shrinking
    // true-peak margin). All four needles share this same left=calm,
    // right=hot reading direction.
    //
    // Grand meter (mainVU) - GAIN REDUCTION. Measured tick arc extent
    // ~[-50, +40] deg (13 evenly-spaced ticks at this design's own
    // measured spacing, see docs/gui-mapping.md); the baked red zone
    // occupies roughly the sweep's own last third, [+10, +40] deg.
    // 0 dB GR (idle, no reduction) rests at the sweep's negative end;
    // grFullScaleReductionDb (a chosen, documented "pins the needle" depth
    // - see docs/gui-mapping.md's Honesty note on why 12 dB, not a
    // measured value) deflects it fully into the red zone.
    constexpr float grRestAngleDeg = -50.0f;
    constexpr float grFullScaleAngleDeg = 40.0f;
    constexpr float grRestDb = 0.0f;
    constexpr float grFullScaleReductionDb = -12.0f;

    // Small meters (top/mid/bottom) - measured independently per dial and
    // found consistent to within ~5deg of each other (top ~[-40,+33],
    // mid ~[-42,+34], bottom ~[-38,+33] - see docs/gui-mapping.md); since
    // these are visibly the SAME gauge design repeated three times, a
    // single shared sweep is used for all three rather than three
    // near-identical tables that would imply more measurement precision
    // than the render actually supports.
    constexpr float smallMeterRestAngleDeg = -40.0f;
    constexpr float smallMeterFullScaleAngleDeg = 32.0f;

    // Input/Output level needles (Standard-A suite convention: 0 VU =
    // -18 dBFS - see docs/gui-mapping.md). VU-referenced dB (dbfs -
    // vuZeroReferenceDbfs) is clamped to the classic VU range [-20, +3]
    // before mapping onto the small-meter sweep - the same range/reference
    // basilica-audio/aureate's own HubNeedle-family VU needle uses.
    constexpr float vuZeroReferenceDbfs = -18.0f;
    constexpr float vuRestDb = -20.0f;
    constexpr float vuFullScaleDb = 3.0f;

    // True-peak-margin needle (smallMeterBottom): marginDb = Ceiling -
    // getOutputTruePeakDb(), clamped [0, marginRestDb]. A large margin
    // (safe, headroom to spare) rests at the sweep's negative end; margin
    // shrinking toward 0 (signal riding the ceiling) deflects toward the
    // positive end - same convention as every other needle here.
    // marginRestDb (12 dB) is a chosen, documented "comfortably safe"
    // reference (not a measured value) - see docs/gui-mapping.md.
    constexpr float marginRestDb = 12.0f;
    constexpr float marginFullScaleDb = 0.0f;

    //==========================================================================
    // Tube bay: ONE glow zone covering all 4 tubes (components/tube-
    // glow.json's own offsetX/offsetY/width/height - a single bounding rect,
    // same "one rect, not four independent tube cutouts" simplification
    // aureate's own vent-glow zone uses, and safe for the same reason:
    // the glow sprite's own alpha is confined to the tubes themselves, so
    // subtracting/blending across the whole rect never touches the
    // honeycomb mesh or bay frame incorrectly - see tube-glow.json's own
    // "why" field).
    const juce::Rectangle<int> tubeBayZoneMasterPx { 627, 169, 468, 283 };
    const juce::Rectangle<int> tubeBayZone1x { 410, 111, 306, 185 };

    // Tube-glow breathing ballistics (see docs/gui-mapping.md's "Tube bay
    // behaviour" section for the full coupling-curve rationale): idle
    // t~=0.75 with a +/-0.05 multi-sine flicker (Flicker.h's
    // slowDriftLayers), rising toward the hard t=1.0 ceiling (the base
    // master itself) as gain reduction deepens, reaching the ceiling at
    // the SAME grFullScaleReductionDb depth the grand needle itself pins
    // at - the tubes and the needle top out together.
    constexpr float tubeGlowTauSeconds = 0.15f;
    constexpr float tubeGlowFloorDb = 0.0f;
    constexpr float tubeGlowCeilingDb = -grFullScaleReductionDb; // 12.0, positive "depth" units
    constexpr float tubeGlowIdleBreathCentre = 0.75f;
    constexpr float tubeGlowIdleBreathHalfRange = 0.05f;
    constexpr float tubeGlowPhaseSeed = 3.0f;

    constexpr int topStripHeight1x = 32;
    constexpr int topStripGap1x = 6;
    constexpr int scaleButtonWidth1x = 64;

    constexpr int baseEditorWidth = plateWidth1x;
    constexpr int baseEditorHeight = topStripHeight1x + topStripGap1x + plateHeight1x;

    constexpr std::array<float, 3> scaleSteps { 1.0f, 1.5f, 2.0f };
}
