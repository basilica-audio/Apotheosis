#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <random>

namespace
{
    void setParam (ApotheosisAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }
}

TEST_CASE ("Silence produces silence (and no NaN/Inf)", "[robustness]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();

    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
    CHECK (TestHelpers::peakAbsolute (buffer) == 0.0f);
}

TEST_CASE ("Full-scale input at maximum input gain produces no NaN/Inf", "[robustness]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::inputGain, 24.0f);
    setParam (processor, ParamIDs::ceiling, -1.0f);
    setParam (processor, ParamIDs::release, 5.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 1.0f);

    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
    CHECK (TestHelpers::peakAbsolute (buffer) < 10.0f); // sane bound, not just "finite"
}

TEST_CASE ("Denormal-range input produces no NaN/Inf output", "[robustness]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    constexpr int numSamples = 512;
    juce::AudioBuffer<float> buffer (2, numSamples);

    const auto denormalValue = std::numeric_limits<float>::denorm_min() * 4.0f;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* data = buffer.getWritePointer (channel);

        for (int sample = 0; sample < numSamples; ++sample)
            data[sample] = (sample % 2 == 0) ? denormalValue : -denormalValue;
    }

    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("NaN/Inf sweep: poisoned input samples do not propagate indefinitely", "[robustness][nan]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::inputGain, 6.0f);

    constexpr int numSamples = 512;
    juce::AudioBuffer<float> buffer (2, numSamples);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* data = buffer.getWritePointer (channel);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            switch (sample % 4)
            {
                case 0: data[sample] = std::numeric_limits<float>::quiet_NaN(); break;
                case 1: data[sample] = std::numeric_limits<float>::infinity(); break;
                case 2: data[sample] = -std::numeric_limits<float>::infinity(); break;
                default: data[sample] = 0.3f; break;
            }
        }
    }

    juce::MidiBuffer midi;
    CHECK_NOTHROW (processor.processBlock (buffer, midi));
    CHECK (TestHelpers::allSamplesFinite (buffer));

    // Feed clean audio afterwards: any latent NaN in internal filter/
    // envelope state would otherwise poison every subsequent block forever.
    for (int i = 0; i < 8; ++i)
    {
        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.5f);
        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("Zero-sample buffer does not crash processBlock", "[robustness]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 0);
    juce::MidiBuffer midi;

    CHECK_NOTHROW (processor.processBlock (buffer, midi));
    CHECK (buffer.getNumSamples() == 0);
}

TEST_CASE ("Extreme parameter values at both range edges produce no NaN/Inf", "[robustness]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (44100.0, 256);

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;

    for (bool useMinimum : { true, false })
    {
        setParam (processor, ParamIDs::inputGain, useMinimum ? -12.0f : 24.0f);
        setParam (processor, ParamIDs::ceiling, useMinimum ? -12.0f : 0.0f);
        setParam (processor, ParamIDs::release, useMinimum ? 5.0f : 1000.0f);

        TestHelpers::fillWithSine (buffer, 44100.0, 440.0, 0.8f);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("Rapid parameter automation across many blocks produces no NaN/Inf", "[robustness]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);

    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> unit (0.0f, 1.0f);

    juce::MidiBuffer midi;

    for (int block = 0; block < 100; ++block)
    {
        setParam (processor, ParamIDs::inputGain, -12.0f + unit (rng) * 36.0f);
        setParam (processor, ParamIDs::ceiling, -12.0f + unit (rng) * 12.0f);
        setParam (processor, ParamIDs::release, 5.0f + unit (rng) * 995.0f);

        juce::AudioBuffer<float> buffer (2, 256);
        TestHelpers::fillWithSine (buffer, 48000.0, 200.0 + unit (rng) * 4000.0, 0.9f);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("reset() followed by processBlock does not crash", "[robustness]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::inputGain, 12.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.6f);
    juce::MidiBuffer midi;

    processor.processBlock (buffer, midi);

    CHECK_NOTHROW (processor.reset());

    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.6f);
    CHECK_NOTHROW (processor.processBlock (buffer, midi));
    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Block size larger than prepared maximum is chunked, not passed straight into the oversampler", "[robustness]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);

    constexpr float ceilingDb = -1.0f;
    setParam (processor, ParamIDs::ceiling, ceilingDb);
    setParam (processor, ParamIDs::release, 20.0f);

    // Some hosts occasionally hand over a block larger than promised at
    // prepareToPlay() (e.g. offline bounce/render, buffer-size
    // renegotiation - see issue #14). 700 is deliberately not a multiple of
    // the prepared 256-sample maximum, so process()'s chunking loop is also
    // exercised on a partial final chunk.
    constexpr int numSamples = 700;
    static_assert (numSamples > 256, "must actually exceed the prepared maximum - see issue #15");

    juce::AudioBuffer<float> buffer (2, numSamples);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.9f);
    juce::MidiBuffer midi;

    // Before issue #14 was fixed, TruePeakLimiterEngine::process() passed
    // this oversized block straight into juce::dsp::Oversampling, whose
    // internal buffer juce::dsp::Oversampling::initProcessing() sized for
    // at most 256 samples at prepareToPlay() time; every processSamplesUp/
    // Down override guards its writes past that only with a debug-only
    // jassert (Release: silent heap corruption with no exception to catch;
    // Debug: an assertion failure). CHECK_NOTHROW alone would therefore not
    // reliably have caught the bug even pre-fix (see issue #15), so this
    // also pins down the actual output: fully finite and still respecting
    // the ceiling, exactly as a correctly chunked call must.
    CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));

    const auto ceilingLinear = juce::Decibels::decibelsToGain (ceilingDb);
    constexpr float toleranceDb = 0.5f; // see LimiterTests.cpp for rationale
    const auto toleranceLinear = ceilingLinear * juce::Decibels::decibelsToGain (toleranceDb);

    CHECK (TestHelpers::peakAbsolute (buffer) <= toleranceLinear);
}
