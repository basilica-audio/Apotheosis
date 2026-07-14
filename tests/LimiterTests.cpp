#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

namespace
{
    void setParam (ApotheosisAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }

    // Test-only tolerance above the nominal ceiling, expressed in dB. Real
    // true-peak limiters conventionally allow some small overshoot budget
    // above their nominal ceiling because the measurement/reconstruction
    // filters used to *verify* true peak are never bit-identical to the
    // ones used internally to *limit* it - see docs/architecture.md and the
    // engineering handoff notes for the "internal headroom margin"
    // rationale (TruePeakLimiterEngine::headroomMarginDb).
    constexpr float toleranceDb = 0.5f;
}

TEST_CASE ("Output true peak does not exceed ceiling for a signal with strong inter-sample peaks", "[limiter][truepeak]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int numSamples = 4096;
    constexpr float ceilingDb = -1.0f;

    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (sampleRate, numSamples);

    setParam (processor, ParamIDs::inputGain, 0.0f);
    setParam (processor, ParamIDs::ceiling, ceilingDb);
    setParam (processor, ParamIDs::release, 50.0f);

    // A near-Nyquist sine, amplitude close to full scale but phased so its
    // sample-domain peak sits below its true continuous-time amplitude -
    // exactly the "hidden" inter-sample peak scenario a true-peak limiter
    // (as opposed to a plain sample-peak limiter) has to catch.
    juce::AudioBuffer<float> input (2, numSamples);
    TestHelpers::fillWithSine (input, sampleRate, sampleRate * 0.45, 0.98f);

    const auto inputSamplePeak = TestHelpers::peakAbsolute (input);
    const auto inputTruePeak = TestHelpers::measureTruePeakLinear (input);

    // Sanity check that the constructed test signal is actually exercising
    // an inter-sample peak, so this test isn't vacuously passing on a
    // signal a plain sample-peak limiter would already have handled.
    REQUIRE (inputTruePeak > inputSamplePeak * 1.001f);

    juce::AudioBuffer<float> processed;
    processed.makeCopyOf (input);

    juce::MidiBuffer midi;

    // Run a few blocks so the release-smoothed gain-reduction envelope
    // settles into steady state; only the last block is asserted on.
    for (int i = 0; i < 4; ++i)
    {
        processed.makeCopyOf (input);
        processor.processBlock (processed, midi);
    }

    REQUIRE (TestHelpers::allSamplesFinite (processed));

    const auto outputTruePeak = TestHelpers::measureTruePeakLinear (processed);
    const auto ceilingLinear = juce::Decibels::decibelsToGain (ceilingDb);
    const auto toleranceLinear = ceilingLinear * juce::Decibels::decibelsToGain (toleranceDb);

    CHECK (outputTruePeak <= toleranceLinear);
}

TEST_CASE ("Output true peak does not exceed ceiling for impulsive inter-sample peaks", "[limiter][truepeak]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int numSamples = 2048;
    constexpr float ceilingDb = -2.0f;

    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (sampleRate, numSamples);

    setParam (processor, ParamIDs::ceiling, ceilingDb);
    setParam (processor, ParamIDs::release, 30.0f);

    // Alternating-polarity impulses every 3 samples: classic broadband,
    // ISP-rich test material (a discretised approximation of the impulse
    // trains used by true-peak metering test suites).
    juce::AudioBuffer<float> input (2, numSamples);
    input.clear();

    for (int channel = 0; channel < input.getNumChannels(); ++channel)
    {
        auto* data = input.getWritePointer (channel);

        for (int sample = 0; sample < numSamples; sample += 3)
            data[sample] = (((sample / 3) % 2) == 0) ? 0.95f : -0.95f;
    }

    juce::AudioBuffer<float> processed;
    juce::MidiBuffer midi;

    for (int i = 0; i < 4; ++i)
    {
        processed.makeCopyOf (input);
        processor.processBlock (processed, midi);
    }

    REQUIRE (TestHelpers::allSamplesFinite (processed));

    const auto outputTruePeak = TestHelpers::measureTruePeakLinear (processed);
    const auto ceilingLinear = juce::Decibels::decibelsToGain (ceilingDb);
    const auto toleranceLinear = ceilingLinear * juce::Decibels::decibelsToGain (toleranceDb);

    CHECK (outputTruePeak <= toleranceLinear);
}

TEST_CASE ("Heavy over-threshold input is limited without producing NaN/Inf", "[limiter]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 1024);

    setParam (processor, ParamIDs::inputGain, 0.0f);
    setParam (processor, ParamIDs::ceiling, -1.0f);
    setParam (processor, ParamIDs::release, 50.0f);

    juce::AudioBuffer<float> buffer (2, 1024);
    // 5x full scale (+14 dB over 0 dBFS) - a deliberately extreme overshoot.
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 5.0f);

    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));

    const auto ceilingLinear = juce::Decibels::decibelsToGain (-1.0f);
    // Generous sanity bound (not a tight tolerance): the settled output
    // must be limited to something in the same order of magnitude as the
    // ceiling, not left at (or near) the raw 5x input level.
    CHECK (TestHelpers::peakAbsolute (buffer) < ceilingLinear * 2.0f);
}

TEST_CASE ("Sustained over-threshold input settles close to the ceiling", "[limiter]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 2048);

    constexpr float ceilingDb = -1.0f;
    setParam (processor, ParamIDs::inputGain, 6.0f); // extra 6 dB into the limiter
    setParam (processor, ParamIDs::ceiling, ceilingDb);
    setParam (processor, ParamIDs::release, 20.0f);

    juce::AudioBuffer<float> buffer (2, 2048);
    juce::MidiBuffer midi;

    // Several blocks of a steady, moderately loud sine to let the gain
    // reduction envelope settle into steady state.
    for (int i = 0; i < 10; ++i)
    {
        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.9f, static_cast<juce::int64> (i) * 2048);
        processor.processBlock (buffer, midi);
    }

    REQUIRE (TestHelpers::allSamplesFinite (buffer));

    const auto ceilingLinear = juce::Decibels::decibelsToGain (ceilingDb);
    const auto toleranceLinear = ceilingLinear * juce::Decibels::decibelsToGain (toleranceDb);

    CHECK (TestHelpers::peakAbsolute (buffer) <= toleranceLinear);
}
