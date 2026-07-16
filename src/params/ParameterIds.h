#pragma once

// Central definition of all AudioProcessorValueTreeState parameter IDs for
// Apotheosis. See docs/architecture.md for the corresponding signal-flow
// diagram.
//
// FROZEN AS OF THE v0.1 PARAMETER LAYOUT:
// Parameter IDs below must NEVER change once shipped - saved sessions and
// presets persist the APVTS state keyed by these string IDs, and renaming or
// removing one would silently break every user's saved state. Ranges,
// defaults, and skew MAY still be refined during voicing/tuning milestones;
// only the IDs themselves are frozen.
namespace ParamIDs
{
    // Input trim into the limiter, applied before true-peak detection.
    inline constexpr auto inputGain = "inputGain";

    // True-peak ceiling (dBTP): the never-exceed target the limiter's gain
    // reduction is computed against.
    inline constexpr auto ceiling = "ceiling";

    // Release time: how quickly gain reduction relaxes back towards unity
    // once the programme material no longer requires it. Attack is not a
    // separate control - it is effectively instantaneous, made non-clipping
    // by the Lookahead delay (see TruePeakLimiterEngine).
    inline constexpr auto release = "release";

    // Lookahead time. Determines how far into the future the limiter can
    // "see" an oncoming true peak before it reaches the output, which is
    // what allows the instantaneous, click-free attack. Directly
    // contributes to the plugin's reported latency, so it is a "setup"
    // parameter: changes take effect at the next prepare() cycle rather
    // than live mid-stream (see TruePeakLimiterEngine::setLookaheadMs).
    inline constexpr auto lookahead = "lookahead";

    // Release curve shape: Exponential (classic one-pole, default) / Linear
    // / Smooth (two-stage cascade). Only shapes the release (increasing-
    // gain) phase - attack is always instantaneous via the lookahead
    // minimum, regardless of this choice.
    inline constexpr auto releaseCurve = "releaseCurve";

    // Output dither: Off (default, bit-identical to no dither) / 16-bit /
    // 24-bit TPDF, added after downsampling at the output word length.
    inline constexpr auto dither = "dither";

    // Clip Mix: blends the transparent gain-reduction limiter path (0%,
    // default) with an alternate tanh soft-clip "clipper" path (100%)
    // applied directly to the lookahead-delayed signal. Both paths (and
    // every blend between them) still pass through the same final hard
    // ceiling clamp, so the never-exceed-ceiling guarantee holds at any
    // Clip Mix setting.
    inline constexpr auto clipMix = "clipMix";

    // v0.2.0 deep-dive additions (docs/design-brief.md) - ADDITIVE ONLY.
    // Every one of the four IDs below defaults to the value that reproduces
    // v1's exact prior behaviour, so old (v1) saved state loading with none
    // of these IDs present falls back to a bit-identical-to-v1 result (see
    // docs/design-brief.md's Guarantee 1/7 and tests/RegressionTests.cpp,
    // tests/StateMigrationTests.cpp). IDs frozen as of v0.2.0, same
    // never-rename contract as the block above.

    // Attack: transient/sustain classifier window, 0-50 ms, default 0 ms.
    // At 0 ms every gain-reduction event is classified "sustained" and
    // routed through the normal Release-governed path exactly as in v1 -
    // NOT a gain-reduction ramp (this is not a compressor retrofit). See
    // TruePeakLimiterEngine::setAttackMs.
    inline constexpr auto attack = "attack";

    // Auto Release: program-dependent release-time modulator, 0-100%,
    // default 0%. At 0% the modulator is a no-op and Release behaves
    // exactly as in v1 for every Release Curve choice. Above 0%, blends in
    // a slow (multi-second) gain-reduction-history-biased modulation of the
    // *effective* Release time fed into the existing curve state machine -
    // this project's own reasoned design, not a copy of any vendor's
    // proprietary IRC/ARC algorithm. See
    // TruePeakLimiterEngine::setAutoReleasePercent.
    inline constexpr auto autoRelease = "autoRelease";

    // Stereo Link: true-peak detector linking amount, 0-100%, default 100%.
    // 100% (default) reproduces v1's only behaviour - max-linked detection
    // across channels - exactly. 0% detects and limits each channel fully
    // independently. See TruePeakLimiterEngine::setStereoLinkPercent.
    inline constexpr auto stereoLink = "stereoLink";

    // Dither Shape: Flat (default, v1's existing plain TPDF, bit-identical)
    // / Shaped (a fixed noise-shaping filter pushing quantisation noise
    // toward the top of the audible band). Only has an audible effect when
    // `dither` (bit depth) is not Off. See
    // TruePeakLimiterEngine::setDitherShape.
    inline constexpr auto ditherShape = "ditherShape";
}
