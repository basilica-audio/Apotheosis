#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// v0.4.0 style-engine envelope stages (brief section 3.1), header-only so
// tests/StyleEngineTests.cpp can drive them directly against double-
// precision reference recurrences. Both classes are real-time safe after
// prepare(): fixed-capacity storage, no allocation in process().
//
// From-scratch implementations of the published cascaded-box / dual-release
// limiter topology (Signalsmith Audio's limiter articles; Giannoulis/
// Massberg/Reiss JAES 2012) - not vendored third-party code.

//==============================================================================
// A single moving average ("box" filter) over the most recent `length`
// inputs, with the running sum accumulated in double plus a periodic exact
// re-summation every 2^14 samples: float running sums drift audibly on gain
// signals, and even double sums are re-anchored so error can never
// accumulate over a session (brief section 3.1 / RTP section 3.1).
class BoxFilter
{
public:
    void prepare (int maxLengthSamples)
    {
        capacity = std::max (2, maxLengthSamples + 1);
        buffer.assign (static_cast<size_t> (capacity), 1.0f);
        length = std::min (length, capacity - 1);
        resetTo (1.0f);
    }

    // Changes the window length without allocating. The running sum is
    // recomputed exactly over the new window, so this is safe to call
    // between blocks (attack-parameter or style changes); cost is O(length),
    // bounded by the capacity fixed at prepare().
    void setLength (int newLengthSamples) noexcept
    {
        const auto clamped = std::clamp (newLengthSamples, 1, capacity - 1);

        if (clamped == length)
            return;

        length = clamped;
        recomputeSumExactly();
    }

    int getLength() const noexcept { return length; }

    // Fills the entire history with `value` - used to re-seed the smoother
    // from the current gain on a style switch so there is no snap.
    void resetTo (float value) noexcept
    {
        std::fill (buffer.begin(), buffer.end(), value);
        writePos = 0;
        runningSum = static_cast<double> (value) * static_cast<double> (length);
        samplesUntilResync = resyncIntervalSamples;
    }

    float process (float input) noexcept
    {
        buffer[static_cast<size_t> (writePos)] = input;
        runningSum += static_cast<double> (input);

        // The sample that just left the window sits `length` positions
        // behind the one just written.
        const auto outgoingIndex = (writePos - length + capacity) % capacity;
        runningSum -= static_cast<double> (buffer[static_cast<size_t> (outgoingIndex)]);

        writePos = (writePos + 1) % capacity;

        if (--samplesUntilResync <= 0)
            recomputeSumExactly();

        return static_cast<float> (runningSum / static_cast<double> (length));
    }

private:
    static constexpr int resyncIntervalSamples = 1 << 14;

    void recomputeSumExactly() noexcept
    {
        double sum = 0.0;

        for (int i = 0; i < length; ++i)
        {
            const auto index = (writePos - 1 - i + 2 * capacity) % capacity;
            sum += static_cast<double> (buffer[static_cast<size_t> (index)]);
        }

        runningSum = sum;
        samplesUntilResync = resyncIntervalSamples;
    }

    std::vector<float> buffer;
    int capacity = 2;
    int length = 1;
    int writePos = 0;
    double runningSum = 1.0;
    int samplesUntilResync = resyncIntervalSamples;
};

//==============================================================================
// Dual concurrent release stages (brief section 3.1 / RTP section 2.4):
// replaces the v0.2.0 binary transient classifier in the non-Classic
// styles. The smoothed gain demand s[n] is decomposed multiplicatively into
//
//   sTop[n]  = max(s[n], capLin)   - the top D dB of gain reduction
//   sRest[n] = s[n] / sTop[n]      - everything deeper than D dB
//
// (so sTop * sRest == s exactly). A FAST follower tracks sTop with tau_f
// and a SLOW follower tracks sRest with tau_s; the combined gain is
//
//   r[n] = min(s[n], rFastCapped[n] * rSlow[n])
//
// - the brief's dual-stage min-combiner, with the fast stage depth-capped
// to the top D dB by construction of sTop. Both followers are SYMMETRIC
// one-poles (k = 1/(tau * fsOs + 1), both directions): the instantaneous
// attack is provided exclusively by the outer min() against s[n], which
// also keeps r <= s at all times so the release can never undo the
// smoother's zero-overshoot guarantee. The symmetric integration is the
// point of the dual topology: a millisecond transient barely dents the
// slow follower (its tau spans hundreds of ms of history), so after the
// transient the gain snaps back as soon as the fast stage recovers -
// while sustained programme depth accumulates into the slow follower and
// releases slowly. Transient snap without pumping.
//
// State is double precision: the followers integrate over hundreds of
// milliseconds at the oversampled rate and float states visibly quantise
// the tail.
class DualStageRelease
{
public:
    // k = 1/(tau*fs + 1) per the brief's release recurrence. fsOs is the
    // OVERSAMPLED rate the stage runs at.
    static double coefficientForTau (double tauSeconds, double fsOs) noexcept
    {
        return 1.0 / (tauSeconds * fsOs + 1.0);
    }

    void setFastCoefficient (double newKFast) noexcept { kFast = newKFast; }
    void setSlowCoefficient (double newKSlow) noexcept { kSlow = newKSlow; }

    void setFastCapDb (double capDb) noexcept
    {
        capLin = std::pow (10.0, -std::abs (capDb) / 20.0);
    }

    double getFastCapLinear() const noexcept { return capLin; }

    // Re-seeds both followers so that their combined output equals `gain`
    // exactly - used on style switches (no snap) and reset().
    void seed (double gain) noexcept
    {
        const auto clamped = std::clamp (gain, 1.0e-6, 1.0);
        fastState = std::max (clamped, capLin);
        slowState = clamped / fastState;
    }

    float process (float smoothedGain) noexcept
    {
        const auto s = static_cast<double> (smoothedGain);
        const auto sTop = std::max (s, capLin);
        const auto sRest = s / std::max (sTop, 1.0e-12);

        // Symmetric one-pole integration (see the class comment: the
        // instantaneous attack lives in the outer min(), not here).
        fastState += (sTop - fastState) * kFast;
        slowState += (sRest - slowState) * kSlow;

        return static_cast<float> (std::min (s, fastState * slowState));
    }

private:
    double kFast = 0.0;
    double kSlow = 0.0;
    double capLin = std::pow (10.0, -2.0 / 20.0);
    double fastState = 1.0;
    double slowState = 1.0;
};
