#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <atomic>

// Suite-reusable pivot-centred VU-needle overlay - the "tubecomp" faceplate
// family's needle component (basilica-audio wave: aureate/requiem/tenebrae/
// apotheosis all share this design and its master render), adapted here for
// Apotheosis's own "victorian" design.
//
// The dial FACE (plate, bezel, tick marks) is baked into the design's
// master render (resources/gui/master_victorian.png) - this component draws
// ONLY the live needle sprite on top of it, rotated about the sprite's own
// measured hub pivot via a live juce::AffineTransform (never a pre-rotated
// frame stack - see needle-*.json's own provenance notes for why the
// master-extraction pipeline deliberately does not rotate the sprite to a
// canonical pose: doing so would resample and soften the master's own
// pixels).
//
// CRITICAL (binding rule, see the M3 GUI briefing): the sprite's pivot is
// the needle's HUB CENTRE, not the visible rod end - components/needle-
// *.json's own pivotXInMaster/pivotYInMaster fields already encode this,
// and this component's pivotXFraction/pivotYFraction constructor parameters
// must be derived from that same point (never the rod end), or the needle
// base will visibly lift off its hub as it rotates.
//
// VALUE->ANGLE MAPPING (diverges from the original tubecomp pilot, and this
// is the one deliberate, documented adaptation): tubecomp's own HubNeedle
// hardcoded a 9-point PIECEWISE table (its dial has 9 printed numeric
// labels to calibrate against). The victorian design's dials carry NO
// numerals at all (brand/mocks/victorian/prompts.md's own "IMPORTANT SCALE
// CORRECTION" note: they were deliberately dropped from the production
// master, tick marks only) - analysis/measure_dial_ticks.py therefore
// measures each dial's tick arc as a single angular SPAN rather than a
// calibrated per-label table, and PluginEditorLayout.h maps each meter's
// own chosen dB-ish range onto that span LINEARLY. This component is
// generalised accordingly: instead of one shared static piecewise-table
// function, each instance carries its OWN (restValue, fullScaleValue,
// restAngleDeg, fullScaleAngleDeg) quadruple, set at construction - a
// strict superset of tubecomp's capability (a 2-point linear map is a
// degenerate piecewise table), and what makes a single component class
// usable for all four of this design's differently-calibrated needles
// (Gain Reduction / Input / Output / True-Peak-Margin) without four
// near-duplicate subclasses.
namespace basilica::gui
{
    class HubNeedle : public juce::Component
    {
    public:
        struct Assets
        {
            // The master-extracted needle sprite - PIVOT-CENTRED canvas
            // (pivot sits at the sprite's own exact canvas centre, fraction
            // 0.5/0.5 - see needle-*.json's pivotXFrac/pivotYFrac), so no
            // additional pivot-offset maths is needed when rotating it
            // about its own centre.
            juce::Image needleSprite;
        };

        // pivotXFraction/pivotYFraction: where the needle's hub pivot sits,
        // as a fraction of this component's own local bounds - measured
        // once against the master render (see PluginEditorLayout.h's
        // per-needle pivot docs) and passed in here.
        //
        // spriteSizeFraction: the needle sprite's own drawn diameter, as a
        // fraction of jmin(width,height) of this component's bounds.
        //
        // bakedAngleDegIn: the sprite's own rest pose in the master render
        // it was extracted from (needle-*.json's bakedAngleDeg) - rotation
        // applied each frame is (targetAngle - bakedAngleDegIn), NOT
        // targetAngle alone (see paint()'s docs).
        //
        // restValue/fullScaleValue -> restAngleDeg/fullScaleAngleDeg: this
        // needle's own linear value->angle calibration (see this file's
        // top-of-file docs on why this replaced tubecomp's piecewise
        // table). restValue need not be numerically smaller than
        // fullScaleValue - e.g. the true-peak-margin needle maps a
        // SHRINKING margin (12 -> 0 dB) onto an INCREASING angle (rest ->
        // full scale), which this ordering-agnostic mapping supports
        // directly (see linearAngleDegreesForValue()'s docs).
        //
        // The constructor also seeds BOTH targetDb and the ballistic-
        // smoothed reading to restValue (not a hardcoded 0.0f, unlike the
        // tubecomp pilot): 0.0f is only a sensible idle default for
        // needles whose OWN restValue happens to be 0 (Gain Reduction).
        // For a needle like the true-peak-margin meter (restValue=12,
        // fullScaleValue=0), defaulting to a literal 0.0f would render it
        // fully deflected into the "hot" end at construction, before the
        // first real reading ever arrives - a real, once-live defect this
        // fixes (caught by tests/gui/EditorSnapshotTests.cpp's needle-
        // rotation proof).
        HubNeedle (Assets assetsIn, juce::String accessibleTitle,
                  float pivotXFraction, float pivotYFraction, float spriteSizeFraction,
                  float bakedAngleDegIn,
                  float restValue, float fullScaleValue, float restAngleDeg, float fullScaleAngleDeg);
        ~HubNeedle() override;

        // Thread-safe (plain atomic store): the instantaneous value, in
        // this needle's own native units (already domain-converted by the
        // caller - e.g. PluginEditor.cpp's timerCallback() turns a raw dBFS
        // reading into VU-referenced dB before calling this), written from
        // the audio thread (or the editor's own polling timer). Ballistic
        // smoothing is applied separately, on the GUI thread, so this is
        // real-time safe to call from anywhere.
        void setTargetDb (float newTargetDb) noexcept { targetDb.store (newTargetDb, std::memory_order_relaxed); }

        // Advances the ballistic smoothing by dtSeconds and repaints if the
        // smoothed value changed meaningfully - called from the editor's own
        // timer (see PluginEditor.cpp), NOT owned internally by a
        // juce::Timer on this component, so headless tests can drive it
        // deterministically without a running message loop.
        void tick (float dtSeconds) noexcept;

        // Test/preview-only: seeds both the raw target and the ballistic-
        // smoothed reading to the same value immediately, bypassing the
        // ramp - mirrors basilica-audio/silentium's AnalogMeter::
        // setImmediateDbForPreview() rationale (headless test binaries have
        // no running message loop to pump real ticks through). Normal
        // operation never calls this.
        void setImmediateDbForPreview (float db) noexcept;

        void paint (juce::Graphics& g) override;
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

        // One-pole ballistic integration step, exposed as a pure/static
        // function so it is directly unit-testable without a running timer.
        static float stepBallistics (float currentSmoothed, float target, float dtSeconds, float tauSeconds) noexcept;

        // value -> angle in degrees, clamped to [restValue, fullScaleValue]
        // (in whichever order they were given) and linearly interpolated
        // onto [restAngleDeg, fullScaleAngleDeg] - see this file's
        // top-of-file docs for why this design's dials (no baked numerals)
        // use a plain linear map rather than tubecomp's piecewise table.
        // Degrees are clockwise from straight-up (12 o'clock), matching
        // components/needle-*.json's own bakedAngleConvention.
        static float linearAngleDegreesForValue (float value, float restValue, float fullScaleValue,
                                                  float restAngleDeg, float fullScaleAngleDeg) noexcept;

        // Instance-bound convenience wrapper around the static function
        // above, using this needle's own stored calibration - what
        // paint() actually calls.
        float angleDegreesForValue (float value) const noexcept;

        static constexpr float ballisticsTauSeconds = 0.25f;

    private:
        class ValueInterface;

        Assets assets;
        juce::String title;

        std::atomic<float> targetDb { 0.0f };
        float smoothedDb = 0.0f;

        const float pivotXFraction;
        const float pivotYFraction;
        const float spriteSizeFraction;
        const float bakedAngleDeg;

        const float restValue;
        const float fullScaleValue;
        const float restAngleDeg;
        const float fullScaleAngleDeg;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HubNeedle)
    };
}
