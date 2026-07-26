#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/TruePeakLimiterEngine.h"
#include "presets/PresetManager.h"

// Apotheosis: a lookahead brickwall true-peak limiter. Signal flow lives in
// TruePeakLimiterEngine (src/dsp) so it stays unit-testable independent of
// this AudioProcessor; this class is just APVTS + host plumbing around it.
class ApotheosisAudioProcessor final : public juce::AudioProcessor
{
public:
    ApotheosisAudioProcessor();
    ~ApotheosisAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    // M2 preset system (.scaffold/specs/preset-system-m2.md,
    // src/presets/PresetManager.h). Constructed after apvts (its
    // constructor registers APVTS parameter listeners) and public so
    // ApotheosisAudioProcessorEditor's PresetBar can talk to it directly -
    // the same "processor owns it, editor references it" pattern apvts
    // itself already uses.
    basilica::presets::PresetManager presetManager;

    // Metering readout for a future GUI (roadmap M3) or any other message-
    // thread consumer; safe to poll from any thread (relaxed atomics owned
    // by the engine, updated once per processed block).
    float getGainReductionDb() const noexcept { return engine.getGainReductionDb(); }
    float getOutputTruePeakDb() const noexcept { return engine.getOutputTruePeakDb(); }
    float getMomentaryLufs() const noexcept { return engine.getMomentaryLufs(); }
    float getShortTermLufs() const noexcept { return engine.getShortTermLufs(); }
    float getIntegratedLufs() const noexcept { return engine.getIntegratedLufs(); }

    // v0.4.0 metering pack (F5/F2): spec-gated LRA, resettable true-peak
    // max hold and deepest-GR hold - see TruePeakLimiterEngine.h.
    float getLoudnessRangeLu() const noexcept { return engine.getLoudnessRangeLu(); }
    float getTruePeakMaxHoldDb() const noexcept { return engine.getTruePeakMaxHoldDb(); }
    void resetTruePeakMaxHold() noexcept { engine.resetTruePeakMaxHold(); }
    float getMaxGainReductionDb() const noexcept { return engine.getMaxGainReductionDb(); }
    void resetMaxGainReduction() noexcept { engine.resetMaxGainReduction(); }

private:
    TruePeakLimiterEngine engine;

    // Raw atomic pointers into the APVTS-managed parameter values, resolved
    // once at construction time so processBlock() never has to search for
    // them (no allocation/locks on the audio thread).
    std::atomic<float>* inputGainDb = nullptr;
    std::atomic<float>* ceilingDb = nullptr;
    std::atomic<float>* releaseMs = nullptr;
    std::atomic<float>* lookaheadMs = nullptr;
    std::atomic<float>* releaseCurveChoice = nullptr;
    std::atomic<float>* ditherChoice = nullptr;
    std::atomic<float>* clipMixPercent = nullptr;

    // v0.2.0 deep-dive additions (docs/design-brief.md).
    std::atomic<float>* attackMs = nullptr;
    std::atomic<float>* autoReleasePercent = nullptr;
    std::atomic<float>* stereoLinkPercent = nullptr;
    std::atomic<float>* ditherShapeChoice = nullptr;

    // v0.4.0 SOTA DSP additions (see ParameterIds.h). oversamplingChoice/
    // osPhaseChoice are prepare-latched (read in prepareToPlay() only, like
    // lookaheadMs); the rest are pushed every block.
    std::atomic<float>* limitStyleChoice = nullptr;
    std::atomic<float>* oversamplingChoice = nullptr;
    std::atomic<float>* osPhaseChoice = nullptr;
    std::atomic<float>* tpGuardEnabled = nullptr;
    std::atomic<float>* noiseShapingChoice = nullptr;
    std::atomic<float>* deltaListenEnabled = nullptr;
    std::atomic<float>* unityGainMonitorEnabled = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ApotheosisAudioProcessor)
};
