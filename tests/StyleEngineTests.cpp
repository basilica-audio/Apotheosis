#include "dsp/TruePeakLimiterEngine.h"
#include "dsp/GainEnvelopeStages.h"
#include "TestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

// v0.4.0 F1/F7 (brief sections 3.1/3.7, tests T2-T5 and T8): the style
// engine's measurable claims. The zero-overshoot property (T2) is asserted
// WITHOUT the final safety clamp - the FIR-smoothed envelope alone must
// guarantee it - via the Tests-only clamp-bypass hook.
namespace
{
    constexpr int styleTransparent = 1;
    constexpr int stylePunchy = 2;
    constexpr int styleBus = 3;
    constexpr int styleSafe = 4;

    // The internal headroom margin the engine's gain target sits below the
    // user Ceiling (TruePeakLimiterEngine::headroomMarginDb - a frozen
    // regression constant, restated here per brief section 6 T2).
    constexpr float headroomMarginDb = 0.3f;

    const char* styleName (int index)
    {
        switch (index)
        {
            case styleTransparent: return "Transparent";
            case stylePunchy: return "Punchy";
            case styleBus: return "Bus";
            case styleSafe: return "Safe";
            default: return "Classic";
        }
    }
}

//==============================================================================
// T2 - zero-overshoot property, clamp bypassed.
//==============================================================================

TEST_CASE ("T2: FIR-smoothed non-Classic envelopes never overshoot the internal target, without the safety clamp",
           "[style][overshoot]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    constexpr float ceilingDb = -1.0f;
    constexpr int signalsPerCombination = 840; // 840 * 4 styles * 3 lookaheads = 10,080 randomized signals

    const auto ceilingLinear = juce::Decibels::decibelsToGain (ceilingDb);
    const auto bound = ceilingLinear * juce::Decibels::decibelsToGain (-headroomMarginDb) + 1.0e-6f;

    std::mt19937 rng (20260726u);
    std::uniform_real_distribution<float> unit (0.0f, 1.0f);

    for (const auto styleIndex : { styleTransparent, stylePunchy, styleBus, styleSafe })
    {
        for (const auto lookaheadMs : { 0.1f, 5.0f, 20.0f })
        {
            CAPTURE (styleName (styleIndex), lookaheadMs);

            TruePeakLimiterEngine engine;
            engine.setLookaheadMs (lookaheadMs);
            juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (blockSize), 2 };
            engine.prepare (spec);
            engine.setCeilingDb (ceilingDb);
            engine.setLimitStyle (styleIndex);
            engine.bypassCeilingClampForTests = true;

            int violations = 0;
            float worstPeak = 0.0f;
            int worstSignal = -1;

            juce::AudioBuffer<float> buffer (2, blockSize);

            for (int signal = 0; signal < signalsPerCombination; ++signal)
            {
                // Randomized signal, one of six families (white / pink-ish /
                // impulse train / square burst / fs-4 tone / DC steps),
                // deliberately allowed to exceed full scale. The engine is
                // NOT reset between signals - the stream continuity itself
                // is part of the property being tested.
                const auto family = signal % 6;
                const auto amplitude = 0.2f + 1.6f * unit (rng);

                for (int channel = 0; channel < 2; ++channel)
                {
                    auto* data = buffer.getWritePointer (channel);
                    float pinkState = 0.0f;
                    const auto period = 20 + static_cast<int> (unit (rng) * 480.0f);
                    const auto toneFrequency = sampleRate / 4.0;
                    const auto phase = unit (rng) * juce::MathConstants<float>::twoPi;
                    float dcLevel = (unit (rng) * 2.0f - 1.0f) * amplitude;

                    for (int sample = 0; sample < blockSize; ++sample)
                    {
                        const auto white = (unit (rng) * 2.0f - 1.0f) * amplitude;

                        switch (family)
                        {
                            case 0: data[sample] = white; break;
                            case 1:
                                pinkState = 0.97f * pinkState + 0.15f * white;
                                data[sample] = pinkState * 3.0f;
                                break;
                            case 2: data[sample] = sample % period == 0 ? ((sample / period) % 2 == 0 ? amplitude : -amplitude) : 0.0f; break;
                            case 3: data[sample] = ((sample / 64) % 2 == 0) ? (white > 0.0f ? amplitude : -amplitude) : 0.0f; break;
                            case 4:
                                data[sample] = amplitude
                                                * std::sin (static_cast<float> (juce::MathConstants<double>::twoPi * toneFrequency
                                                                                 * sample / sampleRate) + phase);
                                break;
                            default:
                                if (sample % 128 == 0)
                                    dcLevel = (unit (rng) * 2.0f - 1.0f) * amplitude;
                                data[sample] = dcLevel;
                                break;
                        }
                    }
                }

                engine.lastOsDomainPeakForTests = 0.0f;
                juce::dsp::AudioBlock<float> block (buffer);
                engine.process (block);

                if (engine.lastOsDomainPeakForTests > bound)
                {
                    ++violations;

                    if (engine.lastOsDomainPeakForTests > worstPeak)
                    {
                        worstPeak = engine.lastOsDomainPeakForTests;
                        worstSignal = signal;
                    }
                }
            }

            CAPTURE (worstPeak, worstSignal, bound);
            CHECK (violations == 0);
        }
    }
}

//==============================================================================
// T3 - attack/release envelope correctness.
//==============================================================================

namespace
{
    // Discrete step response of `numBoxes` cascaded moving averages of
    // `boxLength` samples each - the analytic reference for the attack
    // ramp's expected 1%-to-99% duration.
    std::vector<double> cascadedBoxStepResponse (int numBoxes, int boxLength)
    {
        // Impulse response by repeated convolution of the box kernel.
        std::vector<double> impulse { 1.0 };

        for (int box = 0; box < numBoxes; ++box)
        {
            std::vector<double> next (impulse.size() + static_cast<size_t> (boxLength) - 1, 0.0);

            for (size_t i = 0; i < impulse.size(); ++i)
                for (int j = 0; j < boxLength; ++j)
                    next[i + static_cast<size_t> (j)] += impulse[i] / static_cast<double> (boxLength);

            impulse = std::move (next);
        }

        std::vector<double> step (impulse.size(), 0.0);
        double sum = 0.0;

        for (size_t i = 0; i < impulse.size(); ++i)
        {
            sum += impulse[i];
            step[i] = sum;
        }

        return step;
    }

    double crossingIndex (const std::vector<double>& monotonic, double threshold)
    {
        for (size_t i = 1; i < monotonic.size(); ++i)
            if (monotonic[i] >= threshold)
            {
                const auto below = monotonic[i - 1];
                const auto above = monotonic[i];
                const auto fraction = above > below ? (threshold - below) / (above - below) : 0.0;
                return static_cast<double> (i - 1) + fraction;
            }

        return static_cast<double> (monotonic.size() - 1);
    }
}

TEST_CASE ("T3: attack ramp duration matches the cascaded-box FIR's analytic 1%-99% span, and is monotonic",
           "[style][attack]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 2048;
    constexpr float lookaheadMs = 5.0f;
    constexpr float ceilingDb = -6.0f;
    // 1 kHz carrier with the amplitude switch placed exactly on a carrier
    // zero crossing (1 kHz at 48 kHz crosses zero every 24 samples; 8160 is
    // a multiple of 24): a mid-cycle amplitude discontinuity would put
    // Gibbs overshoot from the upsampling filter into the raw gain demand
    // and contaminate the measured ramp with the release's recovery from
    // that transient over-detection.
    constexpr double frequency = 1000.0;
    constexpr int quietSamples = 8160;
    constexpr int loudSamples = 8224;
    constexpr float quietAmp = 0.05f;
    constexpr float loudAmp = 1.0f;

    struct StyleExpectation { int styleIndex; int numBoxes; float spanFraction; };

    for (const auto& styleInfo : { StyleExpectation { styleTransparent, 2, 1.0f },
                                   StyleExpectation { stylePunchy, 1, 0.4f },
                                   StyleExpectation { styleSafe, 3, 1.0f } })
    {
        CAPTURE (styleName (styleInfo.styleIndex));

        TruePeakLimiterEngine engine;
        engine.setLookaheadMs (lookaheadMs);
        juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (blockSize), 2 };
        engine.prepare (spec);
        engine.setCeilingDb (ceilingDb);
        engine.setLimitStyle (styleInfo.styleIndex);

        const auto latency = engine.getLatencySamples();
        constexpr int totalSamples = quietSamples + loudSamples; // 16384 = 8 blocks

        // Quiet passage -> 0 dBFS passage (phase-continuous carrier). The
        // attack ramp is observable on the still-quiet output while the
        // loud content is inside the lookahead delay.
        std::vector<float> input (static_cast<size_t> (totalSamples));

        for (int n = 0; n < totalSamples; ++n)
        {
            const auto amp = n < quietSamples ? quietAmp : loudAmp;
            input[static_cast<size_t> (n)] = amp
                                              * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * frequency * n / sampleRate));
        }

        juce::AudioBuffer<float> buffer (2, totalSamples);

        for (int channel = 0; channel < 2; ++channel)
            buffer.copyFrom (channel, 0, input.data(), totalSamples);

        juce::dsp::AudioBlock<float> whole (buffer);

        for (int position = 0; position < totalSamples; position += blockSize)
        {
            auto chunk = whole.getSubBlock (static_cast<size_t> (position), static_cast<size_t> (blockSize));
            engine.process (chunk);
        }

        // Per-sample gain estimate g[n] = y[n] / x[n - latency], valid away
        // from carrier zero crossings, calibrated by the measured passband
        // gain alpha of the oversampler round trip (its tiny ripple would
        // otherwise bias the 1% threshold).
        const auto* output = buffer.getReadPointer (0);

        std::vector<int> validIndex;
        std::vector<double> gain;

        double alphaSum = 0.0;
        int alphaCount = 0;

        for (int n = latency; n < totalSamples; ++n)
        {
            const auto reference = static_cast<double> (input[static_cast<size_t> (n - latency)]);

            if (std::abs (reference) < 0.35 * static_cast<double> (n - latency < quietSamples ? quietAmp : loudAmp))
                continue;

            const auto ratio = static_cast<double> (output[n]) / reference;
            validIndex.push_back (n);
            gain.push_back (ratio);

            // Calibration region: early quiet steady state, well before the
            // ramp can begin.
            if (n - latency > 1000 && n - latency < quietSamples - 2 * latency - 200)
            {
                alphaSum += ratio;
                ++alphaCount;
            }
        }

        REQUIRE (alphaCount > 100);
        const auto alpha = alphaSum / static_cast<double> (alphaCount);

        for (auto& value : gain)
            value /= alpha;

        // Steady-state end gain from the loud region's tail.
        double endGainSum = 0.0;
        int endGainCount = 0;

        for (size_t k = 0; k < validIndex.size(); ++k)
            if (validIndex[k] > totalSamples - 2048)
            {
                endGainSum += gain[k];
                ++endGainCount;
            }

        REQUIRE (endGainCount > 50);
        const auto endGain = endGainSum / static_cast<double> (endGainCount);
        REQUIRE (endGain < 0.6); // sanity: the loud passage really is limited (~ -6.3 dB target)

        // Transition fraction per valid sample; find the 1% and 99%
        // crossings with linear interpolation between valid samples.
        std::vector<double> fraction (gain.size());

        for (size_t k = 0; k < gain.size(); ++k)
            fraction[k] = (1.0 - gain[k]) / (1.0 - endGain);

        auto crossingTime = [&] (double threshold) -> double
        {
            for (size_t k = 1; k < fraction.size(); ++k)
                if (fraction[k] >= threshold && validIndex[k] > quietSamples / 2)
                {
                    const auto f0 = fraction[k - 1];
                    const auto f1 = fraction[k];
                    const auto t0 = static_cast<double> (validIndex[k - 1]);
                    const auto t1 = static_cast<double> (validIndex[k]);
                    const auto mix = f1 > f0 ? (threshold - f0) / (f1 - f0) : 0.0;
                    return t0 + (t1 - t0) * mix;
                }

            return -1.0;
        };

        const auto t01 = crossingTime (0.01);
        const auto t99 = crossingTime (0.99);
        REQUIRE (t01 >= 0.0);
        REQUIRE (t99 > t01);

        const auto measuredDurationBase = t99 - t01;

        // Analytic expectation: the smoother is `numBoxes` cascaded boxes
        // of span/numBoxes OS-rate samples (span = spanFraction * lookahead
        // window; attack parameter at its 0 ms default = full lookahead).
        constexpr int oversamplingFactor = 4;
        const auto lookaheadOsSamples = juce::roundToInt (lookaheadMs * 0.001 * sampleRate) * oversamplingFactor;
        const auto span = juce::jmax (1, juce::roundToInt (static_cast<float> (lookaheadOsSamples) * styleInfo.spanFraction));
        const auto boxLength = juce::jmax (1, span / styleInfo.numBoxes);

        const auto stepResponse = cascadedBoxStepResponse (styleInfo.numBoxes, boxLength);
        const auto expectedDurationBase = (crossingIndex (stepResponse, 0.99) - crossingIndex (stepResponse, 0.01))
                                           / static_cast<double> (oversamplingFactor);

        // Tolerance: the brief's +/- 2% of the lookahead window N, plus two
        // samples of interpolation slop from the zero-crossing-gapped
        // measurement grid.
        const auto toleranceBase = 0.02 * static_cast<double> (lookaheadOsSamples) / oversamplingFactor + 2.0;

        CAPTURE (measuredDurationBase, expectedDurationBase, toleranceBase);
        CHECK (std::abs (measuredDurationBase - expectedDurationBase) <= toleranceBase);

        // Monotonicity during the ramp (no zipper): every valid gain sample
        // between the crossings is non-increasing within measurement noise.
        int nonMonotonic = 0;

        for (size_t k = 1; k < gain.size(); ++k)
        {
            const auto t = static_cast<double> (validIndex[k]);

            if (t > t01 && t < t99 && gain[k] > gain[k - 1] + 2.0e-3)
                ++nonMonotonic;
        }

        CHECK (nonMonotonic == 0);
    }
}

TEST_CASE ("T3: dual-stage release matches the double-precision reference recurrence within 0.02 dB per sample",
           "[style][release]")
{
    // Component-level reference-model equivalence (brief section 6 T3, as
    // revised): the engine's DualStageRelease (float state mixed with
    // double internals) against an independent all-double implementation of
    // section 3.1's exact recurrence - min(s, rFastCapped * rSlow), tau
    // 50 ms / 800 ms, 2 dB fast cap - driven by a box-smoothed demand
    // trace. The closed-form 1 - e^{-t}(1+t) fit of the equal-tau cascade
    // is deliberately NOT asserted here (it describes the existing "Smooth"
    // ReleaseCurve's family, not this min-combiner - brief revision note 7).
    constexpr double fsOs = 48000.0 * 4.0;
    constexpr double tauFast = 0.050;
    constexpr double tauSlow = 0.800;
    constexpr double capDb = 2.0;
    constexpr double startGain = 0.5; // -6 dB demand releasing to unity
    constexpr int stepIndex = 4000;
    constexpr int totalSamples = static_cast<int> (fsOs * 2.5);
    constexpr int smootherBoxLength = 480;

    // Box-smoothed demand trace: raw demand 0.5 before the step, 1.0
    // after, through two cascaded 480-sample boxes (double precision).
    std::vector<float> smoothedTrace (static_cast<size_t> (totalSamples));
    {
        std::vector<double> box1 (smootherBoxLength, startGain);
        std::vector<double> box2 (smootherBoxLength, startGain);
        double sum1 = startGain * smootherBoxLength;
        double sum2 = startGain * smootherBoxLength;
        int pos = 0;

        for (int n = 0; n < totalSamples; ++n)
        {
            const auto raw = n < stepIndex ? startGain : 1.0;

            sum1 += raw - box1[static_cast<size_t> (pos)];
            box1[static_cast<size_t> (pos)] = raw;
            const auto stage1 = sum1 / smootherBoxLength;

            sum2 += stage1 - box2[static_cast<size_t> (pos)];
            box2[static_cast<size_t> (pos)] = stage1;
            smoothedTrace[static_cast<size_t> (n)] = static_cast<float> (sum2 / smootherBoxLength);

            pos = (pos + 1) % smootherBoxLength;
        }
    }

    // Engine stage under test.
    DualStageRelease stage;
    stage.setFastCapDb (capDb);
    stage.setFastCoefficient (DualStageRelease::coefficientForTau (tauFast, fsOs));
    stage.setSlowCoefficient (DualStageRelease::coefficientForTau (tauSlow, fsOs));
    stage.seed (startGain);

    // Independent double-precision reference of the same recurrence.
    const auto kFast = 1.0 / (tauFast * fsOs + 1.0);
    const auto kSlow = 1.0 / (tauSlow * fsOs + 1.0);
    const auto capLin = std::pow (10.0, -capDb / 20.0);
    double refFast = std::max (startGain, capLin);
    double refSlow = startGain / refFast;

    double worstErrorDb = 0.0;
    std::vector<double> engineEnvelope (static_cast<size_t> (totalSamples));

    for (int n = 0; n < totalSamples; ++n)
    {
        const auto s = smoothedTrace[static_cast<size_t> (n)];

        const auto engineOut = static_cast<double> (stage.process (s));
        engineEnvelope[static_cast<size_t> (n)] = engineOut;

        const auto sd = static_cast<double> (s);
        const auto sTop = std::max (sd, capLin);
        const auto sRest = sd / sTop;
        refFast += (sTop - refFast) * kFast;
        refSlow += (sRest - refSlow) * kSlow;
        const auto referenceOut = std::min (sd, refFast * refSlow);

        worstErrorDb = std::max (worstErrorDb, std::abs (20.0 * std::log10 (engineOut / referenceOut)));
    }

    CAPTURE (worstErrorDb);
    CHECK (worstErrorDb <= 0.02);

    // Tail slope: over the last 3 dB of recovery (restricted to well after
    // the fast stage has finished, > 5 * tauFast past the step), the decay
    // of (1 - gain) must be a pure tauSlow exponential within +/-10%.
    const auto fitStart = stepIndex + static_cast<int> (5.0 * tauFast * fsOs);
    std::vector<double> times, logs;

    for (int n = fitStart; n < totalSamples; ++n)
    {
        const auto remaining = 1.0 - engineEnvelope[static_cast<size_t> (n)];

        // GR window: between 3 dB (1-g ~ 0.292) and 0.3 dB (1-g ~ 0.034).
        if (remaining < 0.292 && remaining > 0.034)
        {
            times.push_back (static_cast<double> (n));
            logs.push_back (std::log (remaining));
        }
    }

    REQUIRE (times.size() > 1000);

    // Least-squares slope of log(1-g) vs n.
    const auto count = static_cast<double> (times.size());
    double meanT = 0.0, meanL = 0.0;

    for (size_t k = 0; k < times.size(); ++k)
    {
        meanT += times[k];
        meanL += logs[k];
    }

    meanT /= count;
    meanL /= count;

    double covariance = 0.0, variance = 0.0;

    for (size_t k = 0; k < times.size(); ++k)
    {
        covariance += (times[k] - meanT) * (logs[k] - meanL);
        variance += (times[k] - meanT) * (times[k] - meanT);
    }

    const auto slopePerSample = covariance / variance; // negative
    const auto measuredTauSamples = -1.0 / slopePerSample;
    const auto expectedTauSamples = tauSlow * fsOs;

    CAPTURE (measuredTauSamples, expectedTauSamples);
    CHECK (measuredTauSamples >= expectedTauSamples * 0.9);
    CHECK (measuredTauSamples <= expectedTauSamples * 1.1);
}

//==============================================================================
// T4 - low-frequency intermodulation kill.
//==============================================================================

namespace
{
    double renderAndMeasureThd (int styleIndex, float lookaheadMs)
    {
        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 2048;
        constexpr int totalSamples = 48000; // 1 s
        constexpr double fundamental = 50.0;

        TruePeakLimiterEngine engine;
        engine.setLookaheadMs (lookaheadMs);
        juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (blockSize), 2 };
        engine.prepare (spec);
        engine.setCeilingDb (-1.0f);
        engine.setReleaseMs (100.0f);
        engine.setLimitStyle (styleIndex);

        // +3 dB over the ceiling: -1 dBTP ceiling, +2 dBFS sine.
        const auto amplitude = juce::Decibels::decibelsToGain (2.0f);

        juce::AudioBuffer<float> buffer (2, totalSamples);
        TestHelpers::fillWithSine (buffer, sampleRate, fundamental, amplitude);

        juce::dsp::AudioBlock<float> whole (buffer);

        for (int position = 0; position < totalSamples; position += blockSize)
        {
            const auto samples = std::min (blockSize, totalSamples - position);
            auto chunk = whole.getSubBlock (static_cast<size_t> (position), static_cast<size_t> (samples));
            engine.process (chunk);
        }

        // Analyse the final 25 fundamental periods (leakage-free window).
        constexpr int analysisSamples = 24000;
        return TestHelpers::measureThd (buffer, 0, totalSamples - analysisSamples, analysisSamples,
                                        sampleRate, fundamental);
    }
}

TEST_CASE ("T4: 6 dB of sustained 50 Hz limiting is distortion-free at 10 ms lookahead, and the lookahead window is what does the work",
           "[style][thd]")
{
    // Deviation from the brief's literal T4 (called out in the PR
    // description): the brief expected Classic's THD to be >= 3x
    // Transparent's on the identical input, adapted from RTP section 5.6's
    // reference architecture where the sliding-min window is attack-sized.
    // In THIS engine the window has always been LOOKAHEAD-sized (v0.1
    // architecture, praised by the survey as the correct core idea), so at
    // 10 ms lookahead the window bridges the 50 Hz half-period for Classic
    // and non-Classic alike - both measure ~0.000% THD, and a 3x ratio
    // between two zeros is unassertable. What IS measurable, and what
    // RTP 5.6's "short attack degrades THD" sanity check adapts to here,
    // is the LOOKAHEAD window doing the work: shrink it below the 50 Hz
    // half-period and the demand ripples at cycle rate, which the release
    // stages ride into measurable distortion.
    const auto transparentAt10ms = renderAndMeasureThd (styleTransparent, 10.0f);
    const auto classicAt10ms = renderAndMeasureThd (0, 10.0f);
    const auto transparentAt3ms = renderAndMeasureThd (styleTransparent, 3.0f);

    CAPTURE (transparentAt10ms, classicAt10ms, transparentAt3ms);

    // The product claim (brief T4, unchanged): Transparent, 50 Hz +3 dB
    // over the ceiling, 10 ms lookahead -> THD < 1%.
    CHECK (transparentAt10ms < 0.01);

    // Classic at the same settings is equally clean - the v0.2.0
    // rectangular sliding-min HOLD already bridges LF cycles at 10 ms
    // (regression-protecting the architecture's existing strength).
    CHECK (classicAt10ms < 0.01);

    // Sanity check that the measurement can detect distortion at all and
    // that the window is what prevents it: at 3 ms lookahead (window well
    // below the 50 Hz half-period) the same Transparent render distorts
    // by more than an order of magnitude over the 1% bound's headroom.
    CHECK (transparentAt3ms > 10.0 * transparentAt10ms);
    CHECK (transparentAt3ms > 0.02);
}

//==============================================================================
// T5 - dual-release behaviour on the drum-burst fixture.
//==============================================================================

TEST_CASE ("T5: transient bursts recover fast while the quiet bed's gain barely moves", "[style][release]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 128; // fine-grained GR meter time resolution (~2.7 ms)
    constexpr int totalSamples = static_cast<int> (sampleRate * 2.5);
    constexpr int burstIntervalSamples = 24000; // 2 Hz
    constexpr int firstBurstSample = 24000;
    constexpr int burstDurationSamples = 48; // 1 ms

    TruePeakLimiterEngine engine;
    juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (blockSize), 2 };
    engine.prepare (spec);
    engine.setInputGainDb (6.0f);
    engine.setCeilingDb (-1.0f);
    engine.setLimitStyle (styleTransparent);

    // Pink-ish bed at -22 dBFS (still well under the ceiling after the
    // +6 dB input gain) with 1 ms full-scale bursts every 0.5 s.
    std::mt19937 rng (77u);
    std::uniform_real_distribution<float> unit (-1.0f, 1.0f);

    juce::AudioBuffer<float> signal (2, totalSamples);

    for (int channel = 0; channel < 2; ++channel)
    {
        auto* data = signal.getWritePointer (channel);
        float pinkState = 0.0f;

        for (int n = 0; n < totalSamples; ++n)
        {
            pinkState = 0.95f * pinkState + 0.2f * unit (rng);
            data[n] = pinkState * 0.24f; // ~ -22 dBFS RMS-ish bed

            const auto sinceBurst = (n - firstBurstSample) % burstIntervalSamples;

            if (n >= firstBurstSample && sinceBurst >= 0 && sinceBurst < burstDurationSamples)
                data[n] = 0.9f * unit (rng);
        }
    }

    // Process in fine blocks, recording the GR meter per block.
    std::vector<float> gainReductionPerBlock;
    juce::dsp::AudioBlock<float> whole (signal);

    for (int position = 0; position + blockSize <= totalSamples; position += blockSize)
    {
        auto chunk = whole.getSubBlock (static_cast<size_t> (position), static_cast<size_t> (blockSize));
        engine.process (chunk);
        gainReductionPerBlock.push_back (engine.getGainReductionDb());
    }

    const auto blocksPerBurst = burstIntervalSamples / blockSize;
    const auto grAtBlock = [&] (int sampleIndex) { return gainReductionPerBlock[static_cast<size_t> (sampleIndex / blockSize)]; };

    int burstsChecked = 0;

    for (int burstStart = firstBurstSample; burstStart + burstIntervalSamples <= totalSamples; burstStart += burstIntervalSamples)
    {
        // Deepest GR within 15 ms of the burst.
        float peakGr = 0.0f;

        for (int block = burstStart / blockSize; block <= (burstStart + 720) / blockSize; ++block)
            peakGr = std::min (peakGr, gainReductionPerBlock[static_cast<size_t> (block)]);

        REQUIRE (peakGr < -3.0f); // the burst really drives several dB of GR

        // >= 80% recovered 60 ms after the burst ends.
        const auto grAfter60ms = grAtBlock (burstStart + burstDurationSamples + 2880);
        CAPTURE (burstStart, peakGr, grAfter60ms);
        CHECK (std::abs (grAfter60ms) <= 0.2f * std::abs (peakGr));

        // Bed GR excursion < 1 dB outside the burst's neighbourhood
        // (150 ms after the burst to 20 ms before the next).
        float worstBedGr = 0.0f;

        const auto bedFrom = (burstStart + 7200) / blockSize;
        const auto bedTo = (burstStart + burstIntervalSamples - 960) / blockSize;

        for (int block = bedFrom; block <= bedTo && block < static_cast<int> (gainReductionPerBlock.size()); ++block)
            worstBedGr = std::min (worstBedGr, gainReductionPerBlock[static_cast<size_t> (block)]);

        CAPTURE (worstBedGr);
        CHECK (worstBedGr > -1.0f);

        ++burstsChecked;
    }

    REQUIRE (burstsChecked >= 3);
    juce::ignoreUnused (blocksPerBurst);
}

//==============================================================================
// T8 - buffer-size invariance of the fixed-rate Auto Release (F7).
//==============================================================================

TEST_CASE ("T8: Transparent with Auto Release engaged renders identically at 64- and 2048-sample buffers",
           "[style][autorelease][bufferinvariance]")
{
    constexpr double sampleRate = 48000.0;
    // The brief's 30 s programme, rounded to a whole number of 2048-sample
    // blocks (703 * 2048 = 1,439,744 ~ 29.995 s) so both renders process
    // exactly the same sample range.
    constexpr int totalSamples = 703 * 2048;

    // Deterministic 30 s programme with strong loudness dynamics so the
    // Auto Release depth integrator actually moves: an amplitude-modulated
    // mix of a 220 Hz carrier and filtered noise, with alternating loud/
    // quiet passages.
    std::vector<float> programme (static_cast<size_t> (totalSamples));
    {
        std::mt19937 rng (4242u);
        std::uniform_real_distribution<float> unit (-1.0f, 1.0f);
        float noiseState = 0.0f;

        for (int n = 0; n < totalSamples; ++n)
        {
            noiseState = 0.9f * noiseState + 0.3f * unit (rng);
            const auto passage = (n / 48000) % 3; // 1 s alternation
            const auto level = passage == 0 ? 1.1f : (passage == 1 ? 0.25f : 0.7f);
            const auto carrier = std::sin (static_cast<float> (juce::MathConstants<double>::twoPi * 220.0 * n / sampleRate));
            programme[static_cast<size_t> (n)] = level * (0.8f * carrier + 0.4f * noiseState);
        }
    }

    const auto render = [&] (int blockSize) -> juce::AudioBuffer<float>
    {
        TruePeakLimiterEngine engine;
        juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (blockSize), 2 };
        engine.prepare (spec);
        engine.setCeilingDb (-1.0f);
        engine.setAutoReleasePercent (60.0f);
        engine.setLimitStyle (styleTransparent);

        juce::AudioBuffer<float> buffer (2, totalSamples);

        for (int channel = 0; channel < 2; ++channel)
            buffer.copyFrom (channel, 0, programme.data(), totalSamples);

        juce::dsp::AudioBlock<float> whole (buffer);

        for (int position = 0; position + blockSize <= totalSamples; position += blockSize)
        {
            auto chunk = whole.getSubBlock (static_cast<size_t> (position), static_cast<size_t> (blockSize));
            engine.process (chunk);
        }

        return buffer;
    };

    const auto smallBlocks = render (64);
    const auto largeBlocks = render (2048);

    // Null depth between the two renders (identical latency - same
    // prepare-time parameters - so no alignment needed).
    double signalPower = 0.0;
    double differencePower = 0.0;

    for (int channel = 0; channel < 2; ++channel)
    {
        const auto* a = smallBlocks.getReadPointer (channel);
        const auto* b = largeBlocks.getReadPointer (channel);

        for (int n = 0; n < totalSamples; ++n)
        {
            const auto difference = static_cast<double> (a[n]) - static_cast<double> (b[n]);
            signalPower += static_cast<double> (a[n]) * static_cast<double> (a[n]);
            differencePower += difference * difference;
        }
    }

    REQUIRE (signalPower > 0.0);

    const auto nullDepthDb = differencePower > 0.0
                                  ? 10.0 * std::log10 (signalPower / differencePower)
                                  : 200.0;

    CAPTURE (nullDepthDb);
    CHECK (nullDepthDb >= 80.0);

    // Classic is exempt from this guarantee (documented: its Auto Release
    // depth average updates once per host chunk, the verbatim v0.2.0 law) -
    // nothing asserted for it here by design.
}
