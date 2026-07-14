#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

// Broadens coverage per the M1 "Broaden test coverage" issue: sample-rate
// sweeps (44.1-192 kHz), mono/stereo bus configurations, extreme parameter
// automation (including the new Release Curve/Dither/Clip Mix parameters),
// and long-run NaN/Inf stability. The null/reference (LimiterTests.cpp) and
// latency (LatencyTests.cpp) tests are left untouched and stay green.
namespace
{
    void setParam (ApotheosisAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }
}

TEST_CASE ("Sample-rate sweep 44.1-192 kHz: finite output, valid latency, and ceiling respected at every rate",
           "[robustness][samplerate]")
{
    static constexpr double sampleRates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
    constexpr float ceilingDb = -1.0f;
    constexpr float toleranceDb = 0.5f;

    for (const auto sampleRate : sampleRates)
    {
        ApotheosisAudioProcessor processor;
        processor.prepareToPlay (sampleRate, 256);

        CHECK (processor.getLatencySamples() > 0);

        setParam (processor, ParamIDs::inputGain, 6.0f);
        setParam (processor, ParamIDs::ceiling, ceilingDb);
        setParam (processor, ParamIDs::release, 40.0f);

        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;

        for (int block = 0; block < 6; ++block)
        {
            TestHelpers::fillWithSine (buffer, sampleRate, sampleRate * 0.45, 0.95f, static_cast<juce::int64> (block) * 256);
            CHECK_NOTHROW (processor.processBlock (buffer, midi));
        }

        CHECK (TestHelpers::allSamplesFinite (buffer));

        const auto outputTruePeak = TestHelpers::measureTruePeakLinear (buffer);
        const auto ceilingLinear = juce::Decibels::decibelsToGain (ceilingDb);
        const auto toleranceLinear = ceilingLinear * juce::Decibels::decibelsToGain (toleranceDb);

        CHECK (outputTruePeak <= toleranceLinear);
    }
}

TEST_CASE ("Sample-rate change mid-session (prepareToPlay called again) stays finite", "[robustness][samplerate]")
{
    ApotheosisAudioProcessor processor;
    juce::MidiBuffer midi;

    static constexpr double sampleRates[] = { 44100.0, 192000.0, 48000.0, 96000.0 };

    for (const auto sampleRate : sampleRates)
    {
        processor.prepareToPlay (sampleRate, 512);

        juce::AudioBuffer<float> buffer (2, 512);
        TestHelpers::fillWithSine (buffer, sampleRate, 220.0, 0.6f);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("Mono bus layout is supported and processes without NaN/Inf", "[robustness][buslayout]")
{
    ApotheosisAudioProcessor processor;

    juce::AudioProcessor::BusesLayout monoLayout;
    monoLayout.inputBuses.add (juce::AudioChannelSet::mono());
    monoLayout.outputBuses.add (juce::AudioChannelSet::mono());

    REQUIRE (processor.isBusesLayoutSupported (monoLayout));
    REQUIRE (processor.setBusesLayout (monoLayout));

    processor.prepareToPlay (48000.0, 256);

    setParam (processor, ParamIDs::inputGain, 12.0f);
    setParam (processor, ParamIDs::ceiling, -1.0f);

    juce::AudioBuffer<float> buffer (1, 256);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.9f);

    juce::MidiBuffer midi;

    for (int block = 0; block < 4; ++block)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Stereo bus layout is supported (explicit isBusesLayoutSupported check)", "[robustness][buslayout]")
{
    ApotheosisAudioProcessor processor;

    juce::AudioProcessor::BusesLayout stereoLayout;
    stereoLayout.inputBuses.add (juce::AudioChannelSet::stereo());
    stereoLayout.outputBuses.add (juce::AudioChannelSet::stereo());

    CHECK (processor.isBusesLayoutSupported (stereoLayout));
}

TEST_CASE ("Mismatched in/out channel-set bus layouts are rejected", "[robustness][buslayout]")
{
    ApotheosisAudioProcessor processor;

    juce::AudioProcessor::BusesLayout mismatchedLayout;
    mismatchedLayout.inputBuses.add (juce::AudioChannelSet::mono());
    mismatchedLayout.outputBuses.add (juce::AudioChannelSet::stereo());

    CHECK_FALSE (processor.isBusesLayoutSupported (mismatchedLayout));
}

TEST_CASE ("Unsupported multichannel bus layout is rejected", "[robustness][buslayout]")
{
    ApotheosisAudioProcessor processor;

    juce::AudioProcessor::BusesLayout quadLayout;
    quadLayout.inputBuses.add (juce::AudioChannelSet::quadraphonic());
    quadLayout.outputBuses.add (juce::AudioChannelSet::quadraphonic());

    CHECK_FALSE (processor.isBusesLayoutSupported (quadLayout));
}

TEST_CASE ("Long-run processing (many blocks, several seconds of audio) produces no NaN/Inf drift", "[robustness][longrun]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::inputGain, 9.0f);
    setParam (processor, ParamIDs::ceiling, -1.0f);
    setParam (processor, ParamIDs::release, 60.0f);
    setParam (processor, ParamIDs::clipMix, 25.0f);
    setParam (processor, ParamIDs::dither, 2.0f);

    juce::MidiBuffer midi;

    // 500 blocks @ 512 samples/48kHz ~= 5.3 seconds of continuous audio -
    // long enough to reveal slow-building filter-state or smoother drift
    // (including the LUFS K-weighting filters and sliding-window meters
    // added in M1) while staying comfortably under a minute even on
    // Debug/Windows CI.
    constexpr int numBlocks = 500;

    for (int block = 0; block < numBlocks; ++block)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        TestHelpers::fillWithSine (buffer, 48000.0, 110.0, 0.85f, static_cast<juce::int64> (block) * 512);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        REQUIRE (TestHelpers::allSamplesFinite (buffer));
        REQUIRE (TestHelpers::peakAbsolute (buffer) < 100.0f);
    }

    CHECK (std::isfinite (processor.getGainReductionDb()));
    CHECK (std::isfinite (processor.getOutputTruePeakDb()));
    CHECK (std::isfinite (processor.getMomentaryLufs()));
    CHECK (std::isfinite (processor.getShortTermLufs()));
    CHECK (std::isfinite (processor.getIntegratedLufs()));
}

TEST_CASE ("Rapid automation of every parameter (including Release Curve/Dither/Clip Mix) produces no NaN/Inf",
           "[robustness][automation]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);

    juce::MidiBuffer midi;

    for (int block = 0; block < 60; ++block)
    {
        setParam (processor, ParamIDs::inputGain, -12.0f + static_cast<float> (block % 37));
        setParam (processor, ParamIDs::ceiling, -12.0f + static_cast<float> (block % 13));
        setParam (processor, ParamIDs::release, 5.0f + static_cast<float> (block % 40) * 10.0f);
        setParam (processor, ParamIDs::releaseCurve, static_cast<float> (block % 3));
        setParam (processor, ParamIDs::dither, static_cast<float> (block % 3));
        setParam (processor, ParamIDs::clipMix, static_cast<float> (block % 5) * 25.0f);

        juce::AudioBuffer<float> buffer (2, 256);
        TestHelpers::fillWithSine (buffer, 48000.0, 500.0 + static_cast<double> (block) * 10.0, 0.9f);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}
