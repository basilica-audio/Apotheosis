#include "ParameterLayout.h"
#include "ParameterIds.h"

namespace
{
    // True logarithmic (base-10) mapping, so slider/knob travel spends equal
    // space per decade rather than per linear unit - appropriate for both
    // frequency and time-constant parameters, which are perceived
    // logarithmically. Uses juce::mapToLog10/mapFromLog10 rather than
    // NormalisableRange's built-in power-law skew, which only approximates a
    // log curve.
    juce::NormalisableRange<float> makeLogRange (float rangeMin, float rangeMax)
    {
        return juce::NormalisableRange<float> (
            rangeMin,
            rangeMax,
            [] (float start, float end, float normalised)
            { return juce::mapToLog10 (normalised, start, end); },
            [] (float start, float end, float value)
            { return juce::mapFromLog10 (value, start, end); });
    }
}

namespace tbst
{
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        //======================================================================
        // Input Gain: trim into the limiter, applied before true-peak
        // detection - effectively the "how hard are we hitting the ceiling"
        // control.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::inputGain, 1 },
            "Input Gain",
            juce::NormalisableRange<float> (-12.0f, 24.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        //======================================================================
        // Ceiling: the never-exceed true-peak target, dBTP. Default -1.0
        // dBTP is a conventional mastering safety margin against downstream
        // lossy encoding (MP3/AAC) intersample overshoot.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::ceiling, 1 },
            "Ceiling",
            juce::NormalisableRange<float> (-12.0f, 0.0f, 0.01f),
            -1.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dBTP")));

        //======================================================================
        // Release: how quickly gain reduction relaxes back towards unity.
        // Log-mapped, 5 ms (fast, punchy) to 1000 ms (slow, transparent on
        // sustained material), default 50 ms.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::release, 1 },
            "Release",
            makeLogRange (5.0f, 1000.0f),
            50.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));

        //======================================================================
        // Lookahead: 0.1-20 ms, default 5 ms. Directly sets the plugin's
        // reported latency (together with the oversampler's own round-trip
        // latency) - see TruePeakLimiterEngine and docs/architecture.md.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::lookahead, 1 },
            "Lookahead",
            juce::NormalisableRange<float> (0.1f, 20.0f, 0.01f),
            5.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));

        //======================================================================
        // Release Curve: shapes the release (increasing-gain) phase only -
        // attack remains instantaneous via the lookahead minimum regardless
        // of this choice. Default index 0 (Exponential) matches the
        // original v0.1 one-pole behaviour.
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParamIDs::releaseCurve, 1 },
            "Release Curve",
            juce::StringArray { "Exponential", "Linear", "Smooth" },
            0));

        //======================================================================
        // Dither: TPDF dither added after downsampling, at the output word
        // length. Default index 0 (Off) is bit-identical to the pre-dither
        // signal path.
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParamIDs::dither, 1 },
            "Dither",
            juce::StringArray { "Off", "16-bit", "24-bit" },
            0));

        //======================================================================
        // Clip Mix: blends the transparent limiter path (0%, default) with
        // an alternate tanh soft-clip "clipper" path (100%). See
        // ParamIDs::clipMix and TruePeakLimiterEngine::process().
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::clipMix, 1 },
            "Clip Mix",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%")));

        //======================================================================
        // v0.2.0 deep-dive additions (docs/design-brief.md). See
        // ParameterIds.h for the full per-control rationale/sourcing - every
        // default below reproduces v1's exact prior behaviour.
        //======================================================================

        // Attack: 0-50 ms, linear (not log-mapped, unlike Release/Lookahead -
        // see design-brief.md's module spec for why), default 0 ms.
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::attack, 1 },
            "Attack",
            juce::NormalisableRange<float> (0.0f, 50.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("ms")));

        // Auto Release: 0-100%, default 0% (no-op).
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::autoRelease, 1 },
            "Auto Release",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%")));

        // Stereo Link: 0-100%, default 100% (v1's only behaviour - fully
        // max-linked detection).
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ParamIDs::stereoLink, 1 },
            "Stereo Link",
            juce::NormalisableRange<float> (0.0f, 100.0f, 0.01f),
            100.0f,
            juce::AudioParameterFloatAttributes().withLabel ("%")));

        // Dither Shape: Flat (default index 0, bit-identical to v1's plain
        // TPDF) / Shaped (index 1). Only audible when Dither (bit depth)
        // is not Off.
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParamIDs::ditherShape, 1 },
            "Dither Shape",
            juce::StringArray { "Flat", "Shaped" },
            0));

        return layout;
    }
}
