#include "dsp/TruePeakLimiterEngine.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

// v0.2.0 deep-dive additions (docs/design-brief.md) - Guarantee 2 (Attack
// classifier proof) and Guarantee 3 (Auto Release monotonicity proof).
namespace
{
    // Processes a timeline of `eventDurationSamples` of a loud 2 kHz tone
    // (well above the ceiling, so every cycle reliably drives gain
    // reduction regardless of how short the event is), followed by
    // `silenceAfterEventSamples` of true silence, through a fresh engine
    // configured with the given Attack setting - then returns the engine's
    // gain-reduction reading
    // (dB, 0 = no reduction, negative = reducing) exactly
    // `measureOffsetAfterEventEndSamples` samples after the event ends. A
    // smaller (less negative / closer to 0) result means "recovered more" -
    // i.e. faster recovery.
    float measureGainReductionAfterEvent (float attackMs, int eventDurationSamples, int measureOffsetAfterEventEndSamples)
    {
        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 128;

        TruePeakLimiterEngine engine;
        juce::dsp::ProcessSpec spec { sampleRate, blockSize, 2 };
        // Small lookahead so the raw-gain event duration (what the Attack
        // classifier tracks) closely matches the constructed signal's own
        // loud-region duration, and a slow Release so the fast-attack-path
        // vs normal-release-path difference is unambiguous.
        engine.setLookaheadMs (1.0f);
        engine.prepare (spec);
        engine.setCeilingDb (-1.0f);
        engine.setReleaseMs (300.0f);
        engine.setAttackMs (attackMs);

        const auto silenceAfterEventSamples = measureOffsetAfterEventEndSamples + blockSize * 20;
        const auto totalSamples = eventDurationSamples + silenceAfterEventSamples;

        juce::AudioBuffer<float> timeline (2, totalSamples);
        timeline.clear();

        if (eventDurationSamples > 0)
        {
            juce::AudioBuffer<float> loud (2, eventDurationSamples);
            TestHelpers::fillWithSine (loud, sampleRate, 2000.0, 0.99f);

            for (int channel = 0; channel < timeline.getNumChannels(); ++channel)
                timeline.copyFrom (channel, 0, loud, channel, 0, eventDurationSamples);
        }

        float measured = 0.0f;
        bool measuredOnce = false;
        int processed = 0;

        while (processed < totalSamples)
        {
            const auto thisBlockSize = std::min (blockSize, totalSamples - processed);

            juce::AudioBuffer<float> chunk (2, thisBlockSize);

            for (int channel = 0; channel < chunk.getNumChannels(); ++channel)
                chunk.copyFrom (channel, 0, timeline, channel, processed, thisBlockSize);

            juce::dsp::AudioBlock<float> block (chunk);
            engine.process (block);

            processed += thisBlockSize;

            const auto measurePoint = eventDurationSamples + measureOffsetAfterEventEndSamples;

            if (! measuredOnce && processed >= measurePoint)
            {
                measured = engine.getGainReductionDb();
                measuredOnce = true;
            }
        }

        REQUIRE (measuredOnce);
        return measured;
    }
}

//==============================================================================
// Guarantee 2: Attack classifier proof.
//==============================================================================

TEST_CASE ("Guarantee 2: with Attack engaged, a short transient recovers measurably faster than a sustained passage",
           "[dsp][attack][guarantee2]")
{
    constexpr float testAttackMs = 8.0f;
    constexpr int measureOffset = 48000 / 1000 * 10; // 10 ms after the event ends

    // Well below testAttackMs at 48 kHz (48000 * 0.002 = 96 samples ~ 2 ms).
    constexpr int transientEventSamples = 96;
    // Well above testAttackMs (48000 * 0.05 = 2400 samples = 50 ms).
    constexpr int sustainedEventSamples = 2400;

    const auto transientRecovery = measureGainReductionAfterEvent (testAttackMs, transientEventSamples, measureOffset);
    const auto sustainedRecovery = measureGainReductionAfterEvent (testAttackMs, sustainedEventSamples, measureOffset);

    CAPTURE (transientRecovery, sustainedRecovery);

    // Both engaged real gain reduction during their respective events.
    REQUIRE (transientRecovery < 0.0f);
    REQUIRE (sustainedRecovery < 0.0f);

    // The transient (classified as short - fast fixed release path) must
    // have recovered further (closer to 0 dB) than the sustained passage
    // (classified as long - normal 300 ms Release-governed path) by the
    // same fixed offset after each event ends.
    CHECK (transientRecovery > sustainedRecovery);

    // Measurably so, not just a rounding-noise difference.
    CHECK (transientRecovery - sustainedRecovery > 1.0f);
}

TEST_CASE ("Guarantee 2: the transient/sustained recovery differential disappears when Attack = 0 (bit-identical-regression path)",
           "[dsp][attack][guarantee2]")
{
    constexpr int measureOffset = 48000 / 1000 * 10; // 10 ms after the event ends
    constexpr int transientEventSamples = 96;
    constexpr int sustainedEventSamples = 2400;

    const auto transientRecovery = measureGainReductionAfterEvent (0.0f, transientEventSamples, measureOffset);
    const auto sustainedRecovery = measureGainReductionAfterEvent (0.0f, sustainedEventSamples, measureOffset);

    CAPTURE (transientRecovery, sustainedRecovery);

    // At Attack = 0, every event is classified "sustained" - both recover
    // via the same plain Release-governed path. They are not expected to be
    // numerically IDENTICAL (the two events reach different peak gain-
    // reduction depths during their own differently-sized loud regions, so
    // their release trajectories start from different points), but the
    // *dramatic* differential seen above (Guarantee 2's first test) must be
    // gone - a modest margin, not the >1 dB gap asserted with Attack
    // engaged.
    CHECK (std::abs (transientRecovery - sustainedRecovery) < 1.0f);
}

//==============================================================================
// Guarantee 3: Auto Release monotonicity proof.
//==============================================================================

namespace
{
    // Measures the gain-reduction reading a fixed offset after a signal
    // event ends, for a given Auto Release percentage - reusing the same
    // measurement primitive as Guarantee 2's helper (Attack left at 0% so
    // Auto Release's own effect is isolated).
    float measureAutoReleaseRecovery (float autoReleasePercent, int eventDurationSamples, int measureOffsetAfterEventEndSamples)
    {
        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 128;

        TruePeakLimiterEngine engine;
        juce::dsp::ProcessSpec spec { sampleRate, blockSize, 2 };
        engine.setLookaheadMs (1.0f);
        engine.prepare (spec);
        engine.setInputGainDb (9.0f); // drive comfortably past the ceiling so the averaged depth reliably saturates for a sustained passage
        engine.setCeilingDb (-1.0f);
        engine.setReleaseMs (200.0f);
        engine.setAutoReleasePercent (autoReleasePercent);

        const auto silenceAfterEventSamples = measureOffsetAfterEventEndSamples + blockSize * 20;
        const auto totalSamples = eventDurationSamples + silenceAfterEventSamples;

        juce::AudioBuffer<float> timeline (2, totalSamples);
        timeline.clear();

        juce::AudioBuffer<float> loud (2, eventDurationSamples);
        TestHelpers::fillWithSine (loud, sampleRate, 2000.0, 0.99f);

        for (int channel = 0; channel < timeline.getNumChannels(); ++channel)
            timeline.copyFrom (channel, 0, loud, channel, 0, eventDurationSamples);

        float measured = 0.0f;
        bool measuredOnce = false;
        int processed = 0;

        while (processed < totalSamples)
        {
            const auto thisBlockSize = std::min (blockSize, totalSamples - processed);

            juce::AudioBuffer<float> chunk (2, thisBlockSize);

            for (int channel = 0; channel < chunk.getNumChannels(); ++channel)
                chunk.copyFrom (channel, 0, timeline, channel, processed, thisBlockSize);

            juce::dsp::AudioBlock<float> block (chunk);
            engine.process (block);

            processed += thisBlockSize;

            const auto measurePoint = eventDurationSamples + measureOffsetAfterEventEndSamples;

            if (! measuredOnce && processed >= measurePoint)
            {
                measured = engine.getGainReductionDb();
                measuredOnce = true;
            }
        }

        REQUIRE (measuredOnce);
        return measured;
    }
}

TEST_CASE ("Guarantee 3: sweeping Auto Release 0% to 100% lengthens the dense/sustained passage's recovery time monotonically",
           "[dsp][autorelease][guarantee3]")
{
    // ~3 seconds of continuous loud material - long enough (relative to
    // TruePeakLimiterEngine::autoReleaseTimeConstantSeconds == 2.0) for the
    // gain-reduction-depth average to saturate near its maximum, so
    // depthNorm sits close to 1 for the whole sweep below.
    constexpr int denseEventSamples = static_cast<int> (48000.0 * 3.0);
    constexpr int measureOffset = 48000 / 1000 * 15; // 15 ms after the passage ends

    float previousAbsRecovery = -1.0f; // sentinel: first iteration has nothing to compare against

    for (const auto autoReleasePercent : { 0.0f, 25.0f, 50.0f, 75.0f, 100.0f })
    {
        const auto recovery = measureAutoReleaseRecovery (autoReleasePercent, denseEventSamples, measureOffset);
        const auto absRecovery = std::abs (recovery); // "how far still reduced" - larger = slower/longer effective release

        CAPTURE (autoReleasePercent, recovery, absRecovery);

        if (previousAbsRecovery >= 0.0f)
            CHECK (absRecovery >= previousAbsRecovery); // monotonically non-decreasing as Auto Release increases

        previousAbsRecovery = absRecovery;
    }

    // Confirm the sweep produced a *meaningful* (not just numerically
    // technically-monotonic-by-noise) change end to end. 0.5 dB is a
    // conservative floor - well above any float-noise margin used elsewhere
    // in this suite (see LimiterTests.cpp's 0.5 dB true-peak tolerance for
    // the same order-of-magnitude "clearly real, not noise" bar) - not a
    // claim about a specific target swing size.
    const auto recoveryAt0 = measureAutoReleaseRecovery (0.0f, denseEventSamples, measureOffset);
    const auto recoveryAt100 = measureAutoReleaseRecovery (100.0f, denseEventSamples, measureOffset);
    CHECK (std::abs (recoveryAt100) - std::abs (recoveryAt0) > 0.5f);
}

TEST_CASE ("Guarantee 3: an isolated peak's own recovery changes markedly less across the same Auto Release sweep",
           "[dsp][autorelease][guarantee3]")
{
    // A brief, isolated peak - against the same multi-second averager, its
    // own gain-reduction-depth average barely moves off the idle baseline
    // (depthNorm stays close to 0), which is what keeps its post-peak
    // recovery close to the plain Release behaviour regardless of Auto
    // Release - see TruePeakLimiterEngine.h's docs on
    // autoReleaseShortenRangeFraction.
    constexpr int isolatedEventSamples = 48000 / 1000 * 30; // 30 ms
    constexpr int measureOffset = 48000 / 1000 * 15; // 15 ms after the peak ends
    constexpr int denseEventSamples = static_cast<int> (48000.0 * 3.0);

    const auto isolatedRecoveryAt0 = measureAutoReleaseRecovery (0.0f, isolatedEventSamples, measureOffset);
    const auto isolatedRecoveryAt100 = measureAutoReleaseRecovery (100.0f, isolatedEventSamples, measureOffset);
    const auto isolatedSwing = std::abs (std::abs (isolatedRecoveryAt100) - std::abs (isolatedRecoveryAt0));

    const auto denseRecoveryAt0 = measureAutoReleaseRecovery (0.0f, denseEventSamples, measureOffset);
    const auto denseRecoveryAt100 = measureAutoReleaseRecovery (100.0f, denseEventSamples, measureOffset);
    const auto denseSwing = std::abs (std::abs (denseRecoveryAt100) - std::abs (denseRecoveryAt0));

    CAPTURE (isolatedRecoveryAt0, isolatedRecoveryAt100, isolatedSwing, denseRecoveryAt0, denseRecoveryAt100, denseSwing);

    CHECK (isolatedSwing < denseSwing);
}
