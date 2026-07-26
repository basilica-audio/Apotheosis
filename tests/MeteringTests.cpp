#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "dsp/TruePeakLimiterEngine.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

// Covers the M1 "inter-sample-peak metering" and "LUFS/true-peak metering"
// features: TruePeakLimiterEngine publishes gain-reduction, output
// true-peak, and momentary/short-term/integrated LUFS readings via relaxed
// atomics (see the getters in TruePeakLimiterEngine.h and
// ApotheosisAudioProcessor). Display of these meters is GUI work (roadmap
// M3); this only exercises the DSP-side computation and readout API.
namespace
{
    void setParam (ApotheosisAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }
}

TEST_CASE ("Meters start at their documented idle defaults before any block is processed", "[metering]")
{
    TruePeakLimiterEngine engine;
    juce::dsp::ProcessSpec spec { 48000.0, 512, 2 };
    engine.prepare (spec);

    CHECK (engine.getGainReductionDb() == 0.0f);
    CHECK (engine.getOutputTruePeakDb() == -100.0f);
    CHECK (engine.getMomentaryLufs() == -100.0f);
    CHECK (engine.getShortTermLufs() == -100.0f);
    CHECK (engine.getIntegratedLufs() == -100.0f);
}

TEST_CASE ("Gain reduction meter stays at ~0 dB for a signal safely under the ceiling", "[metering]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 1024);

    setParam (processor, ParamIDs::ceiling, -1.0f);

    juce::AudioBuffer<float> buffer (2, 1024);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.1f); // -20 dBFS, well under the ceiling

    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);

    CHECK (processor.getGainReductionDb() > -0.1f);
}

TEST_CASE ("Gain reduction meter reports negative dB while heavily limiting", "[metering]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 1024);

    setParam (processor, ParamIDs::inputGain, 18.0f);
    setParam (processor, ParamIDs::ceiling, -1.0f);
    setParam (processor, ParamIDs::release, 50.0f);

    juce::AudioBuffer<float> buffer (2, 1024);
    juce::MidiBuffer midi;

    for (int i = 0; i < 4; ++i)
    {
        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.9f, static_cast<juce::int64> (i) * 1024);
        processor.processBlock (buffer, midi);
    }

    CHECK (processor.getGainReductionDb() < -1.0f);
}

TEST_CASE ("Output true-peak meter tracks near the ceiling under sustained heavy limiting", "[metering]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 2048);

    constexpr float ceilingDb = -1.0f;
    setParam (processor, ParamIDs::inputGain, 12.0f);
    setParam (processor, ParamIDs::ceiling, ceilingDb);
    setParam (processor, ParamIDs::release, 20.0f);

    juce::AudioBuffer<float> buffer (2, 2048);
    juce::MidiBuffer midi;

    for (int i = 0; i < 10; ++i)
    {
        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.9f, static_cast<juce::int64> (i) * 2048);
        processor.processBlock (buffer, midi);
    }

    // Meter should sit close to (at or just under) the ceiling, not near
    // -100 dB (idle) or wildly above it.
    CHECK (processor.getOutputTruePeakDb() > ceilingDb - 3.0f);
    CHECK (processor.getOutputTruePeakDb() <= ceilingDb + 1.0f);
}

TEST_CASE ("LUFS meters report finite, sane values for a full-scale sine and stay near the idle floor for silence", "[metering][lufs]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 2048);

    setParam (processor, ParamIDs::inputGain, 0.0f);
    setParam (processor, ParamIDs::ceiling, 0.0f);

    juce::AudioBuffer<float> buffer (2, 2048);
    juce::MidiBuffer midi;

    // Several blocks of a loud, sustained sine so the momentary (400 ms)
    // window has fully filled at least once.
    for (int i = 0; i < 20; ++i)
    {
        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.99f, static_cast<juce::int64> (i) * 2048);
        processor.processBlock (buffer, midi);
    }

    // A near-full-scale sine's K-weighted loudness sits in a broad but
    // sane range - loose bounds here deliberately avoid overfitting to the
    // exact K-weighting filter numerics (see docs/architecture.md for the
    // documented approximations versus the full ITU-R BS.1770-4 spec).
    CHECK (processor.getMomentaryLufs() > -20.0f);
    CHECK (processor.getMomentaryLufs() < 3.0f);
    CHECK (processor.getShortTermLufs() > -20.0f);
    CHECK (processor.getShortTermLufs() < 3.0f);
    CHECK (processor.getIntegratedLufs() > -20.0f);
    CHECK (processor.getIntegratedLufs() < 3.0f);

    // Now silence: the Momentary meter is a true 400ms sliding window, so it
    // takes a full window's worth of new (silent) samples to fully flush
    // the earlier loud content back out - not just one block. Re-clear the
    // buffer every block (processBlock overwrites it in place) so each
    // iteration genuinely feeds true digital silence, the way a real host
    // would - not the plugin's own (rapidly decaying) previous output
    // recirculated as new input. 16 blocks * 2048 samples/48kHz ~= 683 ms,
    // comfortably more than one full 400 ms window.
    for (int i = 0; i < 16; ++i)
    {
        buffer.clear();
        processor.processBlock (buffer, midi);
    }

    CHECK (processor.getMomentaryLufs() < -40.0f);
}

TEST_CASE ("LUFS meters increase with signal level (comparative, not absolute)", "[metering][lufs]")
{
    const auto momentaryLufsFor = [] (float amplitude) -> float
    {
        ApotheosisAudioProcessor processor;
        processor.prepareToPlay (48000.0, 2048);

        juce::AudioBuffer<float> buffer (2, 2048);
        juce::MidiBuffer midi;

        for (int i = 0; i < 10; ++i)
        {
            TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, amplitude, static_cast<juce::int64> (i) * 2048);
            processor.processBlock (buffer, midi);
        }

        return processor.getMomentaryLufs();
    };

    const auto quiet = momentaryLufsFor (0.05f);
    const auto loud = momentaryLufsFor (0.5f);

    CHECK (loud > quiet);
}

TEST_CASE ("Integrated LUFS resets to the idle floor on reset()", "[metering][lufs]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 2048);

    juce::AudioBuffer<float> buffer (2, 2048);
    juce::MidiBuffer midi;

    for (int i = 0; i < 10; ++i)
    {
        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.8f, static_cast<juce::int64> (i) * 2048);
        processor.processBlock (buffer, midi);
    }

    REQUIRE (processor.getIntegratedLufs() > -60.0f);

    processor.reset();

    CHECK (processor.getIntegratedLufs() == -100.0f);
    CHECK (processor.getMomentaryLufs() == -100.0f);
    CHECK (processor.getShortTermLufs() == -100.0f);
    CHECK (processor.getGainReductionDb() == 0.0f);
    CHECK (processor.getOutputTruePeakDb() == -100.0f);
}

TEST_CASE ("Meters do not update on a zero-sample block (safe no-op)", "[metering][robustness]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.8f);

    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);

    const auto gainReductionBefore = processor.getGainReductionDb();
    const auto truePeakBefore = processor.getOutputTruePeakDb();

    juce::AudioBuffer<float> emptyBuffer (2, 0);
    CHECK_NOTHROW (processor.processBlock (emptyBuffer, midi));

    CHECK (processor.getGainReductionDb() == gainReductionBefore);
    CHECK (processor.getOutputTruePeakDb() == truePeakBefore);
}

//==============================================================================
// T11 (v0.4.0, F5): BS.1770-4 / EBU Tech 3341+3342 conformance of the gated
// Integrated Loudness and LRA (GatedLoudnessMeter). The steady-state and
// gating cases run against the meter directly (exact synthetic K-weighted
// powers - fast and tolerance-free); the end-to-end case runs the whole
// engine so the K-weighting filters and the processChunk() plumbing are
// covered too.
//==============================================================================

#include "dsp/GatedLoudnessMeter.h"

namespace
{
    // Summed-across-channels K-weighted power for a target loudness:
    // L = -0.691 + 10 log10(power)  =>  power = 10^((L + 0.691) / 10).
    double powerForLoudness (double loudnessLufs)
    {
        return std::pow (10.0, (loudnessLufs + 0.691) / 10.0);
    }

    void pushSteadySeconds (GatedLoudnessMeter& meter, double loudnessLufs, double seconds, double sampleRate)
    {
        const auto power = powerForLoudness (loudnessLufs);
        const auto numSamples = static_cast<juce::int64> (seconds * sampleRate);

        for (juce::int64 i = 0; i < numSamples; ++i)
            meter.pushSamplePower (power);
    }
}

TEST_CASE ("T11: steady-state Integrated matches the fed loudness exactly (Tech 3341 cases 1-2 pattern)",
           "[metering][lufs][t11]")
{
    constexpr double sampleRate = 48000.0;

    for (const auto target : { -23.0, -33.0 })
    {
        CAPTURE (target);

        GatedLoudnessMeter meter;
        meter.prepare (sampleRate);

        pushSteadySeconds (meter, target, 20.0, sampleRate);

        CHECK (std::abs (static_cast<double> (meter.getIntegratedLufs()) - target) < 0.1);
    }
}

TEST_CASE ("T11: the relative gate excludes quiet passages (Tech 3341 case 3 pattern)",
           "[metering][lufs][t11]")
{
    constexpr double sampleRate = 48000.0;

    GatedLoudnessMeter meter;
    meter.prepare (sampleRate);

    // Quiet - loud - quiet: the -36 LUFS flanks sit below the relative
    // threshold (approximately mean - 10 LU, with the mean dominated by
    // the -23 middle) and must not drag Integrated down.
    pushSteadySeconds (meter, -36.0, 5.0, sampleRate);
    pushSteadySeconds (meter, -23.0, 20.0, sampleRate);
    pushSteadySeconds (meter, -36.0, 5.0, sampleRate);

    CHECK (std::abs (static_cast<double> (meter.getIntegratedLufs()) - (-23.0)) < 0.1);
}

TEST_CASE ("T11: blocks below the absolute gate never enter the measurement",
           "[metering][lufs][t11]")
{
    constexpr double sampleRate = 48000.0;

    GatedLoudnessMeter meter;
    meter.prepare (sampleRate);

    // 10 s of -80 LUFS (below the -70 absolute gate) alone: no reading.
    pushSteadySeconds (meter, -80.0, 10.0, sampleRate);
    CHECK (meter.getIntegratedLufs() == -100.0f);

    // Followed by -23: the earlier sub-gate material must not bias it.
    pushSteadySeconds (meter, -23.0, 20.0, sampleRate);
    CHECK (std::abs (static_cast<double> (meter.getIntegratedLufs()) - (-23.0)) < 0.1);
}

TEST_CASE ("T11: LRA matches the EBU Tech 3342 two-level vectors within +/-1 LU",
           "[metering][lufs][lra][t11]")
{
    constexpr double sampleRate = 48000.0;

    struct LraVector { double first; double second; double expectedLra; };

    // Tech 3342 cases 1 and 2: two 20 s steady segments.
    for (const auto& vector : { LraVector { -20.0, -30.0, 10.0 },
                                LraVector { -20.0, -15.0, 5.0 } })
    {
        CAPTURE (vector.first, vector.second, vector.expectedLra);

        GatedLoudnessMeter meter;
        meter.prepare (sampleRate);

        pushSteadySeconds (meter, vector.first, 20.0, sampleRate);
        pushSteadySeconds (meter, vector.second, 20.0, sampleRate);

        CHECK (std::abs (static_cast<double> (meter.getLoudnessRangeLu()) - vector.expectedLra) <= 1.0);
    }
}

TEST_CASE ("T11: end-to-end engine render of a -23 LUFS stereo 997 Hz sine reads -23 +/-0.1 LUFS Integrated",
           "[metering][lufs][t11]")
{
    // For a stereo 997 Hz sine of amplitude a in both channels, the summed
    // K-weighted power is (a * k997)^2 and the -0.691 offset compensates
    // the K-filter's gain at 997 Hz by construction, so L = 20 log10(a).
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 4096;
    constexpr double seconds = 20.0;
    const auto amplitude = static_cast<float> (std::pow (10.0, -23.0 / 20.0));

    TruePeakLimiterEngine engine;
    juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (blockSize), 2 };
    engine.prepare (spec);
    engine.setCeilingDb (0.0f); // far above the tone - pure pass-through

    const auto totalSamples = static_cast<int> (seconds * sampleRate);
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    for (int position = 0; position + blockSize <= totalSamples; position += blockSize)
    {
        TestHelpers::fillWithSine (buffer, sampleRate, 997.0, amplitude, position);
        juce::dsp::AudioBlock<float> chunk (buffer);
        engine.process (chunk);
    }

    CAPTURE (engine.getIntegratedLufs());
    CHECK (std::abs (static_cast<double> (engine.getIntegratedLufs()) - (-23.0)) < 0.1);

    // A steady tone has (near-)zero loudness range - proves the LRA
    // plumbing end-to-end without needing a long two-level programme.
    CHECK (engine.getLoudnessRangeLu() < 1.0f);
}
