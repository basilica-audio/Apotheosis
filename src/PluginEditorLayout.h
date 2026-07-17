#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

// Apotheosis's own @1x faceplate/control-bay geometry table - lives in its
// own header, rather than as an anonymous-namespace block inside
// PluginEditor.cpp, so tests/gui/EditorLayoutTests.cpp can assert layout
// invariants directly against the SAME numbers PluginEditor.cpp actually
// lays components out with, instead of a second hand-copied set of
// constants that could silently drift out of sync (same pattern as the
// suite's M3 pilot, basilica-audio/silentium's PluginEditorLayout.h).
//
// This is Apotheosis-specific, art-authored geometry. The four bay rects
// below are copied VERBATIM (px @1x, top-left-origin rectangles derived
// from centre+size) from
// .scaffold/gui-assets/faceplate-apotheosis-v1/layout-manifest.json, which
// is also the exact config the faceplate PNG was rendered from
// (render_faceplate.py bakes the engraved bay outlines at these precise
// coordinates) - so a control drawn at these rects lands exactly inside its
// engraved frame.
namespace apth::layout
{
    constexpr int plateWidth1x = 900;
    constexpr int plateHeight1x = 600;

    // Header band + roundel: NOT part of layout-manifest.json (which only
    // declares the plugin-specific bays) - render_faceplate.py derives the
    // header/roundel automatically from the canvas size, "same proportions
    // as faceplate-silentium-v1" (see that script's build_faceplate()).
    // These pixel values were measured directly off the rendered
    // faceplate_apotheosis_900x600.png (gold-ring pixel bounding box),
    // rather than re-derived from the Blender world-unit formula, to avoid
    // compounding any camera/margin conversion error - same "verify against
    // shipped pixels" discipline AnalogMeter.cpp's pivot fraction uses.
    const juce::Rectangle<int> headerBay1x { 75, 88, 750, 24 };
    const juce::Point<int> roundelCentre1x { 450, 100 };
    constexpr int roundelRadius1x = 36;

    // layout-manifest.json bays, verbatim (cx/cy/w/h -> top-left rect):
    //   meters:  cx 450 cy 197 w 700 h 95
    //   limiter: cx 195 cy 390 w 190 h 180
    //   release: cx 450 cy 390 w 270 h 180
    //   output:  cx 705 cy 390 w 190 h 180
    const juce::Rectangle<int> metersBay1x { 100, 150, 700, 95 };
    const juce::Rectangle<int> limiterBay1x { 100, 300, 190, 180 };
    const juce::Rectangle<int> releaseBay1x { 315, 300, 270, 180 };
    const juce::Rectangle<int> outputBay1x { 610, 300, 190, 180 };

    // Extra strip above the plate art for the preset bar + scale control -
    // interactive text/menus don't fit the plate's own thin engraved header
    // band at any legible size (same reasoning as Silentium's auxBay).
    constexpr int topStripHeight1x = 32;
    constexpr int topStripGap1x = 6;
    constexpr int scaleButtonWidth1x = 64;

    constexpr int baseEditorWidth = plateWidth1x;
    constexpr int baseEditorHeight = topStripHeight1x + topStripGap1x + plateHeight1x;

    constexpr std::array<float, 3> scaleSteps { 1.0f, 1.5f, 2.0f };

    // Meter strip: three AnalogMeter instances (GR / True Peak / LUFS) share
    // metersBay1x, split into three equal-width columns.
    constexpr int numMeters = 3;

    // Limiter bay (3 knobs: Input Gain, Ceiling, Clip Mix) - single row.
    constexpr int limiterCols = 3;

    // Release bay (5 controls: Attack, Release, Auto Release knobs +
    // Release Curve combo box + Lookahead knob) - 3x2 grid, 1 cell unused.
    constexpr int releaseCols = 3;
    constexpr int releaseRows = 2;

    // Output bay (1 knob: Stereo Link + 2 combo boxes: Dither, Dither
    // Shape): NOT a uniform 3-column grid like limiter/release - a plain
    // 190px-wide/3-column split left Dither/Dither Shape's combo boxes too
    // narrow (~51px) to render even short selections ("Off"/"Flat") at all
    // (JUCE's ComboBox fell back to a bare "..." with zero visible letters
    // - a real legibility bug, not just tighter truncation - see
    // docs/gui-components.md). Instead this bay is a bespoke two-row
    // layout, handled as a special case in PluginEditor.cpp's resized():
    // row 0 = Stereo Link's knob, full bay width; row 1 = Dither/Dither
    // Shape side by side, each ~half the bay width (~95px, comfortably
    // enough for their longest option text, "Streaming Safe"-length
    // choices excluded - the longest here is "16-bit"/"24-bit"/"Shaped").
    constexpr int outputCols = 2;

    constexpr int knobLabelHeight1x = 16;
    constexpr int knobDiameter1x = 72;
    constexpr int choiceBoxHeight1x = 22;
}
