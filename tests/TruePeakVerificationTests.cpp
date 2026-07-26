#include "dsp/TruePeakLimiterEngine.h"
#include "dsp/TruePeakInterpolator.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <vector>

// v0.4.0 F2/F3 verification (brief section 6, T6/T7/T9/T10): the measured -
// not margin-faith - true-peak guarantee. Two test-side reference meters
// live here, deliberately independent of src/dsp/TruePeakInterpolator.h:
//
//  - ComplianceMeter4x: an independent re-implementation of the SAME
//    BS.1770-4 4x 48-tap measurement (its own copy of the coefficient
//    table, double-precision accumulation). T7 (a)'s product claim is
//    stated against this: "a compliant 4x meter reads <= ceiling".
//  - ReferenceMeter8x: a test-only 8x, 192-tap windowed-sinc interpolating
//    meter - higher-resolution ground truth whose worst-case under-read
//    (0.136 dB at f_norm 0.45) is far below the compliant 4x meter's
//    (0.554 dB). T7 (b)/(c) budget exactly that difference.
namespace
{
    // BS.1770-4 Annex 2 coefficient table - independent copy (standards
    // data). Layout: [phase][tap], 12 taps per phase.
    constexpr double kComplianceTaps[4][12] = {
        { 0.0017089843750, 0.0109863281250, -0.0196533203125, 0.0332031250000,
          -0.0594482421875, 0.1373291015625, 0.9721679687500, -0.1022949218750,
          0.0476074218750, -0.0266113281250, 0.0148925781250, -0.0083007812500 },
        { -0.0291748046875, 0.0292968750000, -0.0517578125000, 0.0891113281250,
          -0.1665039062500, 0.4650878906250, 0.7797851562500, -0.2003173828125,
          0.1015625000000, -0.0582275390625, 0.0330810546875, -0.0189208984375 },
        { -0.0189208984375, 0.0330810546875, -0.0582275390625, 0.1015625000000,
          -0.2003173828125, 0.7797851562500, 0.4650878906250, -0.1665039062500,
          0.0891113281250, -0.0517578125000, 0.0292968750000, -0.0291748046875 },
        { -0.0083007812500, 0.0148925781250, -0.0266113281250, 0.0476074218750,
          -0.1022949218750, 0.9721679687500, 0.1373291015625, -0.0594482421875,
          0.0332031250000, -0.0196533203125, 0.0109863281250, 0.0017089843750 },
    };

    // Max absolute 4x-interpolated magnitude over one channel's samples
    // (double precision, streaming). The meter processes the WHOLE signal
    // from sample 0 but only accumulates the peak from `trackFromSample` on
    // - measuring a mid-signal window with a cold filter would read the
    // filter's own onset transient, not the signal.
    double complianceMeter4xChannel (const float* samples, int numSamples, int trackFromSample)
    {
        std::array<double, 12> history {};
        int writePos = 0;
        double peak = 0.0;

        for (int n = 0; n < numSamples; ++n)
        {
            history[static_cast<size_t> (writePos)] = static_cast<double> (samples[n]);

            for (int phase = 0; phase < 4; ++phase)
            {
                double accumulator = 0.0;

                for (int tap = 0; tap < 12; ++tap)
                {
                    const auto index = (writePos - tap + 12) % 12;
                    accumulator += kComplianceTaps[phase][tap] * history[static_cast<size_t> (index)];
                }

                if (n >= trackFromSample)
                    peak = std::max (peak, std::abs (accumulator));
            }

            writePos = (writePos + 1) % 12;
        }

        return peak;
    }

    double complianceMeter4x (const juce::AudioBuffer<float>& buffer, int trackFromSample = 0)
    {
        double peak = 0.0;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            peak = std::max (peak, complianceMeter4xChannel (buffer.getReadPointer (channel),
                                                             buffer.getNumSamples(), trackFromSample));

        return peak;
    }

    // Test-only 8x 192-tap reference interpolator: windowed-sinc prototype
    // (Blackman-Harris 4-term window), 8 phases x 24 taps, each phase
    // normalised to unity DC gain, double precision throughout.
    struct ReferenceMeter8x
    {
        static constexpr int numPhases = 8;
        static constexpr int tapsPerPhase = 24;
        static constexpr int prototypeLength = numPhases * tapsPerPhase; // 192

        std::array<std::array<double, tapsPerPhase>, numPhases> phases {};

        ReferenceMeter8x()
        {
            std::array<double, prototypeLength> prototype {};
            const auto centre = (static_cast<double> (prototypeLength) - 1.0) / 2.0; // 95.5

            for (int i = 0; i < prototypeLength; ++i)
            {
                const auto t = (static_cast<double> (i) - centre) / static_cast<double> (numPhases);
                const auto sinc = t == 0.0 ? 1.0
                                           : std::sin (juce::MathConstants<double>::pi * t)
                                                 / (juce::MathConstants<double>::pi * t);

                // Blackman-Harris 4-term.
                const auto w = 2.0 * juce::MathConstants<double>::pi * static_cast<double> (i)
                                / static_cast<double> (prototypeLength - 1);
                const auto window = 0.35875 - 0.48829 * std::cos (w) + 0.14128 * std::cos (2.0 * w)
                                     - 0.01168 * std::cos (3.0 * w);

                prototype[static_cast<size_t> (i)] = sinc * window;
            }

            // Polyphase split; phase p takes prototype[p], prototype[p+8],
            // ... - then normalise each phase to unity DC gain so a
            // constant (and any low-frequency content) is reproduced at
            // exactly its own amplitude.
            for (int phase = 0; phase < numPhases; ++phase)
            {
                double phaseSum = 0.0;

                for (int tap = 0; tap < tapsPerPhase; ++tap)
                {
                    const auto value = prototype[static_cast<size_t> (tap * numPhases + phase)];
                    phases[static_cast<size_t> (phase)][static_cast<size_t> (tap)] = value;
                    phaseSum += value;
                }

                for (auto& tapValue : phases[static_cast<size_t> (phase)])
                    tapValue /= phaseSum;
            }
        }

        // Same warm-up discipline as complianceMeter4x above: process from
        // sample 0, track the peak from `trackFromSample` on.
        double measureChannel (const float* samples, int numSamples, int trackFromSample) const
        {
            std::vector<double> history (tapsPerPhase, 0.0);
            int writePos = 0;
            double peak = 0.0;

            for (int n = 0; n < numSamples; ++n)
            {
                history[static_cast<size_t> (writePos)] = static_cast<double> (samples[n]);

                for (int phase = 0; phase < numPhases; ++phase)
                {
                    double accumulator = 0.0;

                    for (int tap = 0; tap < tapsPerPhase; ++tap)
                    {
                        const auto index = (writePos - tap + tapsPerPhase) % tapsPerPhase;
                        accumulator += phases[static_cast<size_t> (phase)][static_cast<size_t> (tap)]
                                        * history[static_cast<size_t> (index)];
                    }

                    if (n >= trackFromSample)
                        peak = std::max (peak, std::abs (accumulator));
                }

                writePos = (writePos + 1) % tapsPerPhase;
            }

            return peak;
        }

        double measure (const juce::AudioBuffer<float>& buffer, int trackFromSample = 0) const
        {
            double peak = 0.0;

            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                peak = std::max (peak, measureChannel (buffer.getReadPointer (channel),
                                                       buffer.getNumSamples(), trackFromSample));

            return peak;
        }
    };

    const ReferenceMeter8x& referenceMeter8x()
    {
        static const ReferenceMeter8x meter;
        return meter;
    }

    double toDb (double linear)
    {
        return 20.0 * std::log10 (std::max (linear, 1.0e-12));
    }

    // The T7 render corpus: same five signals and settings as the golden
    // fixtures (tests/RegressionTests.cpp) - continuous 8192-sample streams
    // processed in 2048-sample chunks. contentBelowFsOver8 tags the items
    // whose spectral content sits at or below fs/8, where a compliant 4x
    // meter's under-read is negligible (T7 (c)).
    struct CorpusItem
    {
        const char* name;
        double frequencyHz; // <= 0 -> silence
        float amplitude;
        bool contentBelowFsOver8;
    };

    const std::vector<CorpusItem>& t7Corpus()
    {
        static const std::vector<CorpusItem> corpus = {
            { "300 Hz sine 0.7", 300.0, 0.7f, true },
            { "5 kHz sine 0.9", 5000.0, 0.9f, true },
            { "0.45*fs sine 0.98 (near-Nyquist ISP)", 48000.0 * 0.45, 0.98f, false },
            { "silence", -1.0, 0.0f, true },
            { "1 kHz sine 1.0 (full scale)", 1000.0, 1.0f, true },
        };
        return corpus;
    }

    juce::AudioBuffer<float> renderT7 (const CorpusItem& item, bool guardOn)
    {
        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 2048;
        constexpr int numBlocks = 4;
        constexpr int totalSamples = blockSize * numBlocks;

        TruePeakLimiterEngine engine;
        juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (blockSize), 2 };
        engine.prepare (spec);

        engine.setInputGainDb (4.0f);
        engine.setCeilingDb (-1.0f);
        engine.setReleaseMs (60.0f);
        engine.setTpGuard (guardOn);

        juce::AudioBuffer<float> buffer (2, totalSamples);
        buffer.clear();

        if (item.frequencyHz > 0.0)
            TestHelpers::fillWithSine (buffer, sampleRate, item.frequencyHz, item.amplitude);

        juce::dsp::AudioBlock<float> whole (buffer);

        for (int block = 0; block < numBlocks; ++block)
        {
            auto chunk = whole.getSubBlock (static_cast<size_t> (block * blockSize),
                                            static_cast<size_t> (blockSize));
            engine.process (chunk);
        }

        return buffer;
    }
}

//==============================================================================
// T6 - ISP detector accuracy.
//==============================================================================

TEST_CASE ("T6: compile-time coefficient symmetry of the BS.1770-4 bank", "[truepeak][detector]")
{
    STATIC_REQUIRE (truepeak_detail::phasesAreSymmetric());
}

TEST_CASE ("T6: detector reads the hidden inter-sample peak of a phase-shifted fs/4 sine within the compliant band",
           "[truepeak][detector]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int numSamples = 4800;
    constexpr double amplitude = 0.5; // -6.02 dBTP true peak

    TruePeakInterpolator detector;
    detector.prepare (sampleRate);

    // Phase pi/4 puts every SAMPLE at amplitude/sqrt(2) (-9.03 dBFS) while
    // the continuous waveform still reaches 0.5 between samples - the
    // canonical hidden-ISP construction (RTP section 5.1).
    double peak = 0.0;

    for (int n = 0; n < numSamples; ++n)
    {
        const auto value = static_cast<float> (
            amplitude * std::sin (juce::MathConstants<double>::pi / 2.0 * n + juce::MathConstants<double>::pi / 4.0));

        for (int channel = 0; channel < 2; ++channel)
            peak = std::max (peak, static_cast<double> (detector.processSample (channel, value)));

        detector.advanceWritePosition();
    }

    const auto readingDb = toDb (peak);
    constexpr double trueDb = -6.0206;

    CAPTURE (readingDb);

    // BS.1770-4 Appendix 1: a compliant 4x meter can under-read up to
    // 0.554 dB at f_norm 0.45 (0.688 dB at 0.5); it must never over-read
    // meaningfully.
    CHECK (readingDb >= trueDb - 0.554 - 0.14); // -0.688 dB worst case at f_norm 0.5, small ripple slack
    CHECK (readingDb <= trueDb + 0.1);
}

TEST_CASE ("T6: phase-sweep readings stay inside the 20log10(cos(pi*f_norm/4)) compliance envelope at every tested f_norm",
           "[truepeak][detector]")
{
    // BS.1770-4 Appendix 1's 20log10(cos(pi*f_norm/4)) is the WORST-CASE
    // under-read a compliant n=4 meter is allowed: the detector must never
    // read further below the true peak than that, at any peak position
    // relative to its 4x grid. The brief's literal "envelope matches the
    // formula within 0.05 dB" cannot hold as an equality against the real
    // filter: the Appendix's envelope models ideal 4x sampling, while the
    // actual 48-tap bank adds passband ripple (~ +0.25 dB at f_norm 0.5)
    // and non-ideal interpolation phases, which places the measured
    // worst-case well INSIDE (better than) the analytic bound. Asserted
    // here as the compliance band it is: every phase-sweep reading is
    // >= true + envelope - 0.05 dB (never worse than the permitted
    // under-read) and <= true + 0.3 dB (over-read bounded by the bank's
    // documented passband ripple). Deviation from the brief's phrasing is
    // called out in the PR description.
    constexpr double sampleRate = 48000.0;
    constexpr int numSamples = 4800;
    constexpr double amplitude = 0.5;

    for (const auto fNorm : { 0.40, 0.45, 0.50 })
    {
        const auto frequency = fNorm * sampleRate / 2.0;
        const auto envelopeDb = 20.0 * std::log10 (std::cos (juce::MathConstants<double>::pi * fNorm / 4.0));

        double worstReading = std::numeric_limits<double>::max();
        double bestReading = 0.0;

        for (int phaseStep = 0; phaseStep < 64; ++phaseStep)
        {
            const auto phaseOffset = juce::MathConstants<double>::pi / 2.0 * static_cast<double> (phaseStep) / 64.0;

            TruePeakInterpolator detector;
            detector.prepare (sampleRate);

            double peak = 0.0;

            for (int n = 0; n < numSamples; ++n)
            {
                const auto value = static_cast<float> (
                    amplitude * std::sin (2.0 * juce::MathConstants<double>::pi * frequency * n / sampleRate + phaseOffset));

                // Skip the detector's own 12-sample warm-up.
                const auto reading = static_cast<double> (detector.processSample (0, value));

                if (n >= 12)
                    peak = std::max (peak, reading);

                detector.advanceWritePosition();
            }

            worstReading = std::min (worstReading, peak);
            bestReading = std::max (bestReading, peak);
        }

        const auto worstDb = toDb (worstReading) - toDb (amplitude);
        const auto bestDb = toDb (bestReading) - toDb (amplitude);

        CAPTURE (fNorm, worstDb, bestDb, envelopeDb);

        // Never under-read beyond the compliance envelope...
        CHECK (worstDb >= envelopeDb - 0.05);

        // ...and over-read bounded by the bank's passband ripple.
        CHECK (bestDb <= 0.3);
    }
}

//==============================================================================
// T7 - measured true-peak ceiling (the product claim).
//==============================================================================

TEST_CASE ("T7: with True Peak Guard on, a compliant 4x meter reads at or below the Ceiling on every corpus item",
           "[truepeak][tpguard]")
{
    constexpr double ceilingDb = -1.0;
    // Skip the first half: the +4 dB input-gain ramp (50 ms = 2400 samples)
    // plus release settling - the claim is about steady-state programme.
    constexpr int settleSamples = 4096;

    for (const auto& item : t7Corpus())
    {
        CAPTURE (item.name);

        const auto output = renderT7 (item, true);

        // (a) The product claim: measured by an independent copy of the
        // SAME BS.1770-4 4x 48-tap measurement, output <= ceiling+0.05 dB -
        // on EVERY item including 0.45*fs.
        const auto compliantDb = toDb (complianceMeter4x (output, settleSamples));
        CAPTURE (compliantDb);
        CHECK (compliantDb <= ceilingDb + 0.05);

        // (b) Measured by the 8x 192-tap reference: <= ceiling+0.65 dB
        // corpus-wide - budgets the compliant meter's <= 0.554 dB
        // worst-case under-read at f_norm 0.45 (the guard corrects until
        // its OWN 4x detector reads <= ceiling; a higher-resolution meter
        // may legitimately see up to the 4x under-read more).
        const auto referenceDb = toDb (referenceMeter8x().measure (output, settleSamples));
        CAPTURE (referenceDb);
        CHECK (referenceDb <= ceilingDb + 0.65);

        // (c) For content at or below fs/8, the 4x under-read is
        // negligible, so even the 8x reference must read <= ceiling+0.05.
        if (item.contentBelowFsOver8)
            CHECK (referenceDb <= ceilingDb + 0.05);
    }
}

TEST_CASE ("T7: with True Peak Guard off, the documented margin bounds the 8x-reference measurement", "[truepeak][tpguard]")
{
    constexpr double ceilingDb = -1.0;
    constexpr int settleSamples = 4096; // see the guard-on case above

    for (const auto& item : t7Corpus())
    {
        CAPTURE (item.name);

        const auto output = renderT7 (item, false);
        const auto referenceDb = toDb (referenceMeter8x().measure (output, settleSamples));

        CAPTURE (referenceDb);
        CHECK (referenceDb <= ceilingDb + 0.65);
    }
}

TEST_CASE ("T7: the guard is a micro-correction - it does not audibly duck content that never exceeds the ceiling",
           "[truepeak][tpguard]")
{
    // A signal comfortably under the ceiling must pass the guard bit-
    // untouched (gain stays at exactly 1.0): render the same under-ceiling
    // signal with the guard on and off and require identical output.
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 2048;

    const auto render = [] (bool guardOn)
    {
        TruePeakLimiterEngine engine;
        juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (blockSize), 2 };
        engine.prepare (spec);
        engine.setCeilingDb (-1.0f);
        engine.setTpGuard (guardOn);

        juce::AudioBuffer<float> buffer (2, blockSize * 4);
        TestHelpers::fillWithSine (buffer, sampleRate, 997.0, 0.25f); // ~ -12 dBFS, far under ceiling

        juce::dsp::AudioBlock<float> whole (buffer);

        for (int block = 0; block < 4; ++block)
        {
            auto chunk = whole.getSubBlock (static_cast<size_t> (block * blockSize),
                                            static_cast<size_t> (blockSize));
            engine.process (chunk);
        }

        return buffer;
    };

    const auto withGuard = render (true);
    const auto withoutGuard = render (false);

    int mismatches = 0;

    for (int channel = 0; channel < withGuard.getNumChannels(); ++channel)
        for (int sample = 0; sample < withGuard.getNumSamples(); ++sample)
            if (withGuard.getSample (channel, sample) != withoutGuard.getSample (channel, sample))
                ++mismatches;

    CHECK (mismatches == 0);
}
