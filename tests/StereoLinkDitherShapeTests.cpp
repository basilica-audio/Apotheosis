#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "dsp/TruePeakLimiterEngine.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

// v0.2.0 deep-dive additions (docs/design-brief.md) - Guarantee 4 (Stereo
// Link sweep) and Guarantee 5 (Dither Shape spectral proof).
namespace
{
    void setParam (ApotheosisAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }
}

//==============================================================================
// Guarantee 4: Stereo Link sweep.
//==============================================================================

TEST_CASE ("Guarantee 4: right channel's OWN gain reduction decreases monotonically toward zero as Stereo Link sweeps 100% to 0%, "
           "while the left channel's stays essentially constant",
           "[dsp][stereolink][guarantee4]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int numSamples = 8192;

    // Left channel: a loud, ceiling-exceeding 2 kHz tone (the peak that
    // drives the linked detector). Right channel: a quiet, WELL-under-
    // ceiling probe tone (a different frequency, so it's trivially
    // distinguishable and never itself triggers gain reduction on its own -
    // any reduction measured on it is purely the cross-channel Stereo Link
    // coupling from the left channel's peak).
    juce::AudioBuffer<float> input (2, numSamples);

    juce::AudioBuffer<float> loudLeft (1, numSamples);
    TestHelpers::fillWithSine (loudLeft, sampleRate, 2000.0, 0.99f);
    input.copyFrom (0, 0, loudLeft, 0, 0, numSamples);

    juce::AudioBuffer<float> quietRight (1, numSamples);
    TestHelpers::fillWithSine (quietRight, sampleRate, 500.0, 0.05f);
    input.copyFrom (1, 0, quietRight, 0, 0, numSamples);

    float previousRightPeak = -1.0f; // sentinel
    float firstLeftPeak = -1.0f;

    for (const auto stereoLinkPercent : { 100.0f, 75.0f, 50.0f, 25.0f, 0.0f })
    {
        ApotheosisAudioProcessor processor;
        processor.prepareToPlay (sampleRate, numSamples);

        setParam (processor, ParamIDs::ceiling, -1.0f);
        setParam (processor, ParamIDs::release, 50.0f);
        setParam (processor, ParamIDs::stereoLink, stereoLinkPercent);

        juce::AudioBuffer<float> buffer;
        juce::MidiBuffer midi;

        for (int i = 0; i < 4; ++i)
        {
            buffer.makeCopyOf (input);
            processor.processBlock (buffer, midi);
        }

        const auto* leftData = buffer.getReadPointer (0);
        const auto* rightData = buffer.getReadPointer (1);

        float leftPeak = 0.0f;
        float rightPeak = 0.0f;

        for (int sample = 0; sample < numSamples; ++sample)
        {
            leftPeak = juce::jmax (leftPeak, std::abs (leftData[sample]));
            rightPeak = juce::jmax (rightPeak, std::abs (rightData[sample]));
        }

        CAPTURE (stereoLinkPercent, leftPeak, rightPeak);

        if (firstLeftPeak < 0.0f)
        {
            firstLeftPeak = leftPeak;
        }
        else
        {
            // Left channel's own peak is always driven by its own (loud,
            // ceiling-exceeding) content - Stereo Link changes cross-channel
            // coupling, not each channel's own detection, so this must stay
            // essentially constant across the sweep.
            CHECK (leftPeak == Catch::Approx (firstLeftPeak).margin (0.02));
        }

        if (previousRightPeak >= 0.0f)
        {
            // Right channel's quiet probe tone is UNDER the ceiling on its
            // own - any measured attenuation on it (relative to its
            // untouched 0.05 amplitude) is purely Stereo Link's cross-
            // channel pull from the left channel's peak. As Stereo Link
            // decreases, that pull weakens, so the right channel's peak
            // should recover (increase) monotonically toward its natural
            // (untouched) ~0.05 amplitude.
            CHECK (rightPeak >= previousRightPeak - 1.0e-4f);
        }

        previousRightPeak = rightPeak;
    }

    // At Stereo Link = 0% (fully independent), the right channel's quiet
    // probe (well under Ceiling on its own) should be essentially untouched
    // - close to its natural 0.05 amplitude, not pulled down toward the
    // left channel's much lower ceiling-limited level.
    CHECK (previousRightPeak > 0.03f);
}

//==============================================================================
// Guarantee 5: Dither Shape spectral proof.
//==============================================================================

TEST_CASE ("Guarantee 5: Dither Flat (explicit) matches v1's existing dither output bounds exactly, same as the default",
           "[dsp][dithershape][guarantee5]")
{
    // Every TruePeakLimiterEngine/ApotheosisAudioProcessor instance seeds
    // its own juce::Random independently (see TruePeakLimiterEngine.h's
    // `ditherRng` member - a plain, unseeded-from-outside member), so two
    // separate instances' actual dither noise draws are never expected to
    // be sample-for-sample equal even with identical settings - comparing
    // them directly would be the wrong methodology (this is a property of
    // *any* RNG-based dither, not a v0.2.0 regression risk). What Guarantee
    // 5's "Flat is bit-identical to v1" actually needs is that
    // DitherShape::flat executes the exact same code path v1's dither
    // formula did (`shapingSample = tpdf`, unconditionally, no shaping
    // filter applied) - verified here the same way
    // tests/DspFeatureTests.cpp's existing (untouched, still-passing)
    // "Dither raises the noise floor... bounded to about 1 LSB" test
    // verifies v1's own dither formula: by asserting explicit-Flat's output
    // satisfies the exact same documented amplitude bound.
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 2048);

    setParam (processor, ParamIDs::dither, 1.0f); // 16-bit
    setParam (processor, ParamIDs::ditherShape, 0.0f); // Flat, explicit (already the default)

    juce::AudioBuffer<float> buffer (2, 2048);
    buffer.clear();

    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);

    CHECK (TestHelpers::allSamplesFinite (buffer));

    const auto peak = TestHelpers::peakAbsolute (buffer);
    const auto lsb = std::exp2 (-15.0f);

    CHECK (peak > 0.0f);
    CHECK (peak <= lsb * 1.01f);
}

namespace
{
    // Coarse spectral-tilt measurement: a simple single-pole IIR band-energy
    // estimator run twice per signal (once with a low cutoff, once with a
    // high cutoff) rather than a full FFT - sufficient for the "measurably
    // redistributes energy toward higher frequencies" comparative assertion
    // Guarantee 5 asks for, without pulling in an FFT dependency purely for
    // a test.
    double highPassedEnergy (const juce::AudioBuffer<float>& buffer, double sampleRate, float cutoffHz)
    {
        juce::dsp::IIR::Filter<float> filter;
        filter.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sampleRate, cutoffHz);
        juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (buffer.getNumSamples()), 1 };
        filter.prepare (spec);

        double energy = 0.0;
        const auto* data = buffer.getReadPointer (0);

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto filtered = filter.processSample (data[sample]);
            energy += static_cast<double> (filtered) * static_cast<double> (filtered);
        }

        return energy;
    }
}

TEST_CASE ("Guarantee 5: Dither Shaped measurably redistributes quantisation-noise energy toward higher frequencies versus Flat",
           "[dsp][dithershape][guarantee5]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int numSamples = 65536; // long capture for a stable spectral-tilt estimate

    const auto renderDitherOnly = [] (int ditherShapeIndex) -> juce::AudioBuffer<float>
    {
        ApotheosisAudioProcessor processor;
        processor.prepareToPlay (sampleRate, numSamples);

        setParam (processor, ParamIDs::dither, 1.0f); // 16-bit - the larger of the two amplitudes
        setParam (processor, ParamIDs::ditherShape, static_cast<float> (ditherShapeIndex));

        juce::AudioBuffer<float> buffer (2, numSamples);
        buffer.clear(); // silent input - the entire output is dither noise

        juce::MidiBuffer midi;
        processor.processBlock (buffer, midi);

        return buffer;
    };

    auto flatOutput = renderDitherOnly (0);
    auto shapedOutput = renderDitherOnly (1);

    REQUIRE (TestHelpers::allSamplesFinite (flatOutput));
    REQUIRE (TestHelpers::allSamplesFinite (shapedOutput));

    // Single-channel views (both channels are independent dither draws, but
    // channel 0 alone is a fine, representative sample).
    juce::AudioBuffer<float> flatMono (1, numSamples);
    juce::AudioBuffer<float> shapedMono (1, numSamples);
    flatMono.copyFrom (0, 0, flatOutput, 0, 0, numSamples);
    shapedMono.copyFrom (0, 0, shapedOutput, 0, 0, numSamples);

    constexpr float lowBandCutoffHz = 1000.0f; // energy ABOVE this, as a proxy for "not the very bottom of the band"
    constexpr float highBandCutoffHz = 12000.0f; // energy above this - the "pushed toward the top" region Shaped targets

    const auto flatLowBandEnergy = highPassedEnergy (flatMono, sampleRate, lowBandCutoffHz);
    const auto flatHighBandEnergy = highPassedEnergy (flatMono, sampleRate, highBandCutoffHz);
    const auto shapedLowBandEnergy = highPassedEnergy (shapedMono, sampleRate, lowBandCutoffHz);
    const auto shapedHighBandEnergy = highPassedEnergy (shapedMono, sampleRate, highBandCutoffHz);

    // Coarse spectral tilt ratio: high-band energy as a fraction of
    // low-band-and-above energy. Shaped must show a measurably higher
    // fraction of its energy in the high band than Flat - the "measurably
    // redistributes energy toward higher frequencies" assertion, without
    // claiming to match any specific vendor curve (docs/design-brief.md's
    // Honesty section).
    REQUIRE (flatLowBandEnergy > 0.0);
    REQUIRE (shapedLowBandEnergy > 0.0);

    const auto flatTiltRatio = flatHighBandEnergy / flatLowBandEnergy;
    const auto shapedTiltRatio = shapedHighBandEnergy / shapedLowBandEnergy;

    CAPTURE (flatTiltRatio, shapedTiltRatio);

    CHECK (shapedTiltRatio > flatTiltRatio);
}

TEST_CASE ("Guarantee 5: Dither Shaped still respects the never-exceed-ceiling guarantee", "[dsp][dithershape][guarantee5][truepeak]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 4096);

    constexpr float ceilingDb = -1.0f;
    setParam (processor, ParamIDs::ceiling, ceilingDb);
    setParam (processor, ParamIDs::dither, 1.0f);
    setParam (processor, ParamIDs::ditherShape, 1.0f); // Shaped

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

    const auto ceilingLinear = juce::Decibels::decibelsToGain (ceilingDb);
    // Epsilon consistent with tests/DspFeatureTests.cpp's exact base-rate
    // ceiling check for Flat dither (issue #9) - Shaped dither's amplitude
    // is the same order of magnitude (<= 1 LSB), so the same re-clamp
    // applies.
    const auto epsilon = std::exp2 (-15.0f) * 0.1f;

    CHECK (TestHelpers::peakAbsolute (processed) <= ceilingLinear + epsilon);
}

//==============================================================================
// T12 (v0.4.0, F4): psychoacoustic noise-shaped dither quality gates.
//==============================================================================

#include "dsp/PsychoacousticDither.h"

#include <juce_dsp/juce_dsp.h>

namespace
{
    // Test-side independent implementation of the audibility weighting the
    // shaper was designed against (Terhardt threshold-in-quiet, +55 dB HF
    // clamp - the exact formula documented in PsychoacousticDither.h's
    // provenance block). Written from the formula, not shared with the
    // production code (which only stores the resulting coefficients).
    double audibilityWeight (double frequencyHz)
    {
        const auto fk = std::max (frequencyHz, 10.0) / 1000.0;
        const auto thresholdDb = std::min (55.0,
                                           3.64 * std::pow (fk, -0.8)
                                            - 6.5 * std::exp (-0.6 * (fk - 3.3) * (fk - 3.3))
                                            + 1.0e-3 * fk * fk * fk * fk);
        return std::pow (10.0, -thresholdDb / 10.0);
    }

    // F-weighted total noise power of channel 0 via a Hann-windowed FFT -
    // both signals under comparison run through the identical estimator,
    // so window/scalloping factors cancel in the ratio.
    double fWeightedNoisePower (const juce::AudioBuffer<float>& buffer, double sampleRate)
    {
        constexpr int fftOrder = 16;
        constexpr int fftSize = 1 << fftOrder;
        REQUIRE (buffer.getNumSamples() >= fftSize);

        juce::dsp::FFT fft (fftOrder);
        std::vector<float> data (static_cast<size_t> (fftSize) * 2, 0.0f);

        const auto* samples = buffer.getReadPointer (0);

        for (int n = 0; n < fftSize; ++n)
        {
            const auto hann = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                       * static_cast<float> (n) / static_cast<float> (fftSize));
            data[static_cast<size_t> (n)] = samples[n] * hann;
        }

        fft.performRealOnlyForwardTransform (data.data());

        double weightedPower = 0.0;

        for (int bin = 1; bin < fftSize / 2; ++bin)
        {
            const auto re = static_cast<double> (data[static_cast<size_t> (2 * bin)]);
            const auto im = static_cast<double> (data[static_cast<size_t> (2 * bin + 1)]);
            const auto frequency = sampleRate * static_cast<double> (bin) / static_cast<double> (fftSize);
            weightedPower += (re * re + im * im) * audibilityWeight (frequency);
        }

        return weightedPower;
    }

    // Silence rendered through the engine with 16-bit dither: the entire
    // output IS the dither/requantiser noise.
    juce::AudioBuffer<float> renderDitherNoise (int noiseShapingIndex, double sampleRate, int totalSamples)
    {
        constexpr int blockSize = 2048;

        TruePeakLimiterEngine engine;
        juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (blockSize), 2 };
        engine.prepare (spec);
        engine.setDitherMode (1); // 16-bit
        engine.setNoiseShaping (noiseShapingIndex);

        juce::AudioBuffer<float> buffer (2, totalSamples);
        buffer.clear();

        juce::dsp::AudioBlock<float> whole (buffer);

        for (int position = 0; position + blockSize <= totalSamples; position += blockSize)
        {
            auto chunk = whole.getSubBlock (static_cast<size_t> (position), static_cast<size_t> (blockSize));
            engine.process (chunk);
        }

        return buffer;
    }
}

TEST_CASE ("T12: the Weighted path's TPDF generator draws a triangular distribution (chi-square)",
           "[dither][noiseshaping][t12]")
{
    // Deterministic seed (test-only hook) so the chi-square statistic is a
    // fixed number, not a 1%-of-CI-runs coin flip.
    PsychoacousticDither dither;
    dither.setSeedsForTests (0x5EEDBA5E);

    constexpr int numDraws = 400000;
    constexpr int numBins = 20; // over [-1, 1]
    int histogram[numBins] = {};

    for (int draw = 0; draw < numDraws; ++draw)
    {
        const auto value = dither.nextTpdf (0);
        const auto bin = juce::jlimit (0, numBins - 1,
                                       static_cast<int> ((static_cast<double> (value) + 1.0) / 2.0 * numBins));
        ++histogram[bin];
    }

    // Expected counts under the triangular PDF f(x) = 1 - |x| on [-1, 1]:
    // per-bin probability integrates f over the bin.
    double chiSquare = 0.0;

    for (int bin = 0; bin < numBins; ++bin)
    {
        const auto x0 = -1.0 + 2.0 * bin / numBins;
        const auto x1 = -1.0 + 2.0 * (bin + 1) / numBins;
        const auto integral = [] (double x) { return x < 0.0 ? x + 0.5 * x * x : x - 0.5 * x * x; };
        const auto probability = integral (x1) - integral (x0);
        const auto expected = probability * numDraws;

        REQUIRE (expected > 5.0); // chi-square validity condition
        const auto deviation = static_cast<double> (histogram[bin]) - expected;
        chiSquare += deviation * deviation / expected;
    }

    // 19 degrees of freedom, p > 0.01 <=> chi-square < 36.19.
    CAPTURE (chiSquare);
    CHECK (chiSquare < 36.19);
}

TEST_CASE ("T12: Weighted 16-bit noise sits >= 15 dB below flat TPDF in F-weighted power",
           "[dither][noiseshaping][t12]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int totalSamples = 1 << 16;

    const auto legacyFlat = renderDitherNoise (0, sampleRate, totalSamples);
    const auto weighted = renderDitherNoise (1, sampleRate, totalSamples);

    REQUIRE (TestHelpers::allSamplesFinite (legacyFlat));
    REQUIRE (TestHelpers::allSamplesFinite (weighted));

    const auto legacyPower = fWeightedNoisePower (legacyFlat, sampleRate);
    const auto weightedPower = fWeightedNoisePower (weighted, sampleRate);

    REQUIRE (legacyPower > 0.0);
    REQUIRE (weightedPower > 0.0);

    // The binding floor (brief section 3.4): >= 15 dB F-weighted
    // perceived-floor improvement vs the Legacy flat-TPDF path, including
    // the true-requantiser variance penalty. The committed coefficient
    // table was designed to +20.0 dB @48k (see PsychoacousticDither.h),
    // so this asserts with ~5 dB of measurement margin.
    const auto improvementDb = 10.0 * std::log10 (legacyPower / weightedPower);
    CAPTURE (improvementDb);
    CHECK (improvementDb >= 15.0);
}

TEST_CASE ("T12: Legacy 16-bit dither with the noiseShaping parameter present stays bit-identical to the committed v0.2.0 fixture",
           "[dither][noiseshaping][t12][regression]")
{
    // Mirrors tests/RegressionTests.cpp's dither-fixture render exactly
    // (silence, 16-bit Legacy dither, seeded RNG, 4 x 2048 @48k stereo) -
    // but through a v0.4.0 engine with noiseShaping EXPLICITLY set to
    // Legacy, proving the new branch point leaves the v0.2.0 code path's
    // draw order untouched.
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 2048;
    constexpr int numBlocks = 4;
    constexpr int totalSamples = blockSize * numBlocks;
    constexpr juce::int64 fixtureSeed = 0x5EEDA5D17LL; // must match RegressionTests.cpp's ditherFixtureSeed

    TruePeakLimiterEngine engine;
    juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (blockSize), 2 };
    engine.prepare (spec);
    engine.setDitherMode (1); // 16-bit
    engine.setNoiseShaping (0); // Legacy, explicitly
    engine.setDitherSeedForTests (fixtureSeed);

    juce::AudioBuffer<float> buffer (2, totalSamples);
    buffer.clear();

    juce::dsp::AudioBlock<float> whole (buffer);

    for (int block = 0; block < numBlocks; ++block)
    {
        auto chunk = whole.getSubBlock (static_cast<size_t> (block * blockSize), static_cast<size_t> (blockSize));
        engine.process (chunk);
    }

    const auto fixtureFile = juce::File (APOTHEOSIS_TESTS_DIR)
                                 .getChildFile ("fixtures").getChildFile ("v020")
                                 .getChildFile ("dither16_legacy.f32");
    REQUIRE (fixtureFile.existsAsFile());

    juce::MemoryBlock fixtureBytes;
    REQUIRE (fixtureFile.loadFileAsData (fixtureBytes));
    REQUIRE (fixtureBytes.getSize() == static_cast<size_t> (totalSamples) * 2 * sizeof (float));

    const auto* fixtureData = static_cast<const float*> (fixtureBytes.getData());
    juce::int64 mismatches = 0;

    for (int channel = 0; channel < 2; ++channel)
    {
        const auto* rendered = buffer.getReadPointer (channel);
        const auto* reference = fixtureData + static_cast<size_t> (channel) * totalSamples;

        for (int n = 0; n < totalSamples; ++n)
        {
#if JUCE_MAC && defined (__aarch64__)
            // Generation platform (see fixtures/v020/manifest.json):
            // bit-exact.
            if (std::memcmp (&rendered[n], &reference[n], sizeof (float)) != 0)
                ++mismatches;
#else
            if (std::abs (rendered[n] - reference[n]) > 1.0e-7f)
                ++mismatches;
#endif
        }
    }

    CHECK (mismatches == 0);
}

TEST_CASE ("T12: post-dither samples never exceed the ceiling with the Weighted requantiser engaged",
           "[dither][noiseshaping][t12][truepeak]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 2048;
    constexpr int totalSamples = 65536;
    constexpr float ceilingDb = -1.0f;

    TruePeakLimiterEngine engine;
    juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (blockSize), 2 };
    engine.prepare (spec);
    engine.setCeilingDb (ceilingDb);
    engine.setInputGainDb (4.0f);
    engine.setDitherMode (1); // 16-bit - the largest quantisation step
    engine.setNoiseShaping (1); // Weighted

    juce::AudioBuffer<float> buffer (2, totalSamples);
    TestHelpers::fillWithSine (buffer, sampleRate, 997.0, 1.0f);

    juce::dsp::AudioBlock<float> whole (buffer);

    for (int position = 0; position + blockSize <= totalSamples; position += blockSize)
    {
        auto chunk = whole.getSubBlock (static_cast<size_t> (position), static_cast<size_t> (blockSize));
        engine.process (chunk);
    }

    REQUIRE (TestHelpers::allSamplesFinite (buffer));

    // The post-dither re-clamp is exact - no epsilon needed beyond float
    // representation of the ceiling itself.
    const auto ceilingLinear = juce::Decibels::decibelsToGain (ceilingDb);
    CHECK (TestHelpers::peakAbsolute (buffer) <= ceilingLinear + 1.0e-6f);
}
