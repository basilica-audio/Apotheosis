#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Suite-reusable rotary knob backed by a feathered circular CROP of the
// design's own master render, rotated live via juce::AffineTransform -
// generalises basilica-audio/silentium's v0.3.9 "item 4" INNER-DISC
// technique (see that repo's PluginEditor.cpp knobDiscLayout docs) into a
// standalone, per-plugin-reusable juce::Slider rather than a per-plugin
// anonymous-namespace draw table.
//
// The crop is taken at CONTENT_FRACTION (~94%) of the knob's own measured
// radius and feathered to fully transparent at its own edge, so the
// rotating content never reaches the knob's baked outer rim/specular
// highlight - that rim stays part of the STATIC master render underneath,
// unrotated at every angle, which is what structurally rules out a
// co-rotating highlight or a visible double-knob seam (the exact defect
// silentium's plain full-disc variant hit before the INNER-DISC fix).
//
// Because the crop is sampled directly from the master's own baked-at-rest
// (12 o'clock) knob art, a value at the parameter's normalised proportion
// 0.5 draws with ZERO live rotation - i.e. pixel-identical to the baked
// master underneath at that one value, with no seam at all. Other values
// rotate the crop by (proportion - 0.5) * sweep degrees, honestly reflecting
// the parameter's actual value (not always "reset to neutral-looking" at
// open) - the same convention any real rotary control uses.
namespace basilica::gui
{
    class MasterCropKnob : public juce::Slider
    {
    public:
        // masterImage: the design's full master render (shared, read-only -
        // the crop is copied out of it once here, this component never
        // holds a reference to the original past construction).
        // centreInMasterPx/knobRadiusInMasterPx: this knob's own measured
        // centre/radius in the MASTER image's own pixel space (not the @1x
        // layout table - see PluginEditorLayout.h's knobMasterGeometry
        // docs for why the two are kept separate).
        // contentFraction: how much of the measured radius the crop's fully
        // opaque content covers before feathering to transparent (~0.94).
        MasterCropKnob (const juce::Image& masterImage, juce::Point<float> centreInMasterPx,
                        float knobRadiusInMasterPx, float contentFraction = 0.94f,
                        float minAngleDegIn = -135.0f, float maxAngleDegIn = 135.0f);
        ~MasterCropKnob() override;

        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;

        // WCAG 2.1.1 Keyboard (issue #5): WAI-ARIA-style stepping (Arrow
        // 1%, Shift+Arrow fine, PageUp/Down 10%, Home/End extremes) via
        // KeyboardSteps.h - juce::Slider's own keyPressed (JUCE 8.0.14,
        // juce_Slider.cpp:1029) steps by the raw parameter interval
        // (impractically fine here) and swallows Shift entirely.
        bool keyPressed (const juce::KeyPress& key) override;

        // Normalised slider proportion [0,1] -> absolute rotation in
        // degrees, clockwise from straight up - 0.5 = 0deg (12 o'clock,
        // the crop's own baked rest pose). Exposed for unit testing.
        static float angleForProportionDegrees (double normalisedValue, float minAngleDeg, float maxAngleDeg) noexcept;

        // Builds the feathered circular crop image: fully opaque inside
        // radiusPx*contentFraction*(1-featherFraction), fading linearly to
        // fully transparent at radiusPx*contentFraction. Exposed as an
        // independently testable static function (see
        // tests/gui/MasterCropKnobTests.cpp) - message-thread only (does
        // real per-pixel work), never called from paint()/the audio thread.
        static juce::Image buildFeatheredCrop (const juce::Image& masterImage, juce::Point<float> centreInMasterPx,
                                               float radiusPx, float contentFraction, float featherFraction = 0.12f);

    private:
        juce::Image cropImage;
        const float minAngleDeg;
        const float maxAngleDeg;

        static constexpr int normalDragSensitivity = 200;
        static constexpr int fineDragSensitivity = normalDragSensitivity * 8;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterCropKnob)
    };
}
