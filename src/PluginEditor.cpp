#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "presets/Localisation.h"

#include <BinaryData.h>

namespace
{
    constexpr int knobSize = 100;
    constexpr int textBoxHeight = 20;
    constexpr int labelHeight = 20;
    constexpr int margin = 20;
    constexpr int numKnobs = 8;
    constexpr int choiceBoxHeight = 24;
    constexpr int numChoices = 3;
    constexpr int choiceRowHeight = labelHeight + choiceBoxHeight + margin / 2;
    constexpr int presetBarHeight = 28;
    constexpr int editorWidth = margin * 2 + numKnobs * knobSize + (numKnobs - 1) * margin;
    constexpr int editorHeight = margin * 3 + presetBarHeight + labelHeight + knobSize + textBoxHeight + margin / 2 + choiceRowHeight;

    // M2 i18n frame (.scaffold/specs/preset-system-m2.md): selects German
    // (resources/i18n/de.txt) or falls through to English, once, at editor
    // construction - see Localisation.h's docs. `presetBar` is a member
    // initialised via the constructor's initialiser list, and its own
    // constructor already calls TRANS() on every button label - member
    // initialisers run in declaration order regardless of the order they're
    // written in, so this helper (called from presetBar's own initialiser
    // expression below) is what actually guarantees installLocalisation()
    // runs before presetBar exists, not a installLocalisation() call in the
    // constructor *body*, which would run too late.
    basilica::presets::PresetManager& initLocalisationThenGetPresetManager (ApotheosisAudioProcessor& processor)
    {
        basilica::presets::installLocalisation (BinaryData::de_txt, BinaryData::de_txtSize);
        return processor.presetManager;
    }
}

ApotheosisAudioProcessorEditor::ApotheosisAudioProcessorEditor (ApotheosisAudioProcessor& processorToEdit)
    : juce::AudioProcessorEditor (&processorToEdit),
      audioProcessor (processorToEdit),
      presetBar (initLocalisationThenGetPresetManager (processorToEdit))
{
    addAndMakeVisible (presetBar);

    configureKnob (inputGainKnob, ParamIDs::inputGain, "Input Gain");
    configureKnob (ceilingKnob, ParamIDs::ceiling, "Ceiling");
    configureKnob (releaseKnob, ParamIDs::release, "Release");
    configureKnob (attackKnob, ParamIDs::attack, "Attack");
    configureKnob (lookaheadKnob, ParamIDs::lookahead, "Lookahead");
    configureKnob (autoReleaseKnob, ParamIDs::autoRelease, "Auto Release");
    configureKnob (stereoLinkKnob, ParamIDs::stereoLink, "Stereo Link");
    configureKnob (clipMixKnob, ParamIDs::clipMix, "Clip Mix");

    configureChoice (releaseCurveChoice, ParamIDs::releaseCurve, "Release Curve");
    configureChoice (ditherChoice, ParamIDs::dither, "Dither");
    configureChoice (ditherShapeChoice, ParamIDs::ditherShape, "Dither Shape");

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

    // ComboBoxAttachment does not populate the box itself (see its JUCE doc
    // comment); pull the choice strings straight from the live APVTS
    // parameter (AudioParameterChoice::getAllValueStrings() returns its
    // `choices` array) rather than duplicating the string list here, so the
    // GUI can never drift out of sync with ParameterLayout.cpp. Item IDs are
    // 1-based to match ComboBox's convention; ComboBoxAttachment maps them
    // back to the parameter's 0-based choice index.
    if (auto* parameter = audioProcessor.apvts.getParameter (parameterId))
        choice.box.addItemList (parameter->getAllValueStrings(), 1);

    choice.label.setText (labelText, juce::dontSendNotification);
    choice.label.setJustificationType (juce::Justification::centred);
    choice.label.attachToComponent (&choice.box, false);
    addAndMakeVisible (choice.label);

    choice.attachment = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, parameterId, choice.box);
}

void ApotheosisAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (margin);

    presetBar.setBounds (bounds.removeFromTop (presetBarHeight));
    bounds.removeFromTop (margin);

    auto knobRow = bounds.removeFromTop (labelHeight + knobSize + textBoxHeight);
    knobRow.removeFromTop (labelHeight); // room for the attached labels above each knob

    const auto slotWidth = knobRow.getWidth() / numKnobs;

    for (auto* knob : { &inputGainKnob, &ceilingKnob, &releaseKnob, &attackKnob,
                         &lookaheadKnob, &autoReleaseKnob, &stereoLinkKnob, &clipMixKnob })
        knob->slider.setBounds (knobRow.removeFromLeft (slotWidth).reduced (margin / 2, 0));

    bounds.removeFromTop (margin / 2);

    auto choiceRow = bounds.removeFromTop (choiceRowHeight);
    choiceRow.removeFromTop (labelHeight); // room for the attached labels above each combo box

    const auto choiceSlotWidth = choiceRow.getWidth() / numChoices;
    releaseCurveChoice.box.setBounds (choiceRow.removeFromLeft (choiceSlotWidth).reduced (margin / 2, 0).withHeight (choiceBoxHeight));
    ditherChoice.box.setBounds (choiceRow.removeFromLeft (choiceSlotWidth).reduced (margin / 2, 0).withHeight (choiceBoxHeight));
    ditherShapeChoice.box.setBounds (choiceRow.removeFromLeft (choiceSlotWidth).reduced (margin / 2, 0).withHeight (choiceBoxHeight));
}
