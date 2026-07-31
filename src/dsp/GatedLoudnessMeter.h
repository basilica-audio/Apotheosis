#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>

// v0.4.0 (F5, brief section 3.5): BS.1770-4-compliant gated Integrated
// Loudness plus EBU Tech 3342 Loudness Range (LRA), fed one summed
// K-weighted squared sample at a time from the engine's existing
// per-sample K-weighting filters (TruePeakLimiterEngine::processChunk()'s
// LUFS section - channel weights G are 1.0 for the mono/stereo layouts
// this plugin supports, so the caller sums plain squared filter outputs).
//
// Structure (libebur128's histogram approach - O(1) memory, zero
// allocation after prepare()):
//
// - K-weighted mean-square accumulates in 100 ms sub-blocks (fixed sample
//   count latched at prepare()). Every completed sub-block finishes a
//   400 ms gating block formed from the last 4 sub-blocks (75 % overlap,
//   BS.1770-4) with loudness L_j = -0.691 + 10 log10(power).
// - Blocks with L_j > -70 LUFS (absolute gate) land in a fixed histogram
//   (0.1 LU bins, -70..+5 LUFS, 751 bins; per-bin block count + power
//   sum). Integrated is a two-pass scan over the histogram: mean of all
//   stored blocks -> relative threshold = mean - 10 LU -> re-mean of the
//   blocks at/above it. Runs at 10 Hz on the audio thread: 751 bins x 2
//   passes per 100 ms is negligible.
// - LRA: a second histogram of short-term (3 s) loudness sampled every
//   100 ms (once 3 s of signal exists), absolute gate -70 LUFS, relative
//   gate -20 LU below the gated power mean, LRA = 95th - 10th percentile
//   (EBU Tech 3342).
//
// This replaces v0.2.0's documented-deviation Integrated readout (absolute
// gate only, per-processed-block evaluation) - a metering-only change, no
// audio impact (CHANGELOG "Changed - metering only").
class GatedLoudnessMeter
{
public:
    // -70 LUFS .. +5 LUFS in 0.1 LU steps.
    static constexpr int numBins = 751;
    static constexpr double absoluteGateLufs = -70.0;
    static constexpr double binWidthLu = 0.1;

    void prepare (double sampleRate)
    {
        subBlockLengthSamples = juce::jmax (1, juce::roundToInt (0.1 * sampleRate));
        reset();
    }

    void reset() noexcept
    {
        subBlockPowerSum = 0.0;
        subBlockSampleCount = 0;

        for (auto& power : recentSubBlockPowers)
            power = 0.0;

        recentSubBlockCount = 0;
        recentWritePos = 0;

        gatingCounts.fill (0);
        gatingPowers.fill (0.0);
        gatingTotalCount = 0;

        shortTermCounts.fill (0);
        shortTermPowers.fill (0.0);
        shortTermTotalCount = 0;

        integratedLufs = -100.0f;
        loudnessRangeLu = 0.0f;
    }

    // Pushes one sample's summed K-weighted squared value (sum over
    // channels of the squared K-filter outputs). No allocation; O(1)
    // amortised - the histogram scans run once per 100 ms sub-block.
    void pushSamplePower (double weightedPower) noexcept
    {
        subBlockPowerSum += weightedPower;

        if (++subBlockSampleCount < subBlockLengthSamples)
            return;

        const auto subBlockMeanPower = subBlockPowerSum / static_cast<double> (subBlockLengthSamples);
        subBlockPowerSum = 0.0;
        subBlockSampleCount = 0;

        recentSubBlockPowers[static_cast<size_t> (recentWritePos)] = subBlockMeanPower;
        recentWritePos = (recentWritePos + 1) % maxRecentSubBlocks;
        recentSubBlockCount = juce::jmin (recentSubBlockCount + 1, maxRecentSubBlocks);

        // 400 ms gating block = mean of the last 4 sub-blocks (75 %
        // overlap: one new gating block per 100 ms sub-block).
        if (recentSubBlockCount >= gatingSubBlocks)
        {
            double gatingPower = 0.0;

            for (int i = 1; i <= gatingSubBlocks; ++i)
                gatingPower += recentSubBlockPowers[static_cast<size_t> ((recentWritePos - i + maxRecentSubBlocks) % maxRecentSubBlocks)];

            gatingPower /= static_cast<double> (gatingSubBlocks);

            const auto blockLoudness = loudnessFromPower (gatingPower);

            if (blockLoudness > absoluteGateLufs)
            {
                const auto bin = binForLoudness (blockLoudness);
                ++gatingCounts[static_cast<size_t> (bin)];
                gatingPowers[static_cast<size_t> (bin)] += gatingPower;
                ++gatingTotalCount;
            }

            integratedLufs = computeIntegrated();
        }

        // Short-term (3 s) loudness sampled every 100 ms once the window
        // is fully primed - feeds the LRA histogram.
        if (recentSubBlockCount >= maxRecentSubBlocks)
        {
            double shortTermPower = 0.0;

            for (const auto& power : recentSubBlockPowers)
                shortTermPower += power;

            shortTermPower /= static_cast<double> (maxRecentSubBlocks);

            const auto shortTermLoudness = loudnessFromPower (shortTermPower);

            if (shortTermLoudness > absoluteGateLufs)
            {
                const auto bin = binForLoudness (shortTermLoudness);
                ++shortTermCounts[static_cast<size_t> (bin)];
                shortTermPowers[static_cast<size_t> (bin)] += shortTermPower;
                ++shortTermTotalCount;
            }

            loudnessRangeLu = computeLoudnessRange();
        }
    }

    // -100.0f until the first gating block passes the absolute gate.
    float getIntegratedLufs() const noexcept { return integratedLufs; }

    // 0.0f until at least one short-term value passed the gates.
    float getLoudnessRangeLu() const noexcept { return loudnessRangeLu; }

private:
    static constexpr int gatingSubBlocks = 4; // 400 ms / 100 ms
    static constexpr int maxRecentSubBlocks = 30; // 3 s / 100 ms

    static double loudnessFromPower (double power) noexcept
    {
        return power > 0.0 ? -0.691 + 10.0 * std::log10 (power) : -200.0;
    }

    static int binForLoudness (double loudness) noexcept
    {
        return juce::jlimit (0, numBins - 1,
                             static_cast<int> ((loudness - absoluteGateLufs) / binWidthLu));
    }

    static double binCenterLoudness (int bin) noexcept
    {
        return absoluteGateLufs + (static_cast<double> (bin) + 0.5) * binWidthLu;
    }

    // Two-pass over the gating histogram: mean power of all stored
    // (absolute-gated) blocks -> relative threshold = that mean's loudness
    // - 10 LU -> mean power of the blocks in bins at/above the threshold.
    float computeIntegrated() const noexcept
    {
        if (gatingTotalCount == 0)
            return -100.0f;

        double totalPower = 0.0;

        for (const auto& power : gatingPowers)
            totalPower += power;

        const auto ungatedMean = totalPower / static_cast<double> (gatingTotalCount);
        const auto relativeThreshold = loudnessFromPower (ungatedMean) - 10.0;

        double gatedPower = 0.0;
        juce::int64 gatedCount = 0;

        for (int bin = 0; bin < numBins; ++bin)
        {
            if (binCenterLoudness (bin) >= relativeThreshold && gatingCounts[static_cast<size_t> (bin)] > 0)
            {
                gatedPower += gatingPowers[static_cast<size_t> (bin)];
                gatedCount += gatingCounts[static_cast<size_t> (bin)];
            }
        }

        if (gatedCount == 0)
            return -100.0f;

        return static_cast<float> (loudnessFromPower (gatedPower / static_cast<double> (gatedCount)));
    }

    // EBU Tech 3342: relative gate -20 LU below the (absolute-gated)
    // short-term power mean; LRA = 95th - 10th percentile of the
    // remaining short-term loudness distribution.
    float computeLoudnessRange() const noexcept
    {
        if (shortTermTotalCount == 0)
            return 0.0f;

        double totalPower = 0.0;

        for (const auto& power : shortTermPowers)
            totalPower += power;

        const auto ungatedMean = totalPower / static_cast<double> (shortTermTotalCount);
        const auto relativeThreshold = loudnessFromPower (ungatedMean) - 20.0;

        const auto firstBin = binForLoudness (relativeThreshold);
        juce::int64 gatedCount = 0;

        for (int bin = firstBin; bin < numBins; ++bin)
            gatedCount += shortTermCounts[static_cast<size_t> (bin)];

        if (gatedCount < 2)
            return 0.0f;

        const auto percentileLoudness = [this, firstBin, gatedCount] (double fraction) noexcept
        {
            const auto target = static_cast<juce::int64> (fraction * static_cast<double> (gatedCount - 1));
            juce::int64 cumulative = 0;

            for (int bin = firstBin; bin < numBins; ++bin)
            {
                cumulative += shortTermCounts[static_cast<size_t> (bin)];

                if (cumulative > target)
                    return binCenterLoudness (bin);
            }

            return binCenterLoudness (numBins - 1);
        };

        const auto low = percentileLoudness (0.10);
        const auto high = percentileLoudness (0.95);

        return static_cast<float> (juce::jmax (0.0, high - low));
    }

    int subBlockLengthSamples = 4800;
    double subBlockPowerSum = 0.0;
    int subBlockSampleCount = 0;

    std::array<double, maxRecentSubBlocks> recentSubBlockPowers {};
    int recentSubBlockCount = 0;
    int recentWritePos = 0;

    std::array<juce::int64, numBins> gatingCounts {};
    std::array<double, numBins> gatingPowers {};
    juce::int64 gatingTotalCount = 0;

    std::array<juce::int64, numBins> shortTermCounts {};
    std::array<double, numBins> shortTermPowers {};
    juce::int64 shortTermTotalCount = 0;

    float integratedLufs = -100.0f;
    float loudnessRangeLu = 0.0f;
};
