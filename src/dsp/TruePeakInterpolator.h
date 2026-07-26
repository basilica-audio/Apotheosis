#pragma once

#include <algorithm>
#include <array>
#include <cmath>

// ITU-R BS.1770-4 Annex 2 true-peak interpolating detector (v0.4.0, brief
// section 3.2). Header-only so tests/TruePeakVerificationTests.cpp can
// exercise it directly, without a full engine.
//
// The detector estimates inter-sample peaks by 4x-oversampling the signal
// through the Recommendation's reference 48-tap, 4-phase polyphase FIR
// (12 taps per phase - the exact published coefficient table below) and
// taking the maximum absolute interpolated value. The +/-12.04 dB
// integer-headroom attenuation pair the Recommendation describes is
// deliberately skipped: it exists only for fixed-point arithmetic and is a
// no-op in this float implementation.
//
// Rate policy (mirrors libebur128): the full oversampling ratio is only
// needed at base rates whose Nyquist is close to the audible band.
//   < 88.2 kHz      -> 4x (all four phases)
//   88.2 - 96 kHz   -> 2x: the SAME 48-coefficient 4-phase bank, emitting
//                      only phases 0 and 2 - one prototype, one group
//                      delay, no second filter design (binding, brief
//                      section 3.2)
//   >= 176.4 kHz    -> 1x pass-through (max of |sample|), zero group delay
//
// Group delay: (48 - 1) / (2 * 4) = 5.875 BASE-RATE samples wherever the
// bank runs - identical at 44.1 k and 48 k, and (because the 2x tier reuses
// the same bank) identical at 88.2-96 k too. The engine's true-peak-guard
// alignment delay is therefore the per-rate-policy-tier CONSTANT
// ceil(5.875) = 6 base samples below 176.4 kHz and 0 at/above - explicitly
// NOT scaled with the sample rate (brief section 3.2's table).
class TruePeakInterpolator
{
public:
    enum class RatePolicy
    {
        fourPhase, // 4x: phases 0-3
        twoPhase, // 2x: phases 0 and 2 of the same bank
        passThrough, // 1x: |sample| max, no filtering
    };

    static constexpr int numPhases = 4;
    static constexpr int numTapsPerPhase = 12;
    static constexpr int maxChannels = 2;

    // The exact 48 coefficients from BS.1770-4 Annex 2 (12 rows x 4 phase
    // columns, transcribed per phase). Standards data, not creative
    // expression. Phase symmetry - Phase3 = reverse(Phase0), Phase2 =
    // reverse(Phase1), a linear-phase prototype split into four branches -
    // is asserted at compile time below.
    static constexpr std::array<std::array<float, numTapsPerPhase>, numPhases> coefficients { {
        { 0.0017089843750f, 0.0109863281250f, -0.0196533203125f, 0.0332031250000f,
          -0.0594482421875f, 0.1373291015625f, 0.9721679687500f, -0.1022949218750f,
          0.0476074218750f, -0.0266113281250f, 0.0148925781250f, -0.0083007812500f },
        { -0.0291748046875f, 0.0292968750000f, -0.0517578125000f, 0.0891113281250f,
          -0.1665039062500f, 0.4650878906250f, 0.7797851562500f, -0.2003173828125f,
          0.1015625000000f, -0.0582275390625f, 0.0330810546875f, -0.0189208984375f },
        { -0.0189208984375f, 0.0330810546875f, -0.0582275390625f, 0.1015625000000f,
          -0.2003173828125f, 0.7797851562500f, 0.4650878906250f, -0.1665039062500f,
          0.0891113281250f, -0.0517578125000f, 0.0292968750000f, -0.0291748046875f },
        { -0.0083007812500f, 0.0148925781250f, -0.0266113281250f, 0.0476074218750f,
          -0.1022949218750f, 0.9721679687500f, 0.1373291015625f, -0.0594482421875f,
          0.0332031250000f, -0.0196533203125f, 0.0109863281250f, 0.0017089843750f },
    } };

    static constexpr RatePolicy policyForSampleRate (double sampleRate) noexcept
    {
        if (sampleRate < 88200.0)
            return RatePolicy::fourPhase;

        if (sampleRate < 176400.0)
            return RatePolicy::twoPhase;

        return RatePolicy::passThrough;
    }

    // The engine's always-in-path guard-delay constant per rate-policy tier
    // (see the class comment): 6 base samples below 176.4 kHz, 0 at/above.
    static constexpr int guardDelaySamplesForSampleRate (double sampleRate) noexcept
    {
        return policyForSampleRate (sampleRate) == RatePolicy::passThrough ? 0 : 6;
    }

    void prepare (double sampleRate) noexcept
    {
        policy = policyForSampleRate (sampleRate);
        reset();
    }

    void reset() noexcept
    {
        for (auto& channelHistory : history)
            channelHistory.fill (0.0f);

        writePos = 0;
    }

    // Feeds one base-rate sample for `channel` and returns the largest
    // absolute interpolated magnitude the new sample completes - i.e. the
    // detector's true-peak envelope, delayed by the bank's 5.875-sample
    // group delay. Call for every channel of a frame before moving to the
    // next frame; advanceWritePosition() advances the shared write index
    // once per frame (both channels share one time base, exactly like the
    // engine's lookahead structures).
    float processSample (int channel, float sample) noexcept
    {
        if (policy == RatePolicy::passThrough)
            return std::abs (sample);

        auto& channelHistory = history[static_cast<size_t> (channel)];
        channelHistory[static_cast<size_t> (writePos)] = sample;

        float peak = 0.0f;

        const int phaseStep = policy == RatePolicy::twoPhase ? 2 : 1;

        for (int phase = 0; phase < numPhases; phase += phaseStep)
        {
            float accumulator = 0.0f;

            for (int tap = 0; tap < numTapsPerPhase; ++tap)
            {
                const auto index = (writePos - tap + numTapsPerPhase) % numTapsPerPhase;
                accumulator += coefficients[static_cast<size_t> (phase)][static_cast<size_t> (tap)]
                                * channelHistory[static_cast<size_t> (index)];
            }

            peak = std::max (peak, std::abs (accumulator));
        }

        return peak;
    }

    void advanceWritePosition() noexcept
    {
        writePos = (writePos + 1) % numTapsPerPhase;
    }

private:
    RatePolicy policy = RatePolicy::fourPhase;
    std::array<std::array<float, numTapsPerPhase>, maxChannels> history {};
    int writePos = 0;
};

// Compile-time phase symmetry check (brief section 3.2): the table is a
// linear-phase prototype, so Phase3 must be Phase0 reversed and Phase2 must
// be Phase1 reversed. A transcription error in the table above would break
// these.
namespace truepeak_detail
{
    constexpr bool phasesAreSymmetric()
    {
        // Written as ordered comparisons (not ==) to stay clean under
        // -Wfloat-equal; identical literals compare exactly either way.
        const auto differs = [] (float a, float b) constexpr { return a < b || a > b; };

        for (int tap = 0; tap < TruePeakInterpolator::numTapsPerPhase; ++tap)
        {
            const auto mirrored = static_cast<size_t> (TruePeakInterpolator::numTapsPerPhase - 1 - tap);

            if (differs (TruePeakInterpolator::coefficients[3][static_cast<size_t> (tap)],
                         TruePeakInterpolator::coefficients[0][mirrored]))
                return false;

            if (differs (TruePeakInterpolator::coefficients[2][static_cast<size_t> (tap)],
                         TruePeakInterpolator::coefficients[1][mirrored]))
                return false;
        }

        return true;
    }

    static_assert (phasesAreSymmetric(),
                   "BS.1770-4 coefficient table transcription error: phases must be pairwise mirror-symmetric");

    static_assert (TruePeakInterpolator::guardDelaySamplesForSampleRate (44100.0) == 6);
    static_assert (TruePeakInterpolator::guardDelaySamplesForSampleRate (48000.0) == 6);
    static_assert (TruePeakInterpolator::guardDelaySamplesForSampleRate (96000.0) == 6);
    static_assert (TruePeakInterpolator::guardDelaySamplesForSampleRate (192000.0) == 0);
}
