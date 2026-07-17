#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

#include "gui/AnalogMeter.h"
#include "gui/BasilicaLookAndFeel.h"
#include "gui/FilmstripKnob.h"
#include "presets/PresetBar.h"

class ApotheosisAudioProcessor;

// M3 photoreal skeuomorphic editor, built from the suite-reusable src/gui/
// component family (FilmstripKnob, BasilicaLookAndFeel, AnalogMeter - copied
// verbatim from the M3 pilot, basilica-audio/silentium) plus Apotheosis's
// own pre-rendered faceplate PNG and layout table (see PluginEditorLayout.h,
// which is copied 1:1 from
// .scaffold/gui-assets/faceplate-apotheosis-v1/layout-manifest.json). Every
// visible control is wired to a real APVTS parameter or a real metering
// value - no dead decoration, per the basilica-gui-design skill's binding
// spec.
//
// Unlike Silentium (all continuous knobs + two boolean toggles), Apotheosis
// has three discrete AudioParameterChoice parameters (Release Curve,
// Dither, Dither Shape) and NO boolean parameters - so this editor has no
// FilmstripToggle instances (the component is still copied into src/gui/
// for suite consistency, matching every sibling plugin's shared component
// family, but genuinely unused here) and instead uses plain juce::ComboBox
// for the three choices, styled with BasilicaLookAndFeel's verified-contrast
// gold-on-dark colour pair rather than a bespoke filmstrip asset - there is
// no photoreal rotary/toggle equivalent for an N-way discrete choice in the
// current asset set, and a functional, accessible, on-brand combo box is
// preferable to inventing a new asset family for three controls.
//
// Meter strip: three AnalogMeter instances (Gain Reduction / True Peak /
// LUFS), fed every timer tick from the processor's existing metering
// atomics (ApotheosisAudioProcessor::getGainReductionDb() /
// getOutputTruePeakDb() / getMomentaryLufs() - already published by
// TruePeakLimiterEngine specifically for this M3 GUI, see its class docs).
// See PluginEditor.cpp's timerCallback() for the exact per-meter dB mapping
// rationale (the shared VU face's baked tick table is generic, not
// per-meter, per the M3 briefing).
class ApotheosisAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              private juce::Timer
{
public:
    explicit ApotheosisAudioProcessorEditor (ApotheosisAudioProcessor& processorToEdit);
    ~ApotheosisAudioProcessorEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        std::unique_ptr<basilica::gui::FilmstripKnob> slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    struct Choice
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<ComboBoxAttachment> attachment;
    };

    void configureKnob (Knob& knob, const juce::String& parameterId, const juce::String& labelText, bool isSetupParam);
    void configureChoice (Choice& choice, const juce::String& parameterId, const juce::String& labelText);
    void applyScaleStep (int newStepIndex);
    void cycleScale();

    ApotheosisAudioProcessor& audioProcessor;

    basilica::gui::BasilicaLookAndFeel lookAndFeel;

    juce::Image facePlateImage1x, facePlateImage2x;
    juce::Image brandIconImage;

    basilica::presets::PresetBar presetBar;
    juce::TextButton scaleButton;
    int scaleStepIndex = 0; // 0 = 100%, 1 = 150%, 2 = 200%

    // Gain Reduction / True Peak / LUFS, in that left-to-right reading
    // order (matches TruePeakLimiterEngine's own signal-flow: detection ->
    // ceiling adherence -> resulting loudness).
    basilica::gui::AnalogMeter gainReductionMeter;
    basilica::gui::AnalogMeter truePeakMeter;
    basilica::gui::AnalogMeter lufsMeter;

    static constexpr int numKnobs = 8;
    std::array<Knob, numKnobs> knobs;

    static constexpr int numChoices = 3;
    std::array<Choice, numChoices> choices;

    // Lookahead is prepare-time-latched (a "setup" parameter, not a live
    // performance knob - see ParameterIds.h::lookahead and
    // TruePeakLimiterEngine::setLookaheadMs) - its knob cell is set apart
    // with a dashed amber "setup" frame, painted directly from this stored
    // bounds (computed once per resized() call, in screen space) rather
    // than recomputed inline in paint(), so the frame can never drift from
    // the knob it encloses.
    juce::Rectangle<int> lookaheadSetupFrameBounds;

    juce::Label titleLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ApotheosisAudioProcessorEditor)
};
