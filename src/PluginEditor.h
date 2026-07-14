#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class ApotheosisAudioProcessor;

// A simple, functional v0.1 editor: one rotary slider per parameter, bound
// to the APVTS via SliderAttachment. A custom vector-drawn GUI is a later
// milestone; this is deliberately plain but fully wired and usable.
class ApotheosisAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ApotheosisAudioProcessorEditor (ApotheosisAudioProcessor& processorToEdit);
    ~ApotheosisAudioProcessorEditor() override;

    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    // One knob + label per parameter, in signal-flow order.
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    void configureKnob (Knob& knob, const juce::String& parameterId, const juce::String& labelText);

    ApotheosisAudioProcessor& audioProcessor;

    Knob inputGainKnob;
    Knob ceilingKnob;
    Knob releaseKnob;
    Knob lookaheadKnob;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ApotheosisAudioProcessorEditor)
};
