#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "params/ParameterIds.h"

namespace
{
    constexpr int knobSize = 100;
    constexpr int textBoxHeight = 20;
    constexpr int labelHeight = 20;
    constexpr int margin = 20;
    constexpr int numKnobs = 5;
    constexpr int choiceBoxHeight = 24;
    constexpr int choiceRowHeight = labelHeight + choiceBoxHeight + margin / 2;
    constexpr int editorWidth = margin * 2 + numKnobs * knobSize + (numKnobs - 1) * margin;
    constexpr int editorHeight = margin * 2 + labelHeight + knobSize + textBoxHeight + margin / 2 + choiceRowHeight;
}

ApotheosisAudioProcessorEditor::ApotheosisAudioProcessorEditor (ApotheosisAudioProcessor& processorToEdit)
    : juce::AudioProcessorEditor (&processorToEdit),
      audioProcessor (processorToEdit)
{
    configureKnob (inputGainKnob, ParamIDs::inputGain, "Input Gain");
    configureKnob (ceilingKnob, ParamIDs::ceiling, "Ceiling");
    configureKnob (releaseKnob, ParamIDs::release, "Release");
    configureKnob (lookaheadKnob, ParamIDs::lookahead, "Lookahead");
    configureKnob (clipMixKnob, ParamIDs::clipMix, "Clip Mix");

    configureChoice (releaseCurveChoice, ParamIDs::releaseCurve, "Release Curve");
    configureChoice (ditherChoice, ParamIDs::dither, "Dither");

    setResizable (false, false);
    setSize (editorWidth, editorHeight);
}

ApotheosisAudioProcessorEditor::~ApotheosisAudioProcessorEditor() = default;

void ApotheosisAudioProcessorEditor::configureKnob (Knob& knob, const juce::String& parameterId, const juce::String& labelText)
{
    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, knobSize, textBoxHeight);
    addAndMakeVisible (knob.slider);

    knob.label.setText (labelText, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    // false => label sits above the slider it tracks; JUCE repositions it
    // automatically whenever the slider's bounds change, so resized() only
    // needs to place the sliders themselves.
    knob.label.attachToComponent (&knob.slider, false);
    addAndMakeVisible (knob.label);

    knob.attachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, parameterId, knob.slider);
}

void ApotheosisAudioProcessorEditor::configureChoice (Choice& choice, const juce::String& parameterId, const juce::String& labelText)
{
    addAndMakeVisible (choice.box);

    choice.label.setText (labelText, juce::dontSendNotification);
    choice.label.setJustificationType (juce::Justification::centred);
    choice.label.attachToComponent (&choice.box, false);
    addAndMakeVisible (choice.label);

    choice.attachment = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, parameterId, choice.box);
}

void ApotheosisAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (margin);

    auto knobRow = bounds.removeFromTop (labelHeight + knobSize + textBoxHeight);
    knobRow.removeFromTop (labelHeight); // room for the attached labels above each knob

    const auto slotWidth = knobRow.getWidth() / numKnobs;

    for (auto* knob : { &inputGainKnob, &ceilingKnob, &releaseKnob, &lookaheadKnob, &clipMixKnob })
        knob->slider.setBounds (knobRow.removeFromLeft (slotWidth).reduced (margin / 2, 0));

    bounds.removeFromTop (margin / 2);

    auto choiceRow = bounds.removeFromTop (choiceRowHeight);
    choiceRow.removeFromTop (labelHeight); // room for the attached labels above each combo box

    const auto choiceSlotWidth = choiceRow.getWidth() / 2;
    releaseCurveChoice.box.setBounds (choiceRow.removeFromLeft (choiceSlotWidth).reduced (margin / 2, 0).withHeight (choiceBoxHeight));
    ditherChoice.box.setBounds (choiceRow.removeFromLeft (choiceSlotWidth).reduced (margin / 2, 0).withHeight (choiceBoxHeight));
}
