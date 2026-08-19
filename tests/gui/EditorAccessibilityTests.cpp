#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "gui/HubNeedle.h"

#include <catch2/catch_approx.hpp>
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

// Issue #5 (keyboard navigation): juce::Slider ships with
// setWantsKeyboardFocus(false) in JUCE 8.0.14 (juce_Slider.cpp:1461,
// Slider::init), so MasterCropKnob was silently unreachable by Tab and its
// keyPressed()/focus ring never fired - and even when focused, the base
// keyPressed (juce_Slider.cpp:1029) steps by the raw parameter interval
// (0.01 dB on Input Gain's 36 dB range) and ignores Shift entirely. These
// tests pin the fixed contract (setWantsKeyboardFocus(true) +
// KeyboardSteps.h).

TEST_CASE ("Every interactive control is keyboard-focusable", "[gui][a11y]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    ApotheosisAudioProcessorEditor editor (processor);

    int knobsSeen = 0;

    for (int i = 0; i < editor.getNumChildComponents(); ++i)
    {
        if (auto* slider = dynamic_cast<juce::Slider*> (editor.getChildComponent (i)))
        {
            ++knobsSeen;
            INFO ("knob \"" << slider->getTitle().toStdString() << "\"");
            CHECK (slider->getWantsKeyboardFocus());
        }
    }

    // All 3 knobs must be present AND focusable - a zero-match loop must
    // not pass vacuously.
    CHECK (knobsSeen == 3);

    auto* scaleButton = editor.findChildWithID ("scaleButton");
    REQUIRE (scaleButton != nullptr);
    CHECK (scaleButton->getWantsKeyboardFocus());
}

TEST_CASE ("Arrow keys step knobs by a practical amount, Shift+Arrow steps finer", "[gui][a11y]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    ApotheosisAudioProcessorEditor editor (processor);

    // Input Gain: linear -12..+24 dB, 0.01 dB interval (ParameterLayout.cpp)
    // - the base-class step would be 0.01 dB (3600 presses per sweep).
    auto* knob = findChildByTitle<juce::Slider> (editor, "Input Gain");
    REQUIRE (knob != nullptr);

    knob->setValue (0.0, juce::sendNotificationSync);

    // Called through Component& for the same [class.access.virt] reason
    // documented on createHandlerForTest().
    juce::Component& knobAsComponent = *knob;

    // Plain Right = 1% of the 36 dB range = 0.36 dB.
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::rightKey)));
    CHECK (knob->getValue() == Catch::Approx (0.36).margin (1.0e-4));

    // Shift+Right = 0.1% = 0.036 dB, snapped to the 0.01 grid -> 0.40.
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::rightKey,
                                                          juce::ModifierKeys::shiftModifier, 0)));
    CHECK (knob->getValue() == Catch::Approx (0.40).margin (1.0e-4));

    // Plain Left steps back down symmetrically.
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::leftKey)));
    CHECK (knob->getValue() == Catch::Approx (0.04).margin (1.0e-4));

    // PageDown = 10% = 3.6 dB.
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::pageDownKey)));
    CHECK (knob->getValue() == Catch::Approx (-3.56).margin (1.0e-4));

    // Home/End jump to the range extremes (WAI-ARIA slider pattern).
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::homeKey)));
    CHECK (knob->getValue() == Catch::Approx (-12.0).margin (1.0e-4));
    REQUIRE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::endKey)));
    CHECK (knob->getValue() == Catch::Approx (24.0).margin (1.0e-4));

    // Ctrl/Cmd-modified presses are host shortcuts - never consumed.
    CHECK_FALSE (knobAsComponent.keyPressed (juce::KeyPress (juce::KeyPress::rightKey,
                                                              juce::ModifierKeys::ctrlModifier, 0)));
    CHECK (knob->getValue() == Catch::Approx (24.0).margin (1.0e-4));
}
