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
    auto* releaseCurveParam = processor.apvts.getParameter (ParamIDs::releaseCurve);
    auto* ditherParam = processor.apvts.getParameter (ParamIDs::dither);
    auto* clipMixParam = processor.apvts.getParameter (ParamIDs::clipMix);
    auto* attackParam = processor.apvts.getParameter (ParamIDs::attack);
    auto* autoReleaseParam = processor.apvts.getParameter (ParamIDs::autoRelease);
    auto* stereoLinkParam = processor.apvts.getParameter (ParamIDs::stereoLink);
    auto* ditherShapeParam = processor.apvts.getParameter (ParamIDs::ditherShape);

    REQUIRE (inputGainParam != nullptr);
    REQUIRE (ceilingParam != nullptr);
    REQUIRE (releaseParam != nullptr);
    REQUIRE (lookaheadParam != nullptr);
    REQUIRE (releaseCurveParam != nullptr);
    REQUIRE (ditherParam != nullptr);
    REQUIRE (clipMixParam != nullptr);
    REQUIRE (attackParam != nullptr);
    REQUIRE (autoReleaseParam != nullptr);
    REQUIRE (stereoLinkParam != nullptr);
    REQUIRE (ditherShapeParam != nullptr);

    inputGainParam->setValueNotifyingHost (inputGainParam->convertTo0to1 (6.0f));
    ceilingParam->setValueNotifyingHost (ceilingParam->convertTo0to1 (-3.0f));
    releaseParam->setValueNotifyingHost (releaseParam->convertTo0to1 (200.0f));
    lookaheadParam->setValueNotifyingHost (lookaheadParam->convertTo0to1 (10.0f));
    releaseCurveParam->setValueNotifyingHost (releaseCurveParam->convertTo0to1 (2.0f)); // "Smooth"
    ditherParam->setValueNotifyingHost (ditherParam->convertTo0to1 (1.0f)); // "16-bit"
    clipMixParam->setValueNotifyingHost (clipMixParam->convertTo0to1 (40.0f));
    attackParam->setValueNotifyingHost (attackParam->convertTo0to1 (12.0f));
    autoReleaseParam->setValueNotifyingHost (autoReleaseParam->convertTo0to1 (55.0f));
    stereoLinkParam->setValueNotifyingHost (stereoLinkParam->convertTo0to1 (25.0f));
    ditherShapeParam->setValueNotifyingHost (ditherShapeParam->convertTo0to1 (1.0f)); // "Shaped"

    const auto savedInputGain = inputGainParam->getValue();
    const auto savedCeiling = ceilingParam->getValue();
    const auto savedRelease = releaseParam->getValue();
    const auto savedLookahead = lookaheadParam->getValue();
    const auto savedReleaseCurve = releaseCurveParam->getValue();
    const auto savedDither = ditherParam->getValue();
    const auto savedClipMix = clipMixParam->getValue();
    const auto savedAttack = attackParam->getValue();
    const auto savedAutoRelease = autoReleaseParam->getValue();
    const auto savedStereoLink = stereoLinkParam->getValue();
    const auto savedDitherShape = ditherShapeParam->getValue();

    juce::MemoryBlock savedState;
    processor.getStateInformation (savedState);
    REQUIRE (savedState.getSize() > 0);

    // Reset every parameter back to its default before restoring, so the
    // round-trip assertion below can't pass by accident.
    inputGainParam->setValueNotifyingHost (inputGainParam->getDefaultValue());
    ceilingParam->setValueNotifyingHost (ceilingParam->getDefaultValue());
    releaseParam->setValueNotifyingHost (releaseParam->getDefaultValue());
    lookaheadParam->setValueNotifyingHost (lookaheadParam->getDefaultValue());
    releaseCurveParam->setValueNotifyingHost (releaseCurveParam->getDefaultValue());
    ditherParam->setValueNotifyingHost (ditherParam->getDefaultValue());
    clipMixParam->setValueNotifyingHost (clipMixParam->getDefaultValue());
    attackParam->setValueNotifyingHost (attackParam->getDefaultValue());
    autoReleaseParam->setValueNotifyingHost (autoReleaseParam->getDefaultValue());
    stereoLinkParam->setValueNotifyingHost (stereoLinkParam->getDefaultValue());
    ditherShapeParam->setValueNotifyingHost (ditherShapeParam->getDefaultValue());

    REQUIRE (inputGainParam->getValue() != Catch::Approx (savedInputGain));
    REQUIRE (ceilingParam->getValue() != Catch::Approx (savedCeiling));
    REQUIRE (releaseParam->getValue() != Catch::Approx (savedRelease));
    REQUIRE (lookaheadParam->getValue() != Catch::Approx (savedLookahead));
    REQUIRE (releaseCurveParam->getValue() != Catch::Approx (savedReleaseCurve));
    REQUIRE (ditherParam->getValue() != Catch::Approx (savedDither));
    REQUIRE (clipMixParam->getValue() != Catch::Approx (savedClipMix));
    REQUIRE (attackParam->getValue() != Catch::Approx (savedAttack));
    REQUIRE (autoReleaseParam->getValue() != Catch::Approx (savedAutoRelease));
    REQUIRE (stereoLinkParam->getValue() != Catch::Approx (savedStereoLink));
    REQUIRE (ditherShapeParam->getValue() != Catch::Approx (savedDitherShape));

    processor.setStateInformation (savedState.getData(), static_cast<int> (savedState.getSize()));

    CHECK (inputGainParam->getValue() == Catch::Approx (savedInputGain).margin (1e-6));
    CHECK (ceilingParam->getValue() == Catch::Approx (savedCeiling).margin (1e-6));
    CHECK (releaseParam->getValue() == Catch::Approx (savedRelease).margin (1e-6));
    CHECK (lookaheadParam->getValue() == Catch::Approx (savedLookahead).margin (1e-6));
    CHECK (releaseCurveParam->getValue() == Catch::Approx (savedReleaseCurve).margin (1e-6));
    CHECK (ditherParam->getValue() == Catch::Approx (savedDither).margin (1e-6));
    CHECK (clipMixParam->getValue() == Catch::Approx (savedClipMix).margin (1e-6));
    CHECK (attackParam->getValue() == Catch::Approx (savedAttack).margin (1e-6));
    CHECK (autoReleaseParam->getValue() == Catch::Approx (savedAutoRelease).margin (1e-6));
    CHECK (stereoLinkParam->getValue() == Catch::Approx (savedStereoLink).margin (1e-6));
    CHECK (ditherShapeParam->getValue() == Catch::Approx (savedDitherShape).margin (1e-6));
}
