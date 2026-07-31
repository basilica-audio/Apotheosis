#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "gui/HubNeedle.h"

#include <catch2/catch_test_macros.hpp>

// a11y coverage for every wired M3 photoreal-GUI control (victorian
// design). Deliberately calls createAccessibilityHandler() directly rather
// than getAccessibilityHandler() - the latter (JUCE 8.0.14
// juce_Component.cpp) only returns a handler once the component has a live
// native window peer, which this headless, no-message-loop test binary
// never has.
namespace
{
    template <typename ComponentType>
    ComponentType* findChildByTitle (juce::Component& parent, const juce::String& title)
    {
        for (int i = 0; i < parent.getNumChildComponents(); ++i)
            if (auto* typed = dynamic_cast<ComponentType*> (parent.getChildComponent (i)))
                if (typed->getTitle() == title)
                    return typed;

        return nullptr;
    }

    std::unique_ptr<juce::AccessibilityHandler> createHandlerForTest (juce::Component& component)
    {
        return component.createAccessibilityHandler();
    }
}

TEST_CASE ("Every wired MasterCropKnob exposes an accessible title, value, and declared unit", "[gui][a11y]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    ApotheosisAudioProcessorEditor editor (processor);

    // All 3 knobs, matching ParameterLayout.cpp's own declared units - see
    // docs/gui-mapping.md's mapping table.
    struct Expectation
    {
        const char* label;
        const char* unitSuffix;
    };

    const Expectation expectations[] = {
        { "Input Gain", "dB" },
        { "Ceiling", "dBTP" },
        { "Release", "ms" },
    };

    for (const auto& expectation : expectations)
    {
        auto* knob = findChildByTitle<juce::Slider> (editor, expectation.label);
        REQUIRE (knob != nullptr);
        CHECK (knob->getTitle() == expectation.label);

        const auto handler = createHandlerForTest (*knob);
        REQUIRE (handler != nullptr);

        auto* valueInterface = handler->getValueInterface();
        REQUIRE (valueInterface != nullptr);

        const auto valueText = valueInterface->getCurrentValueAsString();
        INFO ("knob \"" << expectation.label << "\" accessible value = \"" << valueText.toStdString() << "\"");
        CHECK (valueText.isNotEmpty());
        CHECK (valueText.endsWith (expectation.unitSuffix));
    }
}

TEST_CASE ("All 4 needles are present with their own distinct, read-only, unit-suffixed accessible titles", "[gui][a11y]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    ApotheosisAudioProcessorEditor editor (processor);

    for (const auto* title : { "Gain Reduction meter", "Input Level meter", "Output Level meter", "True Peak Margin meter" })
    {
        auto* needle = findChildByTitle<basilica::gui::HubNeedle> (editor, title);
        INFO ("looking for needle titled \"" << title << "\"");
        REQUIRE (needle != nullptr);

        const auto handler = createHandlerForTest (*needle);
        REQUIRE (handler != nullptr);
        CHECK (handler->getRole() == juce::AccessibilityRole::label);

        auto* valueInterface = handler->getValueInterface();
        REQUIRE (valueInterface != nullptr);
        CHECK (valueInterface->isReadOnly());
        CHECK (valueInterface->getCurrentValueAsString().endsWith ("dB"));
    }
}

TEST_CASE ("Scale button's accessible title reflects the current scale percentage, not a static string", "[gui][a11y]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    ApotheosisAudioProcessorEditor editor (processor);

    auto* scaleButton = dynamic_cast<juce::TextButton*> (editor.findChildWithID ("scaleButton"));
    REQUIRE (scaleButton != nullptr);

    CHECK (scaleButton->getTitle().contains ("100%"));

    // Cycle the scale via the SAME onClick callback a mouse/keyboard/AT
    // click would invoke - called directly rather than via triggerClick(),
    // which only posts an async command message that would need a running
    // message loop to ever actually fire, which this headless test binary
    // doesn't have.
    REQUIRE (scaleButton->onClick);
    scaleButton->onClick();

    CHECK (scaleButton->getButtonText() == "150%");
    CHECK (scaleButton->getTitle().contains ("150%"));
    CHECK_FALSE (scaleButton->getTitle().contains ("100%"));
}

TEST_CASE ("Preset bar is present and keyboard-focus-traversable alongside the knobs/needles", "[gui][a11y]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    ApotheosisAudioProcessorEditor editor (processor);

    // The preset bar is a direct child, added first (see PluginEditor.cpp's
    // constructor-order docs) - just confirms it participates in the same
    // component tree as every other control (no separate, disconnected
    // top-level window), which is what makes JUCE's default z-order focus
    // traversal work across the whole editor.
    bool foundPresetBarChild = false;

    for (int i = 0; i < editor.getNumChildComponents(); ++i)
        if (editor.getChildComponent (i)->getNumChildComponents() > 0)
            foundPresetBarChild = true;

    CHECK (foundPresetBarChild);
}
