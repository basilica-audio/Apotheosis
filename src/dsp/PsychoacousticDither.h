#pragma once

#include <juce_core/juce_core.h>

#include <cmath>

// v0.4.0 (F4, brief section 3.4): psychoacoustic noise-shaped dither - a
// true TPDF-dithered REQUANTISER with a 9th-order error-feedback noise
// shaper, engaged only when the "Noise Shaping" parameter is Weighted AND
// "Dither" is not Off (see TruePeakLimiterEngine::processChunk()'s dither
// section; Legacy routes through the untouched v0.2.0 code instead).
//
// Topology (the classic error-feedback form - Lipshitz/Wannamaker/
// Vanderkooy, "Minimally audible noise shaping", JAES 39(11), 1991):
//
//   y[n] = Q( x[n] + sum_{k=1..9} c_k * e[n-k] )
//   e[n] = y[n] - ( x[n] + sum_{k=1..9} c_k * e[n-k] )
//
// where Q is a TPDF-dithered quantiser to the output word-length grid
// (16- or 24-bit). The total error y - x then has the noise transfer
// function NTF(z) = 1 + C(z), C(z) = sum c_k z^-k: white dithered-
// quantiser error is spectrally redistributed away from the ear's most
// sensitive region into the ultrasonics, without changing its statistics.
//
// Unlike the Legacy v0.2.0 path (which only ADDS TPDF noise, leaving the
// actual truncation to the downstream consumer), this path must quantise
// in-loop - error feedback cannot shape a quantiser it does not contain.
// The engine re-clamps to the ceiling after this stage (the existing #9
// fix), so the never-exceed-Ceiling guarantee survives the shaped error's
// larger peak excursion.
//
// Per-channel independent RNG streams (one juce::Random per channel,
// seeded distinctly at prepare()) fix the v0.2.0 shared-stream
// inelegance for this path only - the Legacy path keeps the shared RNG
// and its exact draw order, bit-identical to the committed v0.2.0 golden
// dither fixture (tests/RegressionTests.cpp T0, tests/
// StereoLinkDitherShapeTests.cpp T12).
//
//==============================================================================
// COEFFICIENT PROVENANCE (project-owned - NOT copied from POW-r/MBIT+ or
// any other proprietary curve; those are unpublished trade secrets):
//
// Generated offline by a least-squares fit of |NTF| = |1 + C(z)| to an
// inverse absolute-threshold-of-hearing audibility weighting (the
// "minimally audible" criterion of the JAES 1991 paper above, i.e. a
// modified-E/F-weighted fit), jointly over fs = 44.1 kHz and 48 kHz with
// equal weight. The weighting is Terhardt's analytic threshold-in-quiet
// approximation (E. Terhardt, "Calculating virtual pitch", Hearing
// Research 1, 1979 - the same curve MPEG psychoacoustic models start
// from):
//
//   Tq(f) [dB] = 3.64 (f/kHz)^-0.8
//                - 6.5 exp(-0.6 ((f/kHz) - 3.3)^2) + 1e-3 (f/kHz)^4
//
// clamped to +55 dB above the 3-4 kHz floor (the clamp bounds the NTF's
// ultrasonic boost, keeping the peak shaped-error excursion within a few
// tens of LSB), W(f) = 10^(-Tq/10). The weighted LSQ reduces to a
// Toeplitz normal-equation solve on the weighting's autocorrelation
// r(m) = integral W(w) cos(m w) dw:  R c = -r.
//
// Fit results for THIS table (measured by the generation script below,
// and asserted end-to-end against a real render by T12 in
// tests/StereoLinkDitherShapeTests.cpp with an independent test-side
// implementation of the same weighting formula):
//
//   F-weighted perceived-floor improvement vs the Legacy flat-TPDF path
//   (including the 10log10(1.5) = 1.76 dB variance penalty of a true
//   dithered requantiser vs noise-addition-only):
//       +20.0 dB @ 48 kHz, +18.4 dB @ 44.1 kHz
//   (binding floor per the brief: >= 15 dB; stretch target 18+ met)
//   Unweighted NTF gain: +24.9 dB (the shaped error's total power rises;
//   nearly all of it sits above 16 kHz). Peak NTF gain: +32.6 dB.
//
// At 88.2 kHz and above the same table is used - shaping is defined at
// the output rate and this curve is optimized for 44.1/48 kHz delivery
// (documented in docs/manual.md).
//
// Generation script (python3 + numpy 2.x), runnable as-is:
//
//   import numpy as np
//   def weighting(f, clamp=55.0):
//       fk = np.maximum(f, 10.0) / 1000.0
//       tq = 3.64*fk**-0.8 - 6.5*np.exp(-0.6*(fk-3.3)**2) + 1e-3*fk**4
//       return 10.0 ** (-np.clip(tq, None, clamp) / 10.0)
//   def autocorr(fs, order, N=1<<16):
//       f = np.linspace(0.0, fs/2, N); om = 2*np.pi*f/fs
//       w = weighting(f)
//       return np.array([np.trapezoid(w*np.cos(m*om), om)
//                        for m in range(order+1)])
//   order = 9
//   r48 = autocorr(48000.0, order); r44 = autocorr(44100.0, order)
//   r = r48/r48[0] + r44/r44[0]           # joint fit, equal weight
//   R = np.array([[r[abs(j-k)] for k in range(order)]
//                 for j in range(order)])
//   c = np.linalg.solve(R, -r[1:order+1]) # minimise |1 + C(z)|^2 W
//   print(c)
//==============================================================================
class PsychoacousticDither
{
public:
    static constexpr int maxChannels = 2;
    static constexpr int order = 9;

    // c_1..c_9 of y[n] = Q(x[n] + sum c_k e[n-k]) - see the provenance
    // block above. NTF(z) = 1 + sum c_k z^-k.
    static constexpr float coefficients[order] = {
        -3.647335059f, // c1
        +7.205773464f, // c2
        -9.636145397f, // c3
        +9.447642822f, // c4
        -6.818725649f, // c5
        +3.516342369f, // c6
        -1.110417794f, // c7
        +0.112202296f, // c8
        +0.048878932f, // c9
    };

    // Distinctly-seeded independent per-channel RNG streams: two plugin
    // instances (or two channels) must never emit correlated dither
    // noise. Also clears the error history.
    void prepare() noexcept
    {
        for (int channel = 0; channel < maxChannels; ++channel)
        {
            // Golden-ratio stride (Weyl/splitmix constant) decorrelates the
            // per-channel streams even if the two time sources coincide.
            constexpr juce::uint64 goldenGamma = 0x9E3779B97F4A7C15ULL;
            const auto mixed = static_cast<juce::uint64> (juce::Time::currentTimeMillis())
                                + static_cast<juce::uint64> (juce::Time::getHighResolutionTicks())
                                    * static_cast<juce::uint64> (channel + 1)
                                + static_cast<juce::uint64> (channel + 1) * goldenGamma;
            rng[channel].setSeed (static_cast<juce::int64> (mixed));
        }

        reset();
    }

    // Clears the error-feedback history only (keeps the RNG streams
    // running - reset() must not make two resets emit identical noise).
    void reset() noexcept
    {
        for (auto& channelHistory : errorHistory)
            for (auto& e : channelHistory)
                e = 0.0;

        historyWritePos = 0;
    }

#if APOTHEOSIS_TESTING
    // Deterministic seeding for T12's chi-square TPDF-distribution test
    // (Tests binary only - production streams must stay unpredictable and
    // uncorrelated across instances). Same pattern as
    // TruePeakLimiterEngine::setDitherSeedForTests().
    void setSeedsForTests (juce::int64 seed) noexcept
    {
        for (int channel = 0; channel < maxChannels; ++channel)
            rng[channel].setSeed (seed + (channel + 1) * 7919);
    }
#endif

    // One TPDF draw in [-1, 1] (2 LSB peak-to-peak once scaled by the
    // quantisation step) from the given channel's independent stream.
    // Public so T12's chi-square distribution test can sample the exact
    // generator the audio path uses.
    float nextTpdf (int channel) noexcept
    {
        auto& r = rng[juce::jlimit (0, maxChannels - 1, channel)];

        // The two draws MUST be sequenced explicitly. Written as
        // `r.nextFloat() - r.nextFloat()` the operand evaluation order is
        // unspecified, and because each call advances the RNG the two
        // orders yield exactly negated noise - which made the dither output
        // compiler-dependent (Clang and MSVC disagreed, breaking the
        // seeded-fixture bit-exactness contract across platforms).
        const auto first = r.nextFloat();
        const auto second = r.nextFloat();
        return first - second;
    }

    // Requantises one sample to the grid of `quantisationStep` (the output
    // word-length LSB, e.g. 2^-15 for 16-bit) with TPDF dither and the
    // 9th-order weighted error feedback. Call advanceHistory() once per
    // sample frame, AFTER every channel has been processed.
    float processSample (int channel, float inputSample, float quantisationStep) noexcept
    {
        const auto ch = juce::jlimit (0, maxChannels - 1, channel);

        // x[n] + sum c_k e[n-k] - the error history is per channel, in
        // double: the feedback loop integrates tiny differences for the
        // whole session, exactly the situation the brief's "running sums
        // in double" rule exists for.
        auto corrected = static_cast<double> (inputSample);

        for (int k = 0; k < order; ++k)
        {
            const auto index = (historyWritePos - 1 - k + 2 * order) % order;
            corrected += static_cast<double> (coefficients[k]) * errorHistory[ch][index];
        }

        // TPDF-dithered quantisation to the output grid.
        const auto dither = static_cast<double> (nextTpdf (ch)) * static_cast<double> (quantisationStep);
        const auto quantised = std::round ((corrected + dither) / static_cast<double> (quantisationStep))
                                * static_cast<double> (quantisationStep);

        errorHistory[ch][historyWritePos] = quantised - corrected;

        return static_cast<float> (quantised);
    }

    // Advances the shared per-frame history write position - once per
    // sample frame, after all channels (both channels share the position,
    // not the history values, so channel counts of 1 and 2 both work).
    void advanceHistory() noexcept
    {
        historyWritePos = (historyWritePos + 1) % order;
    }

private:
    juce::Random rng[maxChannels];
    double errorHistory[maxChannels][order] = {};
    int historyWritePos = 0;
};
