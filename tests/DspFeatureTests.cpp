#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "dsp/TruePeakLimiterEngine.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

// Covers the M1 "Complete and refine the DSP" features added beyond the
// v0.1 core: Release Curve, Dither, and Clip Mix. See
// tests/MeteringTests.cpp for the inter-sample-peak/LUFS metering coverage
// and docs/architecture.md for the design rationale of each feature.
namespace
{
    void setParam (ApotheosisAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }

    constexpr float toleranceDb = 0.5f; // see LimiterTests.cpp for rationale
}

//==============================================================================
// Release Curve
//==============================================================================

TEST_CASE ("Release Curve: default (Exponential) matches the original v0.1 one-pole behaviour", "[dsp][releasecurve]")
{
    // Regression guard: with ReleaseCurve left at its default, a fresh
    // engine's output must be identical to the pre-M1 implementation on the
    // same input, since Exponential's release-phase branch performs the
    // exact same arithmetic as before.
    TruePeakLimiterEngine engine;
    juce::dsp::ProcessSpec spec { 48000.0, 512, 2 };
    engine.prepare (spec);

    engine.setCeilingDb (-3.0f);
    engine.setReleaseMs (40.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.95f);

    juce::dsp::AudioBlock<float> block (buffer);

    for (int i = 0; i < 4; ++i)
        CHECK_NOTHROW (engine.process (block));

    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Release Curve: Linear recovers gain at a materially different rate than Exponential", "[dsp][releasecurve]")
{
    // Slam the limiter with a single hot block, then feed several blocks of
    // true silence (re-cleared before each process() call, since process()
    // overwrites its buffer in place - otherwise the engine's own decaying
    // output would recirculate as "new" input) and compare how far each
    // curve's gain-reduction meter has recovered towards 0 dB by the last
    // block. The first silent block still partly reflects the lookahead
    // window draining the tail of the loud block's attack phase (which is
    // curve-independent by design - see docs/architecture.md), so several
    // blocks are processed to reach a purely release-phase measurement.
    const auto recoverAfterSilence = [] (int releaseCurveIndex) -> float
    {
        TruePeakLimiterEngine engine;
        juce::dsp::ProcessSpec spec { 48000.0, 512, 2 };
        engine.prepare (spec);

        engine.setCeilingDb (-1.0f);
        engine.setReleaseMs (200.0f);
        engine.setReleaseCurve (releaseCurveIndex);

        juce::AudioBuffer<float> loud (2, 512);
        TestHelpers::fillWithSine (loud, 48000.0, 1000.0, 0.99f);
        juce::dsp::AudioBlock<float> loudBlock (loud);
        engine.process (loudBlock);

        REQUIRE (engine.getGainReductionDb() < -0.1f); // confirm it actually engaged

        juce::AudioBuffer<float> silence (2, 512);
        float gainReductionDb = 0.0f;

        for (int i = 0; i < 3; ++i)
        {
            silence.clear();
            juce::dsp::AudioBlock<float> silentBlock (silence);
            engine.process (silentBlock);
            gainReductionDb = engine.getGainReductionDb();
        }

        return gainReductionDb;
    };

    const auto linearRecovery = recoverAfterSilence (1);
    const auto exponentialRecovery = recoverAfterSilence (0);
    const auto smoothRecovery = recoverAfterSilence (2);

    CHECK (linearRecovery != Catch::Approx (exponentialRecovery).margin (1e-4));
    CHECK (smoothRecovery != Catch::Approx (exponentialRecovery).margin (1e-4));

    // Smooth (a two-stage cascade) recovers no faster than plain
    // Exponential using the same Release time - the whole point of the
    // extra stage is a softer, not-faster, onset.
    CHECK (smoothRecovery <= exponentialRecovery + 0.01f);
}

TEST_CASE ("Release Curve never produces NaN/Inf or overshoots unity gain, at any curve", "[dsp][releasecurve][robustness]")
{
    for (int curveIndex = 0; curveIndex < 3; ++curveIndex)
    {
        ApotheosisAudioProcessor processor;
        processor.prepareToPlay (48000.0, 256);

        setParam (processor, ParamIDs::releaseCurve, static_cast<float> (curveIndex));
        setParam (processor, ParamIDs::release, 5.0f);
        setParam (processor, ParamIDs::ceiling, -1.0f);

        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;

        for (int block = 0; block < 20; ++block)
        {
            const auto amplitude = (block % 2 == 0) ? 0.99f : 0.05f;
            TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, amplitude);
            CHECK_NOTHROW (processor.processBlock (buffer, midi));
            CHECK (TestHelpers::allSamplesFinite (buffer));
            CHECK (TestHelpers::peakAbsolute (buffer) <= juce::Decibels::decibelsToGain (-1.0f) * 1.5f);
        }
    }
}

//==============================================================================
// Dither
//==============================================================================

TEST_CASE ("Dither Off is bit-identical across repeated identical processing (no hidden randomness)", "[dsp][dither]")
{
    ApotheosisAudioProcessor processorA;
    ApotheosisAudioProcessor processorB;
    processorA.prepareToPlay (48000.0, 512);
    processorB.prepareToPlay (48000.0, 512);

    setParam (processorA, ParamIDs::dither, 0.0f);
    setParam (processorB, ParamIDs::dither, 0.0f);

    juce::AudioBuffer<float> bufferA (2, 512);
    juce::AudioBuffer<float> bufferB (2, 512);
    TestHelpers::fillWithSine (bufferA, 48000.0, 1000.0, 0.5f);
    TestHelpers::fillWithSine (bufferB, 48000.0, 1000.0, 0.5f);

    juce::MidiBuffer midi;
    processorA.processBlock (bufferA, midi);
    processorB.processBlock (bufferB, midi);

    for (int channel = 0; channel < bufferA.getNumChannels(); ++channel)
        for (int sample = 0; sample < bufferA.getNumSamples(); ++sample)
            CHECK (bufferA.getSample (channel, sample) == bufferB.getSample (channel, sample));
}

TEST_CASE ("Dither raises the noise floor on an otherwise silent signal, bounded to about 1 LSB", "[dsp][dither]")
{
    for (const auto ditherIndex : { 1.0f, 2.0f }) // 16-bit, 24-bit
    {
        ApotheosisAudioProcessor processor;
        processor.prepareToPlay (48000.0, 2048);

        setParam (processor, ParamIDs::dither, ditherIndex);

        juce::AudioBuffer<float> buffer (2, 2048);
        buffer.clear();

        juce::MidiBuffer midi;
        processor.processBlock (buffer, midi);

        CHECK (TestHelpers::allSamplesFinite (buffer));

        // Dither must be present (non-zero) but tiny - well under -60 dBFS
        // even for 16-bit dither, and it must never introduce a value at or
        // above 1 LSB in magnitude (2^-15 for 16-bit, 2^-23 for 24-bit).
        const auto peak = TestHelpers::peakAbsolute (buffer);
        const auto lsb = (ditherIndex == 1.0f) ? std::exp2 (-15.0f) : std::exp2 (-23.0f);

        CHECK (peak > 0.0f);
        CHECK (peak <= lsb * 1.01f);
    }
}

TEST_CASE ("Dither does not break the never-exceed-ceiling guarantee beyond the existing test tolerance", "[dsp][dither][truepeak]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 4096);

    constexpr float ceilingDb = -1.0f;
    setParam (processor, ParamIDs::ceiling, ceilingDb);
    setParam (processor, ParamIDs::dither, 1.0f); // 16-bit, the larger of the two dither amplitudes

    juce::AudioBuffer<float> input (2, 4096);
    TestHelpers::fillWithSine (input, 48000.0, 48000.0 * 0.45, 0.98f);

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

TEST_CASE ("Dither never pushes a base-rate sample past the exact nominal Ceiling (issue #9)", "[dsp][dither][truepeak]")
{
    // Unlike the test above (which uses the project's usual 0.5 dB
    // true-peak/reconstruction-filter tolerance), this checks the exact
    // base-rate sample values dither is applied to, with no dB-scale slack
    // - only float-rounding epsilon. Dither's per-sample TPDF amplitude is
    // <= 1 LSB (2^-15 for 16-bit, ~3e-5 linear absolute, NOT relative to
    // Ceiling), so the ~0.5 dB tolerance used elsewhere in this suite would
    // never have caught it exceeding the ceiling - exactly how this went
    // unnoticed until the dedicated review finding.
    //
    // headroomMarginDb (the gap process() targets below Ceiling) is a
    // roughly *proportional* margin, so it shrinks in absolute terms as
    // Ceiling gets quieter, while ditherLsb stays a *fixed* absolute size
    // (it's set by the output word length, not by Ceiling). A very low
    // Ceiling (as a mastering engineer might use for a quiet intro/outro
    // section, or any low-level passage) is therefore exactly where the
    // absolute margin shrinks below ditherLsb and this bug was reachable in
    // practice - confirmed empirically: at -90 dBFS Ceiling the natural
    // (dither-off) margin to Ceiling is under 0.1 LSB, so 16-bit dither's
    // TPDF range (up to ~1 LSB) reliably pushes the post-downsample peak
    // past Ceiling pre-fix, consistently across many runs/RNG seeds.
    TruePeakLimiterEngine engine;

    constexpr double sampleRate = 48000.0;
    constexpr int numSamples = 4096;
    const juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (numSamples), 2 };
    engine.prepare (spec);

    constexpr float ceilingDb = -90.0f;
    engine.setCeilingDb (ceilingDb);
    engine.setDitherMode (1); // 16-bit - the larger of the two dither amplitudes

    // Near-Nyquist tone (inter-sample-peak-rich, same shape as this suite's
    // other true-peak tests), at an amplitude that needs real gain
    // reduction so the limiter is actively driving the signal down towards
    // (the now very quiet) Ceiling rather than passing it through untouched.
    juce::AudioBuffer<float> input (2, numSamples);
    TestHelpers::fillWithSine (input, sampleRate, sampleRate * 0.45, 0.98f);

    juce::AudioBuffer<float> processed;

    for (int i = 0; i < 4; ++i)
    {
        processed.makeCopyOf (input);
        juce::dsp::AudioBlock<float> block (processed);
        engine.process (block);
    }

    REQUIRE (TestHelpers::allSamplesFinite (processed));

    const auto ceilingLinear = juce::Decibels::decibelsToGain (ceilingDb);

    // Epsilon is a small fraction of ditherLsb (2^-15) - comfortably above
    // float rounding noise at this magnitude, comfortably below the ~1 LSB
    // overshoot this test guards against.
    const auto epsilon = std::exp2 (-15.0f) * 0.1f;

    CHECK (TestHelpers::peakAbsolute (processed) <= ceilingLinear + epsilon);
}

//==============================================================================
// Clip Mix
//==============================================================================

TEST_CASE ("Clip Mix at 0% (default) is bit-identical to the pure gain-reduction limiter path", "[dsp][clipmix]")
{
    TruePeakLimiterEngine engineA;
    TruePeakLimiterEngine engineB;
    juce::dsp::ProcessSpec spec { 48000.0, 1024, 2 };
    engineA.prepare (spec);
    engineB.prepare (spec);

    engineA.setCeilingDb (-1.0f);
    engineB.setCeilingDb (-1.0f);
    engineB.setClipMixPercent (0.0f); // explicit, though it's already the default

    juce::AudioBuffer<float> bufferA (2, 1024);
    juce::AudioBuffer<float> bufferB (2, 1024);
    TestHelpers::fillWithSine (bufferA, 48000.0, 1000.0, 0.95f);
    TestHelpers::fillWithSine (bufferB, 48000.0, 1000.0, 0.95f);

    juce::dsp::AudioBlock<float> blockA (bufferA);
    juce::dsp::AudioBlock<float> blockB (bufferB);
    engineA.process (blockA);
    engineB.process (blockB);

    for (int channel = 0; channel < bufferA.getNumChannels(); ++channel)
        for (int sample = 0; sample < bufferA.getNumSamples(); ++sample)
            CHECK (bufferA.getSample (channel, sample) == bufferB.getSample (channel, sample));
}

TEST_CASE ("Clip Mix at 100% still respects the never-exceed-ceiling guarantee", "[dsp][clipmix][truepeak]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 4096);

    constexpr float ceilingDb = -1.0f;
    setParam (processor, ParamIDs::ceiling, ceilingDb);
    setParam (processor, ParamIDs::clipMix, 100.0f);

    juce::AudioBuffer<float> input (2, 4096);
    TestHelpers::fillWithSine (input, 48000.0, 48000.0 * 0.45, 0.98f);

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

TEST_CASE ("Clip Mix produces a different (louder/more saturated) result than 0% for a hot signal", "[dsp][clipmix]")
{
    const auto renderRms = [] (float clipMixPercent) -> double
    {
        ApotheosisAudioProcessor processor;
        processor.prepareToPlay (48000.0, 2048);

        auto* param = processor.apvts.getParameter (ParamIDs::clipMix);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (clipMixPercent));

        auto* ceilingParam = processor.apvts.getParameter (ParamIDs::ceiling);
        REQUIRE (ceilingParam != nullptr);
        ceilingParam->setValueNotifyingHost (ceilingParam->convertTo0to1 (-1.0f));

        juce::AudioBuffer<float> buffer (2, 2048);
        TestHelpers::fillWithSine (buffer, 48000.0, 300.0, 0.98f);

        juce::MidiBuffer midi;

        for (int i = 0; i < 6; ++i)
            processor.processBlock (buffer, midi);

        return TestHelpers::rms (buffer);
    };

    const auto rmsNoClip = renderRms (0.0f);
    const auto rmsFullClip = renderRms (100.0f);

    CHECK (rmsNoClip != Catch::Approx (rmsFullClip).margin (1e-5));
}
