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
}
