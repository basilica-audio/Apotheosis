#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "dsp/TruePeakLimiterEngine.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE ("getLatencySamples() reports Lookahead plus the oversampler's detection latency", "[latency]")
{
    ApotheosisAudioProcessor processor;

    // Before prepareToPlay, no engine has been prepared yet - JUCE's default
    // AudioProcessor latency is 0.
    CHECK (processor.getLatencySamples() == 0);

    processor.prepareToPlay (48000.0, 512);

    // Cross-check against a standalone engine prepared identically, with the
    // default Lookahead (5 ms): the processor must report exactly what the
    // engine computes, not an approximation of it.
    TruePeakLimiterEngine referenceEngine;
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = 48000.0;
    spec.maximumBlockSize = 512;
    spec.numChannels = 2;
    referenceEngine.prepare (spec);

    CHECK (processor.getLatencySamples() == referenceEngine.getLatencySamples());
    CHECK (processor.getLatencySamples() > 0); // Lookahead + oversampling always contribute some latency
}

TEST_CASE ("Latency equals lookaheadSamples + detectionLatency for an explicit Lookahead value", "[latency]")
{
    constexpr double sampleRate = 44100.0;
    constexpr float lookaheadMs = 10.0f;

    TruePeakLimiterEngine engine;
    engine.setLookaheadMs (lookaheadMs);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = 256;
    spec.numChannels = 2;
    engine.prepare (spec);

    // Independent reference for the oversampler's own round-trip latency,
    // prepared identically.
    juce::dsp::Oversampling<float> referenceOversampler (
        2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    referenceOversampler.initProcessing (256);
    const auto detectionLatency = static_cast<int> (std::round (referenceOversampler.getLatencyInSamples()));

    const auto expectedLookaheadSamples = juce::roundToInt (lookaheadMs * 0.001 * sampleRate);
    const auto expectedTotal = expectedLookaheadSamples + detectionLatency;

    CHECK (engine.getLatencySamples() == expectedTotal);
}

TEST_CASE ("Latency updates correctly when the sample rate changes", "[latency]")
{
    ApotheosisAudioProcessor processor;

    processor.prepareToPlay (44100.0, 512);
    const auto latencyAt44k = processor.getLatencySamples();

    processor.prepareToPlay (96000.0, 512);
    const auto latencyAt96k = processor.getLatencySamples();

    CHECK (latencyAt44k > 0);
    CHECK (latencyAt96k > 0);
    // At a fixed Lookahead (ms), a higher sample rate means more lookahead
    // *samples*, so total latency should scale up too. Not asserting an
    // exact ratio - that also depends on the oversampler's own latency,
    // which JUCE's internal filter design determines.
    CHECK (latencyAt96k > latencyAt44k);
}

TEST_CASE ("Latency is stable across repeated prepareToPlay calls at the same sample rate", "[latency]")
{
    ApotheosisAudioProcessor processor;

    processor.prepareToPlay (48000.0, 256);
    const auto firstLatency = processor.getLatencySamples();

    processor.prepareToPlay (48000.0, 256);
    const auto secondLatency = processor.getLatencySamples();

    CHECK (firstLatency == secondLatency);
}
