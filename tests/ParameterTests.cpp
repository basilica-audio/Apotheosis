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

    SECTION ("total parameter count matches the v0.4.0 layout (7 v0.1 + 4 v0.2.0 + 7 v0.4.0)")
    {
        // NOTE (v0.4.0): this assertion necessarily changed 11 -> 18 when
        // the seven v0.4.0 parameters were added - an unavoidable
        // pre-existing-test edit alongside the brief's three sanctioned
        // ones, called out explicitly in the PR description.
        CHECK (apvts.processor.getParameters().size() == 18);
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

//==============================================================================
// v0.4.0 SOTA DSP additions (see ParameterIds.h). Every default below is
// the value that reproduces v0.2.0's exact prior output - see
// tests/RegressionTests.cpp's golden-fixture comparison.
//==============================================================================

TEST_CASE ("v0.4.0 parameters: IDs resolve, defaults are neutral, ranges/choices match the brief", "[processor][parameters][v040]")
{
    ApotheosisAudioProcessor processor;
    auto& apvts = processor.apvts;

    SECTION ("all seven v0.4.0 parameter IDs resolve")
    {
        static constexpr const char* newIds[] = {
            ParamIDs::limitStyle, ParamIDs::oversampling, ParamIDs::osPhase, ParamIDs::tpGuard,
            ParamIDs::noiseShaping, ParamIDs::deltaListen, ParamIDs::unityGainMonitor,
        };

        for (const auto* id : newIds)
            CHECK (apvts.getParameter (id) != nullptr);
    }

    SECTION ("Style: default Classic, five choices")
    {
        auto* param = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ParamIDs::limitStyle));
        REQUIRE (param != nullptr);
        CHECK (param->getIndex() == 0);
        CHECK (param->choices.size() == 5);
        CHECK (param->choices[0] == "Classic");
        CHECK (param->choices[1] == "Transparent");
        CHECK (param->choices[2] == "Punchy");
        CHECK (param->choices[3] == "Bus");
        CHECK (param->choices[4] == "Safe");
    }

    SECTION ("Oversampling: default 4x (the v0.2.0 chain), three choices")
    {
        auto* param = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ParamIDs::oversampling));
        REQUIRE (param != nullptr);
        CHECK (param->getIndex() == 0);
        CHECK (param->choices.size() == 3);
        CHECK (param->choices[0] == "4x");
        CHECK (param->choices[1] == "8x");
        CHECK (param->choices[2] == "16x");
    }

    SECTION ("OS Filter: default Minimum Phase, two choices")
    {
        auto* param = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ParamIDs::osPhase));
        REQUIRE (param != nullptr);
        CHECK (param->getIndex() == 0);
        CHECK (param->choices.size() == 2);
        CHECK (param->choices[0] == "Minimum Phase");
        CHECK (param->choices[1] == "Linear Phase");
    }

    SECTION ("True Peak Guard: bool, default Off")
    {
        auto* param = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (ParamIDs::tpGuard));
        REQUIRE (param != nullptr);
        CHECK (param->get() == false);
    }

    SECTION ("Noise Shaping: default Legacy (bit-identical v0.2.0 dither), two choices")
    {
        auto* param = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ParamIDs::noiseShaping));
        REQUIRE (param != nullptr);
        CHECK (param->getIndex() == 0);
        CHECK (param->choices.size() == 2);
        CHECK (param->choices[0] == "Legacy");
        CHECK (param->choices[1] == "Weighted");
    }

    SECTION ("Delta: bool, default Off")
    {
        auto* param = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (ParamIDs::deltaListen));
        REQUIRE (param != nullptr);
        CHECK (param->get() == false);
    }

    SECTION ("Unity Gain: bool, default Off")
    {
        auto* param = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (ParamIDs::unityGainMonitor));
        REQUIRE (param != nullptr);
        CHECK (param->get() == false);
    }
}

//==============================================================================
// T14 - choice-mapping freeze. Guards ParameterIds.h's binding
// never-extend-a-choice-list rule: choice parameters store NORMALISED
// values, so appending an entry to an existing list would silently re-map
// every saved session's stored value to a different index. The tables below
// hardcode the exact v0.2.0 normalised->index mapping for every
// PRE-EXISTING choice parameter; if any list is ever extended (or
// reordered), these fail loudly.
//==============================================================================

TEST_CASE ("T14: pre-existing choice parameters' normalised-to-index mapping is frozen at the v0.2.0 tables",
           "[processor][parameters][choicefreeze][v040]")
{
    ApotheosisAudioProcessor processor;
    auto& apvts = processor.apvts;

    struct ChoiceFreeze
    {
        const char* id;
        std::vector<const char*> expectedChoices; // frozen v0.2.0 contents, in order
    };

    const std::vector<ChoiceFreeze> frozen = {
        { ParamIDs::releaseCurve, { "Exponential", "Linear", "Smooth" } },
        { ParamIDs::dither, { "Off", "16-bit", "24-bit" } },
        { ParamIDs::ditherShape, { "Flat", "Shaped" } },
    };

    for (const auto& entry : frozen)
    {
        CAPTURE (entry.id);

        auto* param = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (entry.id));
        REQUIRE (param != nullptr);

        // Exact size - an appended entry would change every stored
        // normalised value's meaning even if the old entries kept their
        // positions.
        REQUIRE (param->choices.size() == static_cast<int> (entry.expectedChoices.size()));

        const auto numChoices = param->choices.size();

        for (int index = 0; index < numChoices; ++index)
        {
            CAPTURE (index);
            CHECK (param->choices[index] == entry.expectedChoices[static_cast<size_t> (index)]);

            // The v0.2.0 normalised encoding: index / (numChoices - 1).
            const auto expectedNormalised = numChoices > 1
                                                 ? static_cast<float> (index) / static_cast<float> (numChoices - 1)
                                                 : 0.0f;

            CHECK (param->convertTo0to1 (static_cast<float> (index)) == Catch::Approx (expectedNormalised).margin (1e-6));
            CHECK (juce::roundToInt (param->convertFrom0to1 (expectedNormalised)) == index);
        }
    }
}
