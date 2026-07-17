#include "PluginProcessor.h"
#include "params/ParameterIds.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{
    juce::RangedAudioParameter* requireParam (juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
    {
        auto* param = apvts.getParameter (id);
        REQUIRE (param != nullptr);
        return param;
    }

    void checkFloatRange (juce::AudioProcessorValueTreeState& apvts,
                           const juce::String& id,
                           float expectedMin,
                           float expectedMax)
    {
        auto* param = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (id));
        REQUIRE (param != nullptr);

        const auto range = param->getNormalisableRange().getRange();
        CHECK (range.getStart() == Catch::Approx (expectedMin));
        CHECK (range.getEnd() == Catch::Approx (expectedMax));
    }

    void checkFloatDefault (juce::AudioProcessorValueTreeState& apvts,
                            const juce::String& id,
                            float expectedDefault)
    {
        auto* param = requireParam (apvts, id);
        CHECK (param->getDefaultValue() == Catch::Approx (param->convertTo0to1 (expectedDefault)).margin (1e-4));
    }
}

TEST_CASE ("Processor instantiates with the expected parameters", "[processor][parameters]")
{
    ApotheosisAudioProcessor processor;
    auto& apvts = processor.apvts;

    SECTION ("plugin name")
    {
        CHECK (processor.getName() == juce::String ("Apotheosis"));
    }

    SECTION ("all documented parameter IDs resolve")
    {
        static constexpr const char* allIds[] = {
            ParamIDs::inputGain, ParamIDs::ceiling, ParamIDs::release, ParamIDs::lookahead,
            ParamIDs::releaseCurve, ParamIDs::dither, ParamIDs::clipMix,
            ParamIDs::attack, ParamIDs::autoRelease, ParamIDs::stereoLink, ParamIDs::ditherShape,
        };

        for (const auto* id : allIds)
            CHECK (apvts.getParameter (id) != nullptr);
    }

    SECTION ("total parameter count matches the v0.2.0 layout (7 v0.1 + 4 new)")
    {
        CHECK (apvts.processor.getParameters().size() == 11);
    }

    SECTION ("Input Gain: defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::inputGain, 0.0f);
        checkFloatRange (apvts, ParamIDs::inputGain, -12.0f, 24.0f);
    }

    SECTION ("Ceiling: defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::ceiling, -1.0f);
        checkFloatRange (apvts, ParamIDs::ceiling, -12.0f, 0.0f);
    }

    SECTION ("Release: defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::release, 50.0f);
        checkFloatRange (apvts, ParamIDs::release, 5.0f, 1000.0f);
    }

    SECTION ("Lookahead: defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::lookahead, 5.0f);
        checkFloatRange (apvts, ParamIDs::lookahead, 0.1f, 20.0f);
    }

    SECTION ("Release Curve: default index and choices")
    {
        auto* param = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ParamIDs::releaseCurve));
        REQUIRE (param != nullptr);
        CHECK (param->getIndex() == 0);
        CHECK (param->choices.size() == 3);
        CHECK (param->choices[0] == "Exponential");
        CHECK (param->choices[1] == "Linear");
        CHECK (param->choices[2] == "Smooth");
    }

    SECTION ("Dither: default index and choices")
    {
        auto* param = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ParamIDs::dither));
        REQUIRE (param != nullptr);
        CHECK (param->getIndex() == 0);
        CHECK (param->choices.size() == 3);
        CHECK (param->choices[0] == "Off");
        CHECK (param->choices[1] == "16-bit");
        CHECK (param->choices[2] == "24-bit");
    }

    SECTION ("Clip Mix: defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::clipMix, 0.0f);
        checkFloatRange (apvts, ParamIDs::clipMix, 0.0f, 100.0f);
    }

    //==========================================================================
    // v0.2.0 deep-dive additions (docs/design-brief.md). Every default below
    // is also the value that reproduces v1's exact prior behaviour - see
    // tests/RegressionTests.cpp for the bit-identical-output assertion.
    //==========================================================================

    SECTION ("Attack: defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::attack, 0.0f);
        checkFloatRange (apvts, ParamIDs::attack, 0.0f, 50.0f);
    }

    SECTION ("Auto Release: defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::autoRelease, 0.0f);
        checkFloatRange (apvts, ParamIDs::autoRelease, 0.0f, 100.0f);
    }

    SECTION ("Stereo Link: defaults and range")
    {
        checkFloatDefault (apvts, ParamIDs::stereoLink, 100.0f);
        checkFloatRange (apvts, ParamIDs::stereoLink, 0.0f, 100.0f);
    }

    SECTION ("Dither Shape: default index and choices")
    {
        auto* param = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ParamIDs::ditherShape));
        REQUIRE (param != nullptr);
        CHECK (param->getIndex() == 0);
        CHECK (param->choices.size() == 2);
        CHECK (param->choices[0] == "Flat");
        CHECK (param->choices[1] == "Shaped");
    }
}
