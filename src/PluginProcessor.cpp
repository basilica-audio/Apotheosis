#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "params/ParameterIds.h"
#include "params/ParameterLayout.h"

//==============================================================================
ApotheosisAudioProcessor::ApotheosisAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    inputGainDb = apvts.getRawParameterValue (ParamIDs::inputGain);
    ceilingDb = apvts.getRawParameterValue (ParamIDs::ceiling);
    releaseMs = apvts.getRawParameterValue (ParamIDs::release);
    lookaheadMs = apvts.getRawParameterValue (ParamIDs::lookahead);

    jassert (inputGainDb != nullptr);
    jassert (ceilingDb != nullptr);
    jassert (releaseMs != nullptr);
    jassert (lookaheadMs != nullptr);
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

    // InputGain/Ceiling/Release are cheap to update every block (smoothed
    // internally by the engine). Lookahead is deliberately NOT re-applied
    // here - see TruePeakLimiterEngine::setLookaheadMs() and prepareToPlay()
    // above; changing it takes effect only on the next prepare() cycle.
    engine.setInputGainDb (inputGainDb->load (std::memory_order_relaxed));
    engine.setCeilingDb (ceilingDb->load (std::memory_order_relaxed));
    engine.setReleaseMs (releaseMs->load (std::memory_order_relaxed));

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
    const auto state = apvts.copyState();
    const std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void ApotheosisAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState != nullptr && xmlState->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ApotheosisAudioProcessor();
}
