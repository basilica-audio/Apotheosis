#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "dsp/TruePeakLimiterEngine.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <limits>
#include <vector>

// v0.2.0 deep-dive additions (docs/design-brief.md) - the suite's "Guarantees
// & tests" section. This file covers Guarantee 1 (bit-identical defaults),
// Guarantee 6 (ceiling guarantee across every new-parameter extreme), and
// Guarantee 8 (NaN/Inf robustness with the new controls at their extremes).
// Guarantee 9 (real-time safety) is verified primarily *by design* - see its
// TEST_CASE below - the same pattern nave's PresetManagerTests.cpp uses for
// its own real-time-safety guarantee.
//
// The strongest evidence for Guarantee 1 is actually every OTHER still-green
// test in this suite: tests/LimiterTests.cpp, tests/DspFeatureTests.cpp,
// tests/MeteringTests.cpp, etc. were all written against v1's exact expected
// numeric behaviour (e.g. "Release Curve: default (Exponential) matches the
// original v0.1 one-pole behaviour", "Clip Mix at 0% is bit-identical to the
// pure gain-reduction limiter path") and every one of them still passes
// unmodified after the v0.2.0 per-channel engine rewrite - see this PR's
// description / CHANGELOG.md for the full local-verify test run. This file
// adds a direct, explicit A-vs-B comparison on top of that as belt-and-
// braces coverage of the new code paths themselves (Attack-classifier event
// tracking, Stereo Link's crossfade, Auto Release's averager, Dither
// Shape's per-channel state) at their default/off settings.
namespace
{
    void setParam (ApotheosisAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }

    constexpr float toleranceDb = 0.5f; // see LimiterTests.cpp for rationale

    // A small corpus mirroring docs/design-brief.md Guarantee 1's list:
    // sine sweeps, a near-Nyquist inter-sample-peak signal, silence, and a
    // full-scale sine.
    using SignalBuilder = std::function<void (juce::AudioBuffer<float>&, double)>;

    std::vector<SignalBuilder> makeTestCorpus()
    {
        return {
            [] (juce::AudioBuffer<float>& buffer, double sampleRate)
            { TestHelpers::fillWithSine (buffer, sampleRate, 300.0, 0.7f); },
            [] (juce::AudioBuffer<float>& buffer, double sampleRate)
            { TestHelpers::fillWithSine (buffer, sampleRate, 5000.0, 0.9f); },
            [] (juce::AudioBuffer<float>& buffer, double sampleRate)
            { TestHelpers::fillWithSine (buffer, sampleRate, sampleRate * 0.45, 0.98f); }, // near-Nyquist ISP
            [] (juce::AudioBuffer<float>& buffer, double) { buffer.clear(); }, // silence
            [] (juce::AudioBuffer<float>& buffer, double sampleRate)
            { TestHelpers::fillWithSine (buffer, sampleRate, 1000.0, 1.0f); }, // full-scale
        };
    }
}

//==============================================================================
// Guarantee 1: bit-identical defaults.
//==============================================================================

TEST_CASE ("Guarantee 1: explicit v0.2.0 defaults are bit-identical to never touching the new controls, across the v1 test corpus",
           "[regression][guarantee1]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int numSamples = 2048;

    for (auto& buildSignal : makeTestCorpus())
    {
        TruePeakLimiterEngine engineImplicitDefault;
        TruePeakLimiterEngine engineExplicitDefault;

        juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (numSamples), 2 };
        engineImplicitDefault.prepare (spec);
        engineExplicitDefault.prepare (spec);

        // engineExplicitDefault: explicitly drive every new v0.2.0 setter to
        // its documented "off"/regression value.
        engineExplicitDefault.setAttackMs (0.0f);
        engineExplicitDefault.setAutoReleasePercent (0.0f);
        engineExplicitDefault.setStereoLinkPercent (100.0f);
        engineExplicitDefault.setDitherShape (0); // Flat

        // Shared non-default settings on the pre-existing v1 controls, so
        // this isn't a vacuous silence-in/silence-out comparison.
        for (auto* engine : { &engineImplicitDefault, &engineExplicitDefault })
        {
            engine->setInputGainDb (4.0f);
            engine->setCeilingDb (-1.0f);
            engine->setReleaseMs (60.0f);
        }

        juce::AudioBuffer<float> bufferA (2, numSamples);
        juce::AudioBuffer<float> bufferB (2, numSamples);
        buildSignal (bufferA, sampleRate);
        buildSignal (bufferB, sampleRate);

        juce::dsp::AudioBlock<float> blockA (bufferA);
        juce::dsp::AudioBlock<float> blockB (bufferB);

        // Several blocks so any state divergence would have time to appear.
        for (int i = 0; i < 4; ++i)
        {
            engineImplicitDefault.process (blockA);
            engineExplicitDefault.process (blockB);
        }

        for (int channel = 0; channel < bufferA.getNumChannels(); ++channel)
            for (int sample = 0; sample < bufferA.getNumSamples(); ++sample)
                CHECK (bufferA.getSample (channel, sample) == bufferB.getSample (channel, sample));
    }
}

TEST_CASE ("Guarantee 1: a fresh v0.2.0 processor's default state matches every documented v1 default value", "[regression][guarantee1]")
{
    ApotheosisAudioProcessor processor;

    CHECK (processor.apvts.getParameter (ParamIDs::attack)->getValue()
           == processor.apvts.getParameter (ParamIDs::attack)->convertTo0to1 (0.0f));
    CHECK (processor.apvts.getParameter (ParamIDs::autoRelease)->getValue()
           == processor.apvts.getParameter (ParamIDs::autoRelease)->convertTo0to1 (0.0f));
    CHECK (processor.apvts.getParameter (ParamIDs::stereoLink)->getValue()
           == processor.apvts.getParameter (ParamIDs::stereoLink)->convertTo0to1 (100.0f));
    CHECK (processor.apvts.getParameter (ParamIDs::ditherShape)->getValue()
           == processor.apvts.getParameter (ParamIDs::ditherShape)->convertTo0to1 (0.0f));
}

//==============================================================================
// Guarantee 6: ceiling guarantee holds across every new-parameter extreme.
//==============================================================================

TEST_CASE ("Guarantee 6: never-exceed-ceiling guarantee holds across every new-parameter extreme, individually and combined",
           "[regression][guarantee6][truepeak]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int numSamples = 4096;
    constexpr float ceilingDb = -1.0f;

    for (const auto attackExtreme : { 0.0f, 50.0f })
    {
        for (const auto autoReleaseExtreme : { 0.0f, 100.0f })
        {
            for (const auto stereoLinkExtreme : { 0.0f, 100.0f })
            {
                for (const auto ditherShapeExtreme : { 0.0f, 1.0f })
                {
                    CAPTURE (attackExtreme, autoReleaseExtreme, stereoLinkExtreme, ditherShapeExtreme);

                    ApotheosisAudioProcessor processor;
                    processor.prepareToPlay (sampleRate, numSamples);

                    setParam (processor, ParamIDs::ceiling, ceilingDb);
                    setParam (processor, ParamIDs::attack, attackExtreme);
                    setParam (processor, ParamIDs::autoRelease, autoReleaseExtreme);
                    setParam (processor, ParamIDs::stereoLink, stereoLinkExtreme);
                    setParam (processor, ParamIDs::ditherShape, ditherShapeExtreme);
                    setParam (processor, ParamIDs::dither, 1.0f); // 16-bit, so Dither Shape is actually exercised

                    // Hard-panned near-Nyquist ISP-rich signal: left full,
                    // right silent - the case Stereo Link is specifically
                    // meant to affect (see Guarantee 4's dedicated test),
                    // included here too since it's the most demanding combo
                    // for the ceiling guarantee.
                    juce::AudioBuffer<float> input (2, numSamples);
                    input.clear();
                    TestHelpers::fillWithSine (input, sampleRate, sampleRate * 0.45, 0.98f);
                    input.applyGain (1, 0, numSamples, 0.0f);

                    juce::AudioBuffer<float> processed;
                    juce::MidiBuffer midi;

                    for (int i = 0; i < 4; ++i)
                    {
                        processed.makeCopyOf (input);
                        processor.processBlock (processed, midi);
                    }

                    REQUIRE (TestHelpers::allSamplesFinite (processed));

                    const auto outputTruePeak = TestHelpers::measureTruePeakLinear (processed);
                    const auto ceilingLinear = juce::Decibels::decibelsToGain (ceilingDb);
                    const auto toleranceLinear = ceilingLinear * juce::Decibels::decibelsToGain (toleranceDb);

                    CHECK (outputTruePeak <= toleranceLinear);
                }
            }
        }
    }
}

//==============================================================================
// Guarantee 8: NaN/Inf robustness sweep with the new controls at extremes.
//==============================================================================

TEST_CASE ("Guarantee 8: NaN/Inf sweep with every new v0.2.0 control at its extreme, combined with extreme v1 controls",
           "[regression][guarantee8][robustness]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::inputGain, 24.0f);
    setParam (processor, ParamIDs::ceiling, -12.0f);
    setParam (processor, ParamIDs::release, 5.0f);
    setParam (processor, ParamIDs::clipMix, 100.0f);
    setParam (processor, ParamIDs::attack, 50.0f);
    setParam (processor, ParamIDs::autoRelease, 100.0f);
    setParam (processor, ParamIDs::stereoLink, 0.0f);
    setParam (processor, ParamIDs::dither, 2.0f); // 24-bit
    setParam (processor, ParamIDs::ditherShape, 1.0f); // Shaped

    constexpr int numSamples = 512;
    juce::AudioBuffer<float> buffer (2, numSamples);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* data = buffer.getWritePointer (channel);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            switch (sample % 4)
            {
                case 0: data[sample] = std::numeric_limits<float>::quiet_NaN(); break;
                case 1: data[sample] = std::numeric_limits<float>::infinity(); break;
                case 2: data[sample] = -std::numeric_limits<float>::infinity(); break;
                default: data[sample] = 0.4f; break;
            }
        }
    }

    juce::MidiBuffer midi;
    CHECK_NOTHROW (processor.processBlock (buffer, midi));
    CHECK (TestHelpers::allSamplesFinite (buffer));

    // Clean audio afterwards must stay finite too - any latent NaN in the
    // new per-channel envelope/attack-event/auto-release/dither-shape state
    // would otherwise poison every subsequent block.
    for (int i = 0; i < 8; ++i)
    {
        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.6f);
        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

//==============================================================================
// Guarantee 9: real-time safety.
//==============================================================================
//
// Verified primarily *by design*: every new v0.2.0 setter
// (setAttackMs/setAutoReleasePercent/setStereoLinkPercent/setDitherShape) is
// noexcept, touches only fixed-size member scalars/arrays, and performs no
// allocation, lock, or I/O - the Attack classifier's per-channel event
// counters are plain ints, Auto Release's running average is a single
// double, Stereo Link/Dither Shape are per-sample scalar computations over
// buffers already sized in prepare() (the per-channel sliding-window-minimum
// and gain-envelope arrays introduced in this pass are fixed at
// TruePeakLimiterEngine::maxChannels == 2 and resized only inside prepare()
// - see TruePeakLimiterEngine.cpp). This test exercises normal operation
// with all four new controls automated back-to-back with real audio
// processing, the same "coexists safely" pattern nave's PresetManagerTests.cpp
// uses for its own real-time-safety guarantee.
TEST_CASE ("Guarantee 9: rapid automation of every new v0.2.0 control coexists safely with real-time audio processing",
           "[regression][guarantee9][robustness]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;

    for (int block = 0; block < 50; ++block)
    {
        const auto t = static_cast<float> (block) / 50.0f;
        setParam (processor, ParamIDs::attack, t * 50.0f);
        setParam (processor, ParamIDs::autoRelease, t * 100.0f);
        setParam (processor, ParamIDs::stereoLink, 100.0f - t * 100.0f);
        setParam (processor, ParamIDs::ditherShape, (block % 2 == 0) ? 0.0f : 1.0f);

        TestHelpers::fillWithSine (buffer, 48000.0, 200.0 + t * 4000.0, 0.9f);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}
