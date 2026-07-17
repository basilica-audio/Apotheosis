#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

// Accessibility-handler-level tests for the M3 photoreal editor, ported
// from the suite's M3 pilot (basilica-audio/silentium's
// tests/gui/EditorAccessibilityTests.cpp) and adapted to Apotheosis's own
// parameter set (no boolean toggles; three AudioParameterChoice controls;
// three AnalogMeter instances instead of two).
// juce::ScopedJuceInitialiser_GUI is installed once for the whole test
// binary in tests/TestMain.cpp, so constructing Components is safe here
// even though this is a headless console executable with no running message
// loop or native window/peer.
//
// Deliberately calls createAccessibilityHandler() directly rather than the
// more commonly used getAccessibilityHandler(): the latter (JUCE 8.0.14
// juce_Component.cpp:3323-3326) only returns a handler once the component
// has a live native window peer (getWindowHandle() != nullptr), which this
// headless, no-message-loop test binary never has. createAccessibilityHandler()
// is public API specifically meant to be safely callable/overridable
// independent of any live OS accessibility bridge (see its own docs in
// juce_Component.h) - callers a step removed from the OS bridge (like this
// test) are exactly the documented exception to "should rarely be called
// directly".
namespace
{
    // All FilmstripKnob/AnalogMeter instances and the scale button are
    // direct children of the editor itself (see PluginEditor.cpp's
    // addAndMakeVisible calls - none of them live inside a further nested
    // sub-container), so a flat (non-recursive) scan of direct children is
    // sufficient and avoids needing any additional test-only accessors on
    // the editor.
    template <typename ComponentType>
    ComponentType* findChildByTitle (juce::Component& parent, const juce::String& title)
    {
        for (int i = 0; i < parent.getNumChildComponents(); ++i)
        {
            if (auto* typed = dynamic_cast<ComponentType*> (parent.getChildComponent (i)))
                if (typed->getTitle() == title)
                    return typed;
        }

        return nullptr;
    }

    std::unique_ptr<juce::AccessibilityHandler> createHandlerForTest (juce::Component& component)
    {
        return component.createAccessibilityHandler();
    }
}

TEST_CASE ("Knob accessibility value strings include their declared unit", "[gui][a11y]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    ApotheosisAudioProcessorEditor editor (processor);

    struct Expectation
    {
        const char* label;
        const char* unitSuffix;
    };

    // One representative knob per unit declared in ParameterLayout.cpp
    // (.withLabel("dB"/"ms"/"%")).
    const Expectation expectations[] = {
        { "Input Gain", "dB" },
        { "Attack", "ms" },
        { "Stereo Link", "%" },
    };

    for (const auto& expectation : expectations)
    {
        auto* knob = findChildByTitle<basilica::gui::FilmstripKnob> (editor, expectation.label);
        REQUIRE (knob != nullptr);

        const auto handler = createHandlerForTest (*knob);
        REQUIRE (handler != nullptr);

        auto* valueInterface = handler->getValueInterface();
        REQUIRE (valueInterface != nullptr);

        const auto valueText = valueInterface->getCurrentValueAsString();
        INFO ("knob \"" << expectation.label << "\" accessible value = \"" << valueText.toStdString() << "\"");
        CHECK (valueText.endsWith (expectation.unitSuffix));
    }
}

TEST_CASE ("Lookahead's setup-parameter distinction is exposed to accessibility clients", "[gui][a11y]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    ApotheosisAudioProcessorEditor editor (processor);

    // Lookahead is prepare-time-latched (see ParameterIds.h::lookahead) -
    // its knob's accessible title and description must communicate that,
    // not just its dashed-frame visual treatment (see PluginEditor.cpp's
    // configureKnob()/paint() docs), so an AT user gets the same
    // information a sighted user reads from the frame.
    auto* knob = findChildByTitle<basilica::gui::FilmstripKnob> (editor, "Lookahead (Setup)");
    REQUIRE (knob != nullptr);
    CHECK (knob->getDescription().isNotEmpty());
    CHECK (knob->getDescription().containsIgnoreCase ("prepare"));
}

TEST_CASE ("Choice controls expose their current selection as an accessible value", "[gui][a11y]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    ApotheosisAudioProcessorEditor editor (processor);

    auto* releaseCurveBox = findChildByTitle<juce::ComboBox> (editor, "Release Curve");
    REQUIRE (releaseCurveBox != nullptr);

    const auto handler = createHandlerForTest (*releaseCurveBox);
    REQUIRE (handler != nullptr);

    auto* valueInterface = handler->getValueInterface();
    REQUIRE (valueInterface != nullptr);

    // Default index 0 = "Exponential" (ParameterLayout.cpp).
    const auto valueText = valueInterface->getCurrentValueAsString();
    INFO ("Release Curve accessible value = \"" << valueText.toStdString() << "\"");
    CHECK (valueText.isNotEmpty());
}

TEST_CASE ("All three meters are present with their own distinct accessible titles", "[gui][a11y]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    ApotheosisAudioProcessorEditor editor (processor);

    for (const auto* title : { "Gain Reduction meter", "True Peak meter", "LUFS meter" })
    {
        auto* meter = findChildByTitle<basilica::gui::AnalogMeter> (editor, title);
        INFO ("looking for meter titled \"" << title << "\"");
        REQUIRE (meter != nullptr);

        const auto handler = createHandlerForTest (*meter);
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
    // which only posts an async command message (JUCE 8.0.14
    // juce_Button.cpp:359-362) that would need a running message loop to
    // ever actually fire, which this headless test binary doesn't have.
    REQUIRE (scaleButton->onClick);
    scaleButton->onClick();

    CHECK (scaleButton->getButtonText() == "150%");
    CHECK (scaleButton->getTitle().contains ("150%"));
    CHECK_FALSE (scaleButton->getTitle().contains ("100%"));
}
