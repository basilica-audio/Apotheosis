#include "PluginProcessor.h"
#include "params/ParameterIds.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// v0.2.0 deep-dive additions (docs/design-brief.md) - Guarantee 7 (state
// migration tolerance): old (v1) saved state with only the seven original
// parameter IDs loads without crashing, and all four new parameters fall
// back to their v2 defaults; v2 state with unknown-to-v1 IDs must not crash
// a hypothetical older build (forward-tolerant round-trip).
namespace
{
    // A v1-shaped APVTS XML snapshot: only the seven original parameter IDs
    // (attack/autoRelease/stereoLink/ditherShape absent entirely), the exact
    // shape ApotheosisAudioProcessor::getStateInformation() produced before
    // this pass (juce::AudioProcessorValueTreeState's XML format:
    // <PARAMETERS><PARAM id="..." value="..."/>...</PARAMETERS>).
    juce::String makeV1StateXml (float inputGain, float ceiling, float release, float lookahead,
                                 float releaseCurveIndex, float ditherIndex, float clipMix)
    {
        juce::String xml;
        xml << "<PARAMETERS>"
            << "<PARAM id=\"" << ParamIDs::inputGain << "\" value=\"" << inputGain << "\"/>"
            << "<PARAM id=\"" << ParamIDs::ceiling << "\" value=\"" << ceiling << "\"/>"
            << "<PARAM id=\"" << ParamIDs::release << "\" value=\"" << release << "\"/>"
            << "<PARAM id=\"" << ParamIDs::lookahead << "\" value=\"" << lookahead << "\"/>"
            << "<PARAM id=\"" << ParamIDs::releaseCurve << "\" value=\"" << releaseCurveIndex << "\"/>"
            << "<PARAM id=\"" << ParamIDs::dither << "\" value=\"" << ditherIndex << "\"/>"
            << "<PARAM id=\"" << ParamIDs::clipMix << "\" value=\"" << clipMix << "\"/>"
            << "</PARAMETERS>";
        return xml;
    }
}

TEST_CASE ("Guarantee 7: old v1 state (seven parameters, no v0.2.0 IDs) loads without crashing and every new parameter falls back to its v2 default",
           "[state][migration][guarantee7]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    // Perturb the four new parameters away from their defaults first, so
    // the "falls back to default" assertion below can't pass by accident.
    for (const auto* id : { ParamIDs::attack, ParamIDs::autoRelease, ParamIDs::stereoLink, ParamIDs::ditherShape })
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (0.9f);
    }

    const auto v1Xml = makeV1StateXml (6.0f, -3.0f, 120.0f, 8.0f, 1.0f, 1.0f, 25.0f);
    const std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (v1Xml));
    REQUIRE (xml != nullptr);

    juce::MemoryBlock v1State;
    juce::AudioProcessor::copyXmlToBinary (*xml, v1State);

    CHECK_NOTHROW (processor.setStateInformation (v1State.getData(), static_cast<int> (v1State.getSize())));

    // The seven v1 parameters restored correctly...
    auto getPlain = [&processor] (const char* id) -> float
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        return param->convertFrom0to1 (param->getValue());
    };

    CHECK (getPlain (ParamIDs::inputGain) == Catch::Approx (6.0f).margin (1e-3));
    CHECK (getPlain (ParamIDs::ceiling) == Catch::Approx (-3.0f).margin (1e-3));
    CHECK (getPlain (ParamIDs::release) == Catch::Approx (120.0f).margin (1e-3));
    CHECK (getPlain (ParamIDs::lookahead) == Catch::Approx (8.0f).margin (1e-3));
    CHECK (getPlain (ParamIDs::releaseCurve) == Catch::Approx (1.0f).margin (1e-3));
    CHECK (getPlain (ParamIDs::dither) == Catch::Approx (1.0f).margin (1e-3));
    CHECK (getPlain (ParamIDs::clipMix) == Catch::Approx (25.0f).margin (1e-3));

    // ...and all four new v0.2.0 parameters fell back to their own defaults
    // (each individually the "off"/backward-compatible value per Guarantee
    // 1), NOT the 0.9-normalised perturbation set above - APVTS's
    // replaceState() leaves any parameter absent from the incoming
    // ValueTree at its current live value only if the underlying
    // juce::ValueTree property is missing AND no listener resets it; the
    // authoritative behaviour this test pins down is what
    // ApotheosisAudioProcessor::setStateInformation() actually produces.
    CHECK (getPlain (ParamIDs::attack) == Catch::Approx (0.0f).margin (1e-3));
    CHECK (getPlain (ParamIDs::autoRelease) == Catch::Approx (0.0f).margin (1e-3));
    CHECK (getPlain (ParamIDs::stereoLink) == Catch::Approx (100.0f).margin (1e-3));
    CHECK (getPlain (ParamIDs::ditherShape) == Catch::Approx (0.0f).margin (1e-3));

    // Processing after the migration must stay stable.
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    CHECK_NOTHROW (processor.processBlock (buffer, midi));
}

TEST_CASE ("Guarantee 7: v0.2.0 state with an unknown-to-v1 extra parameter ID does not crash (forward-tolerant round-trip)",
           "[state][migration][guarantee7]")
{
    // Simulates a hypothetical future/newer build's state (or a corrupted/
    // hand-edited session file) carrying an ID this build doesn't know
    // about, alongside the full current v0.2.0 parameter set - mirrors the
    // suite's existing "unknown IDs ignored" pattern from the M2 preset
    // system (src/presets/PresetManager.cpp's applyPlainValues()).
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    juce::String xml;
    xml << "<PARAMETERS>"
        << "<PARAM id=\"" << ParamIDs::inputGain << "\" value=\"3.0\"/>"
        << "<PARAM id=\"" << ParamIDs::ceiling << "\" value=\"-1.0\"/>"
        << "<PARAM id=\"" << ParamIDs::release << "\" value=\"50.0\"/>"
        << "<PARAM id=\"" << ParamIDs::lookahead << "\" value=\"5.0\"/>"
        << "<PARAM id=\"" << ParamIDs::releaseCurve << "\" value=\"0.0\"/>"
        << "<PARAM id=\"" << ParamIDs::dither << "\" value=\"0.0\"/>"
        << "<PARAM id=\"" << ParamIDs::clipMix << "\" value=\"0.0\"/>"
        << "<PARAM id=\"" << ParamIDs::attack << "\" value=\"15.0\"/>"
        << "<PARAM id=\"" << ParamIDs::autoRelease << "\" value=\"40.0\"/>"
        << "<PARAM id=\"" << ParamIDs::stereoLink << "\" value=\"70.0\"/>"
        << "<PARAM id=\"" << ParamIDs::ditherShape << "\" value=\"1.0\"/>"
        << "<PARAM id=\"futureParameterFromV3\" value=\"42.0\"/>"
        << "</PARAMETERS>";

    const std::unique_ptr<juce::XmlElement> parsedXml (juce::XmlDocument::parse (xml));
    REQUIRE (parsedXml != nullptr);

    juce::MemoryBlock v3State;
    juce::AudioProcessor::copyXmlToBinary (*parsedXml, v3State);

    CHECK_NOTHROW (processor.setStateInformation (v3State.getData(), static_cast<int> (v3State.getSize())));

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    CHECK_NOTHROW (processor.processBlock (buffer, midi));
}
