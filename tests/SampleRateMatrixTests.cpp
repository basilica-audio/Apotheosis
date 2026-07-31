#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <random>

// Suite-wide hardening wave (2026-07-31): sample-rate-matrix reprepare
// coverage. None of the existing latency/robustness tests exercise a
// *sequence* of prepareToPlay() calls across widely different sample rates
// and block sizes on a single processor instance with parameter churn in
// between - LatencyTests.cpp's "Latency updates correctly when the sample
// rate changes" case only ever moves 44.1k -> 96k once, and every
// robustness test prepares exactly once. Hosts do re-prepare repeatedly
// (sample-rate changes, buffer-size renegotiation), and Apotheosis's
// reported latency is sample-rate- AND oversampling-mode-dependent
// (TruePeakLimiterEngine::prepare(), docs/manual.md's latency table), so a
// stale or partially-reset internal cache after a reprepare would show up
// exactly here.
namespace
{
    void setParam (ApotheosisAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }

    float getParam (ApotheosisAudioProcessor& processor, const char* id)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        return param->convertFrom0to1 (param->getValue());
    }

    // Churns a handful of live (non prepare-latched) parameters and runs a
    // few blocks of sine through the processor, asserting finiteness and
    // that nothing throws - the "process with parameter churn" step between
    // reprepares.
    void churnAndProcess (ApotheosisAudioProcessor& processor,
                           double sampleRate,
                           int blockSize,
                           std::mt19937& rng,
                           int numBlocks = 6)
    {
        std::uniform_real_distribution<float> unit (0.0f, 1.0f);
        juce::MidiBuffer midi;

        for (int block = 0; block < numBlocks; ++block)
        {
            setParam (processor, ParamIDs::inputGain, -6.0f + unit (rng) * 18.0f);
            setParam (processor, ParamIDs::release, 5.0f + unit (rng) * 300.0f);
            setParam (processor, ParamIDs::autoRelease, unit (rng) * 100.0f);
            setParam (processor, ParamIDs::stereoLink, unit (rng) * 100.0f);
            setParam (processor, ParamIDs::clipMix, unit (rng) * 40.0f);

            juce::AudioBuffer<float> buffer (2, blockSize);

            if (blockSize > 0)
                TestHelpers::fillWithSine (buffer, sampleRate, 220.0 + unit (rng) * 2000.0, 0.85f);

            CHECK_NOTHROW (processor.processBlock (buffer, midi));

            if (blockSize > 0)
                CHECK (TestHelpers::allSamplesFinite (buffer));
        }
    }

    // Selects an oversampling mode (prepare-latched: must be set before the
    // following prepareToPlay() call) and returns the documented total
    // latency (samples, at the project rate) for that mode at that rate,
    // per docs/manual.md's "Latency" table (default 5 ms Lookahead). Rates
    // not in the table are not exercised by this test.
    struct Mode
    {
        float oversamplingChoice; // 0 = 4x, 1 = 8x, 2 = 16x
        float osPhaseChoice;      // 0 = Minimum Phase, 1 = Linear Phase
        const char* name;
    };

    int documentedLatency (const Mode& mode, double sampleRate)
    {
        const bool linear = mode.osPhaseChoice > 0.5f;
        const bool is8x = mode.oversamplingChoice > 0.5f && mode.oversamplingChoice < 1.5f;
        const bool is16x = mode.oversamplingChoice >= 1.5f;

        if (sampleRate == 44100.0)
        {
            if (! linear) return 232;
            if (is8x || is16x) return 286;
            return 283;
        }

        if (sampleRate == 96000.0)
        {
            if (! linear) return 492;
            if (is8x || is16x) return 546;
            return 543;
        }

        if (sampleRate == 192000.0)
        {
            // Engine derates the effective factor at/above 176.4 kHz (4x
            // max), so every column collapses to the 4x figure - docs/
            // manual.md: "At 192 kHz the 8x and 16x rows equal the 4x row".
            return linear ? 1017 : 966;
        }

        FAIL ("no documented latency for this sample rate: " << sampleRate);
        return -1;
    }
}

TEST_CASE ("Sample-rate matrix: reprepare 44.1k -> 96k -> 192k (small and large blocks) "
           "survives with correct documented per-mode latency",
           "[latency][sample-rate-matrix][reprepare]")
{
    const Mode modes[] = {
        { 0.0f, 0.0f, "4x Minimum Phase" },
        { 2.0f, 1.0f, "16x Linear Phase" },
    };

    for (const auto& mode : modes)
    {
        DYNAMIC_SECTION (mode.name)
        {
            ApotheosisAudioProcessor processor;
            std::mt19937 rng (4242);

            setParam (processor, ParamIDs::oversampling, mode.oversamplingChoice);
            setParam (processor, ParamIDs::osPhase, mode.osPhaseChoice);

            // A state-survival marker: an explicit, non-default Ceiling
            // value set once, before the very first prepare(), that must
            // still read back identically after every reprepare below.
            // prepareToPlay() must never reset APVTS-owned parameter
            // state, only the DSP engine's internal buffers/caches. The
            // readback (rather than the literal -3.25f passed in) is the
            // comparison baseline because AudioParameterFloat round-trips
            // through a normalised [0,1] representation, which is not
            // exact for an arbitrary decimal - the point of this check is
            // "reprepare doesn't perturb it further", not "normalisation
            // is lossless".
            constexpr float markerCeilingDb = -3.25f;
            setParam (processor, ParamIDs::ceiling, markerCeilingDb);
            const auto markerCeilingReadback = getParam (processor, ParamIDs::ceiling);

            // --- 44.1 kHz, the starting rate -------------------------------
            processor.prepareToPlay (44100.0, 512);
            CHECK (processor.getLatencySamples() == documentedLatency (mode, 44100.0));
            churnAndProcess (processor, 44100.0, 512, rng);
            CHECK (getParam (processor, ParamIDs::ceiling) == markerCeilingReadback);

            // --- 96 kHz: small block, then large block ---------------------
            processor.prepareToPlay (96000.0, 32);
            CHECK (processor.getLatencySamples() == documentedLatency (mode, 96000.0));
            churnAndProcess (processor, 96000.0, 32, rng);
            CHECK (getParam (processor, ParamIDs::ceiling) == markerCeilingReadback);

            processor.prepareToPlay (96000.0, 8192);
            CHECK (processor.getLatencySamples() == documentedLatency (mode, 96000.0));
            churnAndProcess (processor, 96000.0, 8192, rng);
            CHECK (getParam (processor, ParamIDs::ceiling) == markerCeilingReadback);

            // --- 192 kHz: small block, then large block ---------------------
            processor.prepareToPlay (192000.0, 16);
            CHECK (processor.getLatencySamples() == documentedLatency (mode, 192000.0));
            churnAndProcess (processor, 192000.0, 16, rng);
            CHECK (getParam (processor, ParamIDs::ceiling) == markerCeilingReadback);

            processor.prepareToPlay (192000.0, 16384);
            CHECK (processor.getLatencySamples() == documentedLatency (mode, 192000.0));
            churnAndProcess (processor, 192000.0, 16384, rng);
            CHECK (getParam (processor, ParamIDs::ceiling) == markerCeilingReadback);

            // Finally, back down to 44.1 kHz (round trip): latency must
            // return to exactly what it was the first time, proving no
            // reprepare along the way left the engine in a state that
            // depends on prepare *history* rather than just the current
            // spec.
            processor.prepareToPlay (44100.0, 512);
            CHECK (processor.getLatencySamples() == documentedLatency (mode, 44100.0));
            churnAndProcess (processor, 44100.0, 512, rng);
            CHECK (TestHelpers::allSamplesFinite ([&]
            {
                juce::AudioBuffer<float> buffer (2, 512);
                TestHelpers::fillWithSine (buffer, 44100.0, 1000.0, 0.9f);
                juce::MidiBuffer midi;
                processor.processBlock (buffer, midi);
                return buffer;
            }()));
        }
    }
}

TEST_CASE ("Sample-rate matrix: reprepare with a zero-sample buffer immediately after does not crash",
           "[latency][sample-rate-matrix][reprepare]")
{
    // A narrower, cheap companion to the case above: some hosts hand over a
    // zero-length buffer on the very first callback after a reprepare
    // (buffer-size renegotiation mid-stream). Runs the full rate ladder at
    // the default mode only.
    ApotheosisAudioProcessor processor;
    juce::MidiBuffer midi;

    for (double rate : { 44100.0, 96000.0, 192000.0 })
    {
        for (int blockSize : { 1, 4096 })
        {
            processor.prepareToPlay (rate, blockSize);

            juce::AudioBuffer<float> zeroBuffer (2, 0);
            CHECK_NOTHROW (processor.processBlock (zeroBuffer, midi));
            CHECK (zeroBuffer.getNumSamples() == 0);

            juce::AudioBuffer<float> normalBuffer (2, blockSize);
            TestHelpers::fillWithSine (normalBuffer, rate, 500.0, 0.5f);
            CHECK_NOTHROW (processor.processBlock (normalBuffer, midi));
            CHECK (TestHelpers::allSamplesFinite (normalBuffer));
        }
    }
}
