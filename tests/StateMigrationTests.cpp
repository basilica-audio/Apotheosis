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

//==============================================================================
// T15 - v0.4.0 state schema v3 (see PluginProcessor::getStateInformation).
//==============================================================================

namespace
{
    // A v2-shaped APVTS XML snapshot: the eleven v0.2.0 parameter IDs, no
    // stateVersion property, none of the seven v0.4.0 IDs - the exact shape
    // ApotheosisAudioProcessor::getStateInformation() produced at v0.2.0.
    juce::String makeV2StateXml()
    {
        juce::String xml;
        xml << "<PARAMETERS>"
            << "<PARAM id=\"" << ParamIDs::inputGain << "\" value=\"5.0\"/>"
            << "<PARAM id=\"" << ParamIDs::ceiling << "\" value=\"-2.0\"/>"
            << "<PARAM id=\"" << ParamIDs::release << "\" value=\"80.0\"/>"
            << "<PARAM id=\"" << ParamIDs::lookahead << "\" value=\"6.0\"/>"
            << "<PARAM id=\"" << ParamIDs::releaseCurve << "\" value=\"2.0\"/>"
            << "<PARAM id=\"" << ParamIDs::dither << "\" value=\"2.0\"/>"
            << "<PARAM id=\"" << ParamIDs::clipMix << "\" value=\"10.0\"/>"
            << "<PARAM id=\"" << ParamIDs::attack << "\" value=\"12.0\"/>"
            << "<PARAM id=\"" << ParamIDs::autoRelease << "\" value=\"30.0\"/>"
            << "<PARAM id=\"" << ParamIDs::stereoLink << "\" value=\"80.0\"/>"
            << "<PARAM id=\"" << ParamIDs::ditherShape << "\" value=\"1.0\"/>"
            << "</PARAMETERS>";
        return xml;
    }

    float plainValueOf (ApotheosisAudioProcessor& processor, const char* id)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        return param->convertFrom0to1 (param->getValue());
    }
}

TEST_CASE ("T15: v2 state (eleven parameters, no v0.4.0 IDs) loads with every new parameter on its neutral default",
           "[state][migration][v040]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    // Perturb every v0.4.0 parameter away from its default first, so the
    // fallback assertion below cannot pass by accident.
    for (const auto* id : { ParamIDs::limitStyle, ParamIDs::oversampling, ParamIDs::osPhase, ParamIDs::tpGuard,
                            ParamIDs::noiseShaping, ParamIDs::deltaListen, ParamIDs::unityGainMonitor })
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (1.0f);
    }

    const std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (makeV2StateXml()));
    REQUIRE (xml != nullptr);

    juce::MemoryBlock v2State;
    juce::AudioProcessor::copyXmlToBinary (*xml, v2State);

    CHECK_NOTHROW (processor.setStateInformation (v2State.getData(), static_cast<int> (v2State.getSize())));

    // The eleven v0.2.0 parameters restored...
    CHECK (plainValueOf (processor, ParamIDs::inputGain) == Catch::Approx (5.0f).margin (1e-3));
    CHECK (plainValueOf (processor, ParamIDs::ceiling) == Catch::Approx (-2.0f).margin (1e-3));
    CHECK (plainValueOf (processor, ParamIDs::release) == Catch::Approx (80.0f).margin (1e-3));
    CHECK (plainValueOf (processor, ParamIDs::lookahead) == Catch::Approx (6.0f).margin (1e-3));
    CHECK (plainValueOf (processor, ParamIDs::releaseCurve) == Catch::Approx (2.0f).margin (1e-3));
    CHECK (plainValueOf (processor, ParamIDs::dither) == Catch::Approx (2.0f).margin (1e-3));
    CHECK (plainValueOf (processor, ParamIDs::clipMix) == Catch::Approx (10.0f).margin (1e-3));
    CHECK (plainValueOf (processor, ParamIDs::attack) == Catch::Approx (12.0f).margin (1e-3));
    CHECK (plainValueOf (processor, ParamIDs::autoRelease) == Catch::Approx (30.0f).margin (1e-3));
    CHECK (plainValueOf (processor, ParamIDs::stereoLink) == Catch::Approx (80.0f).margin (1e-3));
    CHECK (plainValueOf (processor, ParamIDs::ditherShape) == Catch::Approx (1.0f).margin (1e-3));

    // ...and every v0.4.0 parameter fell back to its neutral
    // (v0.2.0-bit-identical) default, not the perturbation set above.
    CHECK (plainValueOf (processor, ParamIDs::limitStyle) == Catch::Approx (0.0f).margin (1e-3)); // Classic
    CHECK (plainValueOf (processor, ParamIDs::oversampling) == Catch::Approx (0.0f).margin (1e-3)); // 4x
    CHECK (plainValueOf (processor, ParamIDs::osPhase) == Catch::Approx (0.0f).margin (1e-3)); // Minimum Phase
    CHECK (plainValueOf (processor, ParamIDs::tpGuard) == Catch::Approx (0.0f).margin (1e-3)); // Off
    CHECK (plainValueOf (processor, ParamIDs::noiseShaping) == Catch::Approx (0.0f).margin (1e-3)); // Legacy
    CHECK (plainValueOf (processor, ParamIDs::deltaListen) == Catch::Approx (0.0f).margin (1e-3)); // Off
    CHECK (plainValueOf (processor, ParamIDs::unityGainMonitor) == Catch::Approx (0.0f).margin (1e-3)); // Off

    // Processing after the migration must stay stable.
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    CHECK_NOTHROW (processor.processBlock (buffer, midi));
}

TEST_CASE ("T15: v3 round-trip preserves stateVersion=3 and foreign root-tree properties (e.g. the GUI's uiScaleStep)",
           "[state][migration][v040]")
{
    ApotheosisAudioProcessor source;
    source.prepareToPlay (48000.0, 512);

    // Non-default values on a couple of v0.4.0 parameters so the round trip
    // is not vacuous.
    for (const auto* id : { ParamIDs::limitStyle, ParamIDs::tpGuard })
    {
        auto* param = source.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (1.0f);
    }

    // A foreign (non-parameter) root-tree property, exactly how the M3
    // photoreal GUI branch persists its editor scale step. It must survive
    // the round trip untouched - state properties ride along inside the
    // APVTS tree and neither getStateInformation() nor setStateInformation()
    // may filter them.
    source.apvts.state.setProperty ("uiScaleStep", 2, nullptr);

    juce::MemoryBlock stateBlock;
    source.getStateInformation (stateBlock);

    ApotheosisAudioProcessor target;
    target.prepareToPlay (48000.0, 512);
    CHECK_NOTHROW (target.setStateInformation (stateBlock.getData(), static_cast<int> (stateBlock.getSize())));

    CHECK (target.apvts.state.getProperty ("stateVersion").toString() == "3");
    CHECK (static_cast<int> (target.apvts.state.getProperty ("uiScaleStep")) == 2);

    CHECK (plainValueOf (target, ParamIDs::limitStyle) == Catch::Approx (4.0f).margin (1e-3)); // Safe (normalised 1.0)
    CHECK (plainValueOf (target, ParamIDs::tpGuard) == Catch::Approx (1.0f).margin (1e-3)); // On

    // And a second save emits stateVersion=3 again (idempotent).
    juce::MemoryBlock secondBlock;
    target.getStateInformation (secondBlock);
    const std::unique_ptr<juce::XmlElement> xml (juce::AudioProcessor::getXmlFromBinary (secondBlock.getData(),
                                                                                         static_cast<int> (secondBlock.getSize())));
    REQUIRE (xml != nullptr);
    CHECK (juce::String (xml->getStringAttribute ("stateVersion")) == "3");
}

TEST_CASE ("T15: the original v1 fixture still loads (regression of Guarantee 7 under schema v3)",
           "[state][migration][v040]")
{
    // The v1 case above (Guarantee 7) already covers the load itself; this
    // adds the v0.4.0 angle: a v1 state must ALSO leave all seven v0.4.0
    // parameters on their neutral defaults.
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    for (const auto* id : { ParamIDs::limitStyle, ParamIDs::oversampling, ParamIDs::osPhase, ParamIDs::tpGuard,
                            ParamIDs::noiseShaping, ParamIDs::deltaListen, ParamIDs::unityGainMonitor })
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (1.0f);
    }

    const auto v1Xml = makeV1StateXml (6.0f, -3.0f, 120.0f, 8.0f, 1.0f, 1.0f, 25.0f);
    const std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (v1Xml));
    REQUIRE (xml != nullptr);

    juce::MemoryBlock v1State;
    juce::AudioProcessor::copyXmlToBinary (*xml, v1State);
    CHECK_NOTHROW (processor.setStateInformation (v1State.getData(), static_cast<int> (v1State.getSize())));

    for (const auto* id : { ParamIDs::limitStyle, ParamIDs::oversampling, ParamIDs::osPhase, ParamIDs::tpGuard,
                            ParamIDs::noiseShaping, ParamIDs::deltaListen, ParamIDs::unityGainMonitor })
        CHECK (plainValueOf (processor, id) == Catch::Approx (0.0f).margin (1e-3));
}
