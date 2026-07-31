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
//
// ADDITIONALLY (binding as of v0.4.0): NEVER extend an existing
// juce::AudioParameterChoice's StringArray. Choice parameters store
// NORMALISED values (index / (numChoices - 1)), so appending an entry
// re-maps every saved session's stored value to a different index - a
// silent, global corruption of user state. Any new discrete behaviour gets
// a NEW parameter ID with its own choice list instead (this is why v0.4.0's
// noise shaping is the new `noiseShaping` parameter rather than a third
// `ditherShape` entry). Guarded by tests/ParameterTests.cpp's
// choice-mapping-freeze test.
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

    // v0.4.0 SOTA DSP additions (the 2026-07 SOTA brief) - ADDITIVE ONLY.
    // Every ID below defaults to the value that reproduces v0.2.0's exact
    // prior output bit-for-bit (the tpGuard guard-delay line excepted - a
    // pure, constant integer delay documented in CHANGELOG.md; see
    // tests/RegressionTests.cpp's golden-fixture comparison). IDs frozen as
    // of v0.4.0, same never-rename contract as the blocks above.

    // Style: envelope voicing of the limiter. Classic (default, index 0) is
    // the literal v0.2.0 code path - rectangular sliding-minimum attack plus
    // the binary transient classifier - kept verbatim behind a top-level
    // dispatch. Transparent/Punchy/Bus/Safe replace the rectangular attack
    // with a cascaded-box FIR smoother (zero overshoot by construction) and
    // the classifier with two concurrent release followers (fast stage
    // depth-capped to the top few dB of gain reduction, slow stage carrying
    // programme GR). See TruePeakLimiterEngine::setLimitStyle and
    // docs/manual.md's per-style table.
    inline constexpr auto limitStyle = "limitStyle";

    // Oversampling: 4x (default - the exact v0.2.0 chain, bit-identical) /
    // 8x / 16x. Latched at prepare() with exactly the same contract as
    // `lookahead`: the setter stores the value, the engine reads it only
    // inside prepare(), and a change takes effect at the next HOST-DRIVEN
    // prepareToPlay() (session reload, sample-rate/buffer-size change,
    // plugin re-enable) - there is deliberately no async re-prepare
    // machinery in this release (see docs/manual.md). Not intended for
    // automation. The engine internally caps the effective factor at high
    // base rates (8x max at >= 96 kHz, 4x max at >= 176.4 kHz); the
    // parameter keeps its stored value.
    inline constexpr auto oversampling = "oversampling";

    // OS Filter: Minimum Phase (default - polyphase allpass IIR halfband,
    // the v0.2.0 filter class, lowest latency) / Linear Phase (FIR
    // equiripple halfband - null-test/mastering-stack friendly, higher
    // latency, pre-ringing). Same prepare()-latch contract as
    // `oversampling` above.
    inline constexpr auto osPhase = "osPhase";

    // True Peak Guard: opt-in output micro-correction stage (default Off).
    // A BS.1770-4-compliant 4x interpolating detector runs on the final
    // base-rate output; while the measured true peak exceeds the Ceiling,
    // the output is ducked by exactly the excess (attack <= 0.1 ms, release
    // 5 ms, only for the duration of the over). The guard's small alignment
    // delay line is ALWAYS in the signal path (a constant +6 base samples
    // below 176.4 kHz, +0 above; pure delay when the guard is Off), so
    // toggling/automating this parameter can never change the reported
    // latency mid-session. See TruePeakLimiterEngine::setTpGuard.
    inline constexpr auto tpGuard = "tpGuard";

    // Noise Shaping: Legacy (default - routes through the untouched v0.2.0
    // dither code, bit-identical including RNG draw order) / Weighted (a
    // psychoacoustically noise-shaped 16/24-bit requantiser: per-channel
    // TPDF plus a 9th-order weighted error-feedback shaper fitted to an
    // inverse audibility curve). Only audible when `dither` is not Off;
    // when Weighted, it supersedes `ditherShape` entirely (precedence
    // documented in docs/manual.md). See
    // TruePeakLimiterEngine::setNoiseShaping.
    inline constexpr auto noiseShaping = "noiseShaping";

    // Delta: audition what the limiter removes (default Off). Output
    // becomes processed-minus-delayed-dry, sample-aligned via the existing
    // lookahead delay line, crossfaded over 10 ms so automation never
    // clicks. Bypasses dither (it is a monitor mode, not a render path) but
    // still passes the final safety clamp. See
    // TruePeakLimiterEngine::setDeltaListen.
    inline constexpr auto deltaListen = "deltaListen";

    // Unity Gain: loudness-matched drive audition (default Off). Applies an
    // output trim of exactly -inputGain dB (via the standard 50 ms smoothed
    // ramp) so Input Gain can be A/B'd at matched loudness. When Delta is
    // also on, Delta wins (it is already a monitor mode). See
    // TruePeakLimiterEngine::setUnityGainMonitor.
    inline constexpr auto unityGainMonitor = "unityGainMonitor";
}
