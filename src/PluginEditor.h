#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <memory>

#include "gui/HubNeedle.h"
#include "gui/MasterCropKnob.h"
#include "gui/PlateTypography.h"
#include "gui/SubtractiveGlow.h"
#include "presets/PresetBar.h"

class ApotheosisAudioProcessor;

// M3 photoreal GUI (the "victorian" faceplate design), built from the
// suite-reusable src/gui/ component family (HubNeedle, MasterCropKnob,
// SubtractiveGlow - pilot: basilica-audio/aureate's own "tubecomp" M3 GUI;
// shared with sibling requiem/tenebrae). Owner visual sign-off pending -
// see docs/gui-mapping.md and the PR this shipped in.
//
// Architecture (identical to the pilot): a SINGLE baked master image
// (resources/gui/master_victorian.png) is the sole faceplate - oak
// picture-frame border, brass plate, grand rococo GR meter (empty dial,
// baked tick arc + red zone), honeycomb tube bay at full glow, 3 small
// meters (empty dials), 3 brass knobs at 12 o'clock - and every dynamic
// element is a small, targeted live overlay drawn on top of it:
//   1. baseline master (paint())
//   2. 3x MasterCropKnob (own child components, each rotating a feathered
//      crop of its own knob's baked art)
//   3. tube-bay glow breathing (paint(), SubtractiveGlow)
//   4. 4x HubNeedle (own child components: grand Gain Reduction meter +
//      3 small Input/Output/True-Peak-Margin meters - the dial faces
//      themselves stay fully baked)
//   5. typography layer (paint(), PlateTypography - printed grand-dial
//      numerals + caption on the parchment, gilded small-meter legends
//      and knob labels on the bronze; drawn last in paint(), under the
//      needle child components, exactly like a printed dial face)
//
// This design has NO toggle-capable controls (no lever/switch element in
// the master render at all - see brand/mocks/victorian/prompts.md), so
// unlike the pilot there is no ToggleZoneSwap wiring here; that header is
// still copied into src/gui/ for suite family consistency (matching every
// sibling plugin's shared component set) but genuinely unused in this
// editor.
//
// SUPERSEDES the previous (M3-labelled but not owner-approved) filmstrip-
// knob/AnalogMeter editor - see PluginEditorLayout.h's top-of-file docs for
// the full "superseded, not deleted" file list.
class ApotheosisAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              private juce::Timer
{
public:
    explicit ApotheosisAudioProcessorEditor (ApotheosisAudioProcessor& processorToEdit);
    ~ApotheosisAudioProcessorEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    // Test/preview-only: mirrors basilica-audio/aureate's
    // setVentGlowMixForPreview()/setVentGlowElapsedSecondsForPreview() -
    // headless test binaries have no running message loop to pump real
    // timer ticks through (see tests/gui/EditorSnapshotTests.cpp's own
    // docs). Normal operation never calls these.
    void setTubeGlowMixForPreview (float t) noexcept;
    void setTubeGlowElapsedSecondsForPreview (double elapsedSeconds) noexcept;
    void recomputeTubeGlowForPreview() noexcept;

private:
    void timerCallback() override;
    void updateTubeGlowMix() noexcept;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    struct Knob
    {
        std::unique_ptr<basilica::gui::MasterCropKnob> slider;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void configureKnob (Knob& knob, const juce::String& parameterId, const juce::String& labelText);
    void applyScaleStep (int newStepIndex);
    void cycleScale();
    void drawPlateTypography (juce::Graphics& g, juce::Point<float> plateOrigin, float scale) const;

    ApotheosisAudioProcessor& audioProcessor;

    juce::Image masterImage;

    basilica::presets::PresetBar presetBar;
    juce::TextButton scaleButton;
    int scaleStepIndex = 0; // 0 = 100%, 1 = 150%, 2 = 200%

    // Reading order: grand Gain Reduction meter, then the 3 small meters
    // top-to-bottom (Input / Output / True-Peak Margin) - see
    // docs/gui-mapping.md's meter-semantics table.
    std::unique_ptr<basilica::gui::HubNeedle> gainReductionNeedle;
    std::unique_ptr<basilica::gui::HubNeedle> inputLevelNeedle;
    std::unique_ptr<basilica::gui::HubNeedle> outputLevelNeedle;
    std::unique_ptr<basilica::gui::HubNeedle> truePeakMarginNeedle;

    static constexpr int numKnobs = 3;
    std::array<Knob, numKnobs> knobs;

    basilica::gui::PlateTypography typography;
    basilica::gui::SubtractiveGlow tubeGlow;
    float tubeGlowMix = 1.0f;
    basilica::gui::GlowMixState tubeGlowState;
    juce::Rectangle<int> tubeGlowRepaintBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ApotheosisAudioProcessorEditor)
};
