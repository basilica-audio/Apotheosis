#include "PluginProcessor.h"
#include "params/ParameterIds.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE ("State round-trip preserves non-default values of every parameter", "[state]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    auto* inputGainParam = processor.apvts.getParameter (ParamIDs::inputGain);
    auto* ceilingParam = processor.apvts.getParameter (ParamIDs::ceiling);
    auto* releaseParam = processor.apvts.getParameter (ParamIDs::release);
    auto* lookaheadParam = processor.apvts.getParameter (ParamIDs::lookahead);

    REQUIRE (inputGainParam != nullptr);
    REQUIRE (ceilingParam != nullptr);
    REQUIRE (releaseParam != nullptr);
    REQUIRE (lookaheadParam != nullptr);

    inputGainParam->setValueNotifyingHost (inputGainParam->convertTo0to1 (6.0f));
    ceilingParam->setValueNotifyingHost (ceilingParam->convertTo0to1 (-3.0f));
    releaseParam->setValueNotifyingHost (releaseParam->convertTo0to1 (200.0f));
    lookaheadParam->setValueNotifyingHost (lookaheadParam->convertTo0to1 (10.0f));

    const auto savedInputGain = inputGainParam->getValue();
    const auto savedCeiling = ceilingParam->getValue();
    const auto savedRelease = releaseParam->getValue();
    const auto savedLookahead = lookaheadParam->getValue();

    juce::MemoryBlock savedState;
    processor.getStateInformation (savedState);
    REQUIRE (savedState.getSize() > 0);

    // Reset every parameter back to its default before restoring, so the
    // round-trip assertion below can't pass by accident.
    inputGainParam->setValueNotifyingHost (inputGainParam->getDefaultValue());
    ceilingParam->setValueNotifyingHost (ceilingParam->getDefaultValue());
    releaseParam->setValueNotifyingHost (releaseParam->getDefaultValue());
    lookaheadParam->setValueNotifyingHost (lookaheadParam->getDefaultValue());

    REQUIRE (inputGainParam->getValue() != Catch::Approx (savedInputGain));
    REQUIRE (ceilingParam->getValue() != Catch::Approx (savedCeiling));
    REQUIRE (releaseParam->getValue() != Catch::Approx (savedRelease));
    REQUIRE (lookaheadParam->getValue() != Catch::Approx (savedLookahead));

    processor.setStateInformation (savedState.getData(), static_cast<int> (savedState.getSize()));

    CHECK (inputGainParam->getValue() == Catch::Approx (savedInputGain).margin (1e-6));
    CHECK (ceilingParam->getValue() == Catch::Approx (savedCeiling).margin (1e-6));
    CHECK (releaseParam->getValue() == Catch::Approx (savedRelease).margin (1e-6));
    CHECK (lookaheadParam->getValue() == Catch::Approx (savedLookahead).margin (1e-6));
}
