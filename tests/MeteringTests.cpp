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
    CHECK (engine.getInputLevelDb() == -100.0f);
    CHECK (engine.getOutputLevelDb() == -100.0f);
}

// M3 photoreal GUI (victorian design) small-meter readouts - see
// TruePeakLimiterEngine::getInputLevelDb()/getOutputLevelDb() docs.
TEST_CASE ("Input level meter reads the post-input-gain peak, independent of limiting", "[metering]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 1024);

    setParam (processor, ParamIDs::inputGain, 6.0f); // +6 dB trim
    setParam (processor, ParamIDs::ceiling, -1.0f);

    juce::AudioBuffer<float> buffer (2, 1024);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.1f); // -20 dBFS raw

    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);

    // Post +6 dB input trim, a -20 dBFS sine should read close to -14 dBFS
    // at the input meter, regardless of how much the limiter subsequently
    // reduces it (this is a peak reading a few ms after the input gain
    // smoothing ramp, not an instantaneous transform - generous margin).
    CHECK (processor.getInputLevelDb() > -18.0f);
    CHECK (processor.getInputLevelDb() < -10.0f);
}

TEST_CASE ("Output level meter never exceeds the Ceiling, even when the input level does", "[metering]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 1024);

    setParam (processor, ParamIDs::ceiling, -3.0f);
    setParam (processor, ParamIDs::release, 5.0f);

    juce::AudioBuffer<float> buffer (2, 1024);
    juce::MidiBuffer midi;

    // Several blocks of a hot signal so the limiter is fully engaged and
    // the (smoothed) input-gain ramp has settled.
    for (int i = 0; i < 8; ++i)
    {
        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.9f, static_cast<juce::int64> (i) * 1024); // -0.9 dBFS raw
        processor.processBlock (buffer, midi);
    }

    CHECK (processor.getInputLevelDb() > -3.0f); // input is hot, well above the ceiling
    CHECK (processor.getOutputLevelDb() <= -3.0f + 0.2f); // output stays at/under the ceiling (small margin for dither/interpolation)
}

TEST_CASE ("Input/output level meters reset to the idle floor on reset()", "[metering][robustness]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 1024);

    juce::AudioBuffer<float> buffer (2, 1024);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.5f);

    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);

    REQUIRE (processor.getInputLevelDb() > -100.0f);
    REQUIRE (processor.getOutputLevelDb() > -100.0f);

    processor.reset();

    CHECK (processor.getInputLevelDb() == -100.0f);
    CHECK (processor.getOutputLevelDb() == -100.0f);
}

TEST_CASE ("Input/output level meters do not update on a zero-sample block", "[metering][robustness]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.5f);

    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);

    const auto inputBefore = processor.getInputLevelDb();
    const auto outputBefore = processor.getOutputLevelDb();

    juce::AudioBuffer<float> emptyBuffer (2, 0);
    CHECK_NOTHROW (processor.processBlock (emptyBuffer, midi));

    CHECK (processor.getInputLevelDb() == inputBefore);
    CHECK (processor.getOutputLevelDb() == outputBefore);
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
