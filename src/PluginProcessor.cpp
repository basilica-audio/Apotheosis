#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIds.h"
#include "params/ParameterLayout.h"

#include <BinaryData.h>

namespace
{
    // The small, Apotheosis-specific config surface PresetManager needs
    // (see src/presets/PresetManager.h's class docs) - everything else
    // about the preset system is fully generic and portable to sibling
    // plugins (see docs/preset-system-notes.md, the pilot's replication
    // recipe).
    basilica::presets::PresetManagerConfig makePresetManagerConfig()
    {
        // JucePlugin_CFBundleIdentifier expands to a raw (unquoted) token
        // sequence, not a string literal - JUCE_STRINGIFY() is the
        // documented way to turn it into one. This is always
        // "com.yvesvogl.apotheosis" here (BUNDLE_ID in CMakeLists.txt),
        // matching the "plugin" field baked into every
        // presets/factory/*.json file.
        basilica::presets::PresetManagerConfig config;
        config.pluginId = JUCE_STRINGIFY (JucePlugin_CFBundleIdentifier);
        config.pluginName = JucePlugin_Name;
        config.manufacturerName = "Yves Vogl";
        config.pluginVersion = JucePlugin_VersionString;
        // userPresetsDirectoryOverrideForTests intentionally left
        // default-constructed (empty) - production instances always use the
        // real platform-standard preset location (see PresetManager.h).
        return config;
    }

    // BinaryData symbol names are derived from the presets/factory/*.json
    // file names passed to juce_add_binary_data() in CMakeLists.txt (dots
    // become underscores) - this list must stay in sync with that SOURCES
    // list. Order here only affects factory-preset iteration order before
    // getAllPresets() re-sorts alphabetically, so it isn't otherwise
    // significant.
    std::vector<basilica::presets::FactoryPresetAsset> makeFactoryPresetAssets()
    {
        return {
            { BinaryData::default_json, BinaryData::default_jsonSize },
            { BinaryData::punchyMaster_json, BinaryData::punchyMaster_jsonSize },
            { BinaryData::denseLoudModern_json, BinaryData::denseLoudModern_jsonSize },
            { BinaryData::wideImagePreserve_json, BinaryData::wideImagePreserve_jsonSize },
            { BinaryData::streamingSafeHighLoudness_json, BinaryData::streamingSafeHighLoudness_jsonSize },
            { BinaryData::adaptiveRiding_json, BinaryData::adaptiveRiding_jsonSize },
            { BinaryData::brightClipperBlend_json, BinaryData::brightClipperBlend_jsonSize },
            { BinaryData::cleanExportDithered_json, BinaryData::cleanExportDithered_jsonSize },
        };
    }
}

//==============================================================================
ApotheosisAudioProcessor::ApotheosisAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout()),
      presetManager (apvts, makePresetManagerConfig(), makeFactoryPresetAssets())
{
    inputGainDb = apvts.getRawParameterValue (ParamIDs::inputGain);
    ceilingDb = apvts.getRawParameterValue (ParamIDs::ceiling);
    releaseMs = apvts.getRawParameterValue (ParamIDs::release);
    lookaheadMs = apvts.getRawParameterValue (ParamIDs::lookahead);
    releaseCurveChoice = apvts.getRawParameterValue (ParamIDs::releaseCurve);
    ditherChoice = apvts.getRawParameterValue (ParamIDs::dither);
    clipMixPercent = apvts.getRawParameterValue (ParamIDs::clipMix);
    attackMs = apvts.getRawParameterValue (ParamIDs::attack);
    autoReleasePercent = apvts.getRawParameterValue (ParamIDs::autoRelease);
    stereoLinkPercent = apvts.getRawParameterValue (ParamIDs::stereoLink);
    ditherShapeChoice = apvts.getRawParameterValue (ParamIDs::ditherShape);
    limitStyleChoice = apvts.getRawParameterValue (ParamIDs::limitStyle);
    oversamplingChoice = apvts.getRawParameterValue (ParamIDs::oversampling);
    osPhaseChoice = apvts.getRawParameterValue (ParamIDs::osPhase);
    tpGuardEnabled = apvts.getRawParameterValue (ParamIDs::tpGuard);
    noiseShapingChoice = apvts.getRawParameterValue (ParamIDs::noiseShaping);
    deltaListenEnabled = apvts.getRawParameterValue (ParamIDs::deltaListen);
    unityGainMonitorEnabled = apvts.getRawParameterValue (ParamIDs::unityGainMonitor);

    jassert (inputGainDb != nullptr);
    jassert (ceilingDb != nullptr);
    jassert (releaseMs != nullptr);
    jassert (lookaheadMs != nullptr);
    jassert (releaseCurveChoice != nullptr);
    jassert (ditherChoice != nullptr);
    jassert (clipMixPercent != nullptr);
    jassert (attackMs != nullptr);
    jassert (autoReleasePercent != nullptr);
    jassert (stereoLinkPercent != nullptr);
    jassert (ditherShapeChoice != nullptr);
    jassert (limitStyleChoice != nullptr);
    jassert (oversamplingChoice != nullptr);
    jassert (osPhaseChoice != nullptr);
    jassert (tpGuardEnabled != nullptr);
    jassert (noiseShapingChoice != nullptr);
    jassert (deltaListenEnabled != nullptr);
    jassert (unityGainMonitorEnabled != nullptr);

    // M2 default resolution: user "Default" preset > factory "Default"
    // preset > the ParameterLayout defaults apvts was just constructed
    // with above (see PresetManager::applyStartupDefault()'s docs).
    presetManager.applyStartupDefault();
}

ApotheosisAudioProcessor::~ApotheosisAudioProcessor() = default;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout ApotheosisAudioProcessor::createParameterLayout()
{
    return tbst::createParameterLayout();
}

//==============================================================================
const juce::String ApotheosisAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool ApotheosisAudioProcessor::acceptsMidi() const
{
    return false;
}

bool ApotheosisAudioProcessor::producesMidi() const
{
    return false;
}

bool ApotheosisAudioProcessor::isMidiEffect() const
{
    return false;
}

double ApotheosisAudioProcessor::getTailLengthSeconds() const
{
    // A limiter has no reverberant/decay tail beyond its reported latency.
    return 0.0;
}

int ApotheosisAudioProcessor::getNumPrograms()
{
    return 1;
}

int ApotheosisAudioProcessor::getCurrentProgram()
{
    return 0;
}

void ApotheosisAudioProcessor::setCurrentProgram (int)
{
}

const juce::String ApotheosisAudioProcessor::getProgramName (int)
{
    return {};
}

void ApotheosisAudioProcessor::changeProgramName (int, const juce::String&)
{
}

//==============================================================================
void ApotheosisAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32> (samplesPerBlock);
    spec.numChannels = static_cast<juce::uint32> (getTotalNumOutputChannels());

    // Seed the engine's parameters from the current APVTS state before
    // prepare() runs, so the very first block after prepareToPlay() already
    // reflects the host/session's actual parameter values. Lookahead in
    // particular must be seeded here: it is latched by the engine at
    // prepare()-time only (see TruePeakLimiterEngine::setLookaheadMs), since
    // it both sizes real-time buffers and determines the reported latency.
    engine.setInputGainDb (inputGainDb->load (std::memory_order_relaxed));
    engine.setCeilingDb (ceilingDb->load (std::memory_order_relaxed));
    engine.setReleaseMs (releaseMs->load (std::memory_order_relaxed));
    engine.setLookaheadMs (lookaheadMs->load (std::memory_order_relaxed));
    engine.setReleaseCurve (static_cast<int> (releaseCurveChoice->load (std::memory_order_relaxed)));
    engine.setDitherMode (static_cast<int> (ditherChoice->load (std::memory_order_relaxed)));
    engine.setClipMixPercent (clipMixPercent->load (std::memory_order_relaxed));
    engine.setAttackMs (attackMs->load (std::memory_order_relaxed));
    engine.setAutoReleasePercent (autoReleasePercent->load (std::memory_order_relaxed));
    engine.setStereoLinkPercent (stereoLinkPercent->load (std::memory_order_relaxed));
    engine.setDitherShape (static_cast<int> (ditherShapeChoice->load (std::memory_order_relaxed)));

    // v0.4.0: Oversampling/OS Filter are prepare-latched with exactly
    // Lookahead's contract (seeded here, read by engine.prepare(), never
    // re-applied per block) - deliberately no async re-prepare machinery in
    // this release, so a change takes effect at the next host-driven
    // prepareToPlay() only (see ParameterIds.h::oversampling). The
    // remaining v0.4.0 controls are live and also pushed per block below.
    engine.setLimitStyle (static_cast<int> (limitStyleChoice->load (std::memory_order_relaxed)));
    engine.setOversamplingFactor (static_cast<int> (oversamplingChoice->load (std::memory_order_relaxed)));
    engine.setOsPhase (static_cast<int> (osPhaseChoice->load (std::memory_order_relaxed)));
    engine.setTpGuard (tpGuardEnabled->load (std::memory_order_relaxed) >= 0.5f);
    engine.setNoiseShaping (static_cast<int> (noiseShapingChoice->load (std::memory_order_relaxed)));
    engine.setDeltaListen (deltaListenEnabled->load (std::memory_order_relaxed) >= 0.5f);
    engine.setUnityGainMonitor (unityGainMonitorEnabled->load (std::memory_order_relaxed) >= 0.5f);

    engine.prepare (spec);

    // Latency = Lookahead (converted to samples) + the 4x oversampler's own
    // round-trip latency (see docs/architecture.md).
    setLatencySamples (engine.getLatencySamples());
}

void ApotheosisAudioProcessor::releaseResources()
{
}

void ApotheosisAudioProcessor::reset()
{
    engine.reset();
}

bool ApotheosisAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mono = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();

    const auto mainOut = layouts.getMainOutputChannelSet();
    const auto mainIn = layouts.getMainInputChannelSet();

    if (mainOut != mono && mainOut != stereo)
        return false;

    if (mainOut != mainIn)
        return false;

    return true;
}

void ApotheosisAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto totalNumInputChannels = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Buses are constrained to in == out (mono or stereo), so this is
    // normally a no-op, but it's cheap insurance against stray channels.
    for (auto channel = totalNumInputChannels; channel < totalNumOutputChannels; ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    // InputGain/Ceiling/Release/ReleaseCurve/Dither/ClipMix/Attack/Auto
    // Release/Stereo Link/Dither Shape are all cheap to update every block
    // (smoothed internally where audible, no allocation either way).
    // Lookahead is deliberately NOT re-applied here - see
    // TruePeakLimiterEngine::setLookaheadMs() and prepareToPlay() above;
    // changing it takes effect only on the next prepare() cycle.
    engine.setInputGainDb (inputGainDb->load (std::memory_order_relaxed));
    engine.setCeilingDb (ceilingDb->load (std::memory_order_relaxed));
    engine.setReleaseMs (releaseMs->load (std::memory_order_relaxed));
    engine.setReleaseCurve (static_cast<int> (releaseCurveChoice->load (std::memory_order_relaxed)));
    engine.setDitherMode (static_cast<int> (ditherChoice->load (std::memory_order_relaxed)));
    engine.setClipMixPercent (clipMixPercent->load (std::memory_order_relaxed));
    engine.setAttackMs (attackMs->load (std::memory_order_relaxed));
    engine.setAutoReleasePercent (autoReleasePercent->load (std::memory_order_relaxed));
    engine.setStereoLinkPercent (stereoLinkPercent->load (std::memory_order_relaxed));
    engine.setDitherShape (static_cast<int> (ditherShapeChoice->load (std::memory_order_relaxed)));

    // v0.4.0 live controls. Oversampling/OS Filter are deliberately NOT
    // re-applied here - they are prepare-latched, exactly like Lookahead
    // (see prepareToPlay() above and ParameterIds.h::oversampling).
    engine.setLimitStyle (static_cast<int> (limitStyleChoice->load (std::memory_order_relaxed)));
    engine.setTpGuard (tpGuardEnabled->load (std::memory_order_relaxed) >= 0.5f);
    engine.setNoiseShaping (static_cast<int> (noiseShapingChoice->load (std::memory_order_relaxed)));
    engine.setDeltaListen (deltaListenEnabled->load (std::memory_order_relaxed) >= 0.5f);
    engine.setUnityGainMonitor (unityGainMonitorEnabled->load (std::memory_order_relaxed) >= 0.5f);

    juce::dsp::AudioBlock<float> block (buffer);
    engine.process (block);
}

//==============================================================================
bool ApotheosisAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* ApotheosisAudioProcessor::createEditor()
{
    return new ApotheosisAudioProcessorEditor (*this);
}

//==============================================================================
void ApotheosisAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    // State schema versioning (v0.4.0, schema v3 - independent of the
    // product version): a plain property on the APVTS root tree.
    //   v1 = the seven v0.1 parameters, no version property
    //   v2 = + attack/autoRelease/stereoLink/ditherShape, no version property
    //   v3 = + the seven v0.4.0 parameters, stateVersion="3"
    // The loader (setStateInformation below) needs no branching: a state
    // with the property absent is v1/v2 by definition (inferable from the
    // presence of the `attack` parameter, see tests/StateMigrationTests.cpp),
    // and every migration is additive-defaults - any parameter ID missing
    // from the incoming tree keeps its neutral default, which reproduces
    // the older version's behaviour exactly (Guarantee 7 lineage).
    state.setProperty ("stateVersion", "3", nullptr);

    const std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void ApotheosisAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    // Additive-defaults migration for every schema version (see
    // getStateInformation() above): replaceState() applies the parameters
    // present in the incoming tree and resets absent ones to their layout
    // defaults - which are all neutral by the v0.2.0/v0.4.0 design rules -
    // so v1/v2/v3 states all load through this single unconditional path.
    // Unknown properties (e.g. a newer schema's additions, or the GUI's
    // uiScaleStep) ride along untouched inside the tree.
    if (xmlState != nullptr && xmlState->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ApotheosisAudioProcessor();
}
