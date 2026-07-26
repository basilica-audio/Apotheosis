#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <new>
#include <random>
#include <vector>

namespace
{
    void setParam (ApotheosisAudioProcessor& processor, const char* id, float realValue)
    {
        auto* param = processor.apvts.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }

    //==========================================================================
    // T16's allocation guard.
    //
    // Both counters are zero-initialised (std::atomic<int>'s constructor is
    // constexpr), so they are alive during CONSTANT initialisation - before
    // any dynamic initialiser in the binary can allocate. That matters
    // because replacing the global operator new below makes every
    // allocation in the whole Tests binary, including ones made before
    // main(), pass through it.
    //
    // Disarmed (the default, and the state during every other test case in
    // this binary) the replacement costs one relaxed atomic load per
    // allocation and otherwise behaves exactly like the default: malloc,
    // throw std::bad_alloc on failure. The matching operator delete
    // replacements exist so new/delete stay a malloc/free pair rather than
    // relying on the standard library pairing a replaced new with its own
    // unreplaced delete.
    std::atomic<int> allocationGuardArmed { 0 };
    std::atomic<int> allocationsWhileArmed { 0 };

    // Arms the guard for its lifetime. Scoped so a failed assertion inside
    // the guarded region cannot leave the counter armed for the rest of the
    // binary (Catch2 unwinds through this destructor).
    struct ScopedAllocationGuard
    {
        ScopedAllocationGuard()
        {
            allocationsWhileArmed.store (0, std::memory_order_relaxed);
            allocationGuardArmed.fetch_add (1, std::memory_order_relaxed);
        }

        ~ScopedAllocationGuard() { allocationGuardArmed.fetch_sub (1, std::memory_order_relaxed); }

        ScopedAllocationGuard (const ScopedAllocationGuard&) = delete;
        ScopedAllocationGuard& operator= (const ScopedAllocationGuard&) = delete;

        static int count() { return allocationsWhileArmed.load (std::memory_order_relaxed); }
    };

    void* guardedAllocate (std::size_t size)
    {
        if (allocationGuardArmed.load (std::memory_order_relaxed) != 0)
            allocationsWhileArmed.fetch_add (1, std::memory_order_relaxed);

        // malloc(0) may legitimately return nullptr; operator new must not.
        auto* memory = std::malloc (size != 0 ? size : 1);

        if (memory == nullptr)
            throw std::bad_alloc();

        return memory;
    }

    void* guardedAllocateAligned (std::size_t size, std::size_t alignment)
    {
        if (allocationGuardArmed.load (std::memory_order_relaxed) != 0)
            allocationsWhileArmed.fetch_add (1, std::memory_order_relaxed);

        void* memory = nullptr;

        // Both back ends want a power-of-two alignment that is at least
        // sizeof(void*); C++17's aligned new already guarantees a power of
        // two, so only the lower bound needs raising.
        const auto effectiveAlignment = std::max (alignment, sizeof (void*));
        const auto effectiveSize = size != 0 ? size : 1;

       #if defined (_WIN32)
        // MSVC has no posix_memalign/aligned_alloc; _aligned_malloc is the
        // equivalent and returns nullptr rather than an errno on failure.
        // Its blocks must be released with _aligned_free, which is what the
        // aligned operator delete replacements below do.
        memory = ::_aligned_malloc (effectiveSize, effectiveAlignment);

        if (memory == nullptr)
            throw std::bad_alloc();
       #else
        if (::posix_memalign (&memory, effectiveAlignment, effectiveSize) != 0)
            throw std::bad_alloc();
       #endif

        return memory;
    }

    void guardedFreeAligned (void* memory) noexcept
    {
       #if defined (_WIN32)
        ::_aligned_free (memory);
       #else
        std::free (memory);
       #endif
    }
}

void* operator new (std::size_t size) { return guardedAllocate (size); }
void* operator new[] (std::size_t size) { return guardedAllocate (size); }

void* operator new (std::size_t size, std::align_val_t alignment)
{
    return guardedAllocateAligned (size, static_cast<std::size_t> (alignment));
}

void* operator new[] (std::size_t size, std::align_val_t alignment)
{
    return guardedAllocateAligned (size, static_cast<std::size_t> (alignment));
}

void operator delete (void* memory) noexcept { std::free (memory); }
void operator delete[] (void* memory) noexcept { std::free (memory); }
void operator delete (void* memory, std::size_t) noexcept { std::free (memory); }
void operator delete[] (void* memory, std::size_t) noexcept { std::free (memory); }
void operator delete (void* memory, std::align_val_t) noexcept { guardedFreeAligned (memory); }
void operator delete[] (void* memory, std::align_val_t) noexcept { guardedFreeAligned (memory); }
void operator delete (void* memory, std::size_t, std::align_val_t) noexcept { guardedFreeAligned (memory); }
void operator delete[] (void* memory, std::size_t, std::align_val_t) noexcept { guardedFreeAligned (memory); }

TEST_CASE ("Silence produces silence (and no NaN/Inf)", "[robustness]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();

    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
    CHECK (TestHelpers::peakAbsolute (buffer) == 0.0f);
}

TEST_CASE ("Full-scale input at maximum input gain produces no NaN/Inf", "[robustness]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::inputGain, 24.0f);
    setParam (processor, ParamIDs::ceiling, -1.0f);
    setParam (processor, ParamIDs::release, 5.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 1.0f);

    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
    CHECK (TestHelpers::peakAbsolute (buffer) < 10.0f); // sane bound, not just "finite"
}

TEST_CASE ("Denormal-range input produces no NaN/Inf output", "[robustness]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    constexpr int numSamples = 512;
    juce::AudioBuffer<float> buffer (2, numSamples);

    const auto denormalValue = std::numeric_limits<float>::denorm_min() * 4.0f;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* data = buffer.getWritePointer (channel);

        for (int sample = 0; sample < numSamples; ++sample)
            data[sample] = (sample % 2 == 0) ? denormalValue : -denormalValue;
    }

    juce::MidiBuffer midi;

    for (int i = 0; i < 8; ++i)
        CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("NaN/Inf sweep: poisoned input samples do not propagate indefinitely", "[robustness][nan]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::inputGain, 6.0f);

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
                default: data[sample] = 0.3f; break;
            }
        }
    }

    juce::MidiBuffer midi;
    CHECK_NOTHROW (processor.processBlock (buffer, midi));
    CHECK (TestHelpers::allSamplesFinite (buffer));

    // Feed clean audio afterwards: any latent NaN in internal filter/
    // envelope state would otherwise poison every subsequent block forever.
    for (int i = 0; i < 8; ++i)
    {
        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.5f);
        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("Zero-sample buffer does not crash processBlock", "[robustness]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 0);
    juce::MidiBuffer midi;

    CHECK_NOTHROW (processor.processBlock (buffer, midi));
    CHECK (buffer.getNumSamples() == 0);
}

TEST_CASE ("Extreme parameter values at both range edges produce no NaN/Inf", "[robustness]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (44100.0, 256);

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;

    for (bool useMinimum : { true, false })
    {
        setParam (processor, ParamIDs::inputGain, useMinimum ? -12.0f : 24.0f);
        setParam (processor, ParamIDs::ceiling, useMinimum ? -12.0f : 0.0f);
        setParam (processor, ParamIDs::release, useMinimum ? 5.0f : 1000.0f);

        TestHelpers::fillWithSine (buffer, 44100.0, 440.0, 0.8f);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("Rapid parameter automation across many blocks produces no NaN/Inf", "[robustness]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);

    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> unit (0.0f, 1.0f);

    juce::MidiBuffer midi;

    for (int block = 0; block < 100; ++block)
    {
        setParam (processor, ParamIDs::inputGain, -12.0f + unit (rng) * 36.0f);
        setParam (processor, ParamIDs::ceiling, -12.0f + unit (rng) * 12.0f);
        setParam (processor, ParamIDs::release, 5.0f + unit (rng) * 995.0f);

        juce::AudioBuffer<float> buffer (2, 256);
        TestHelpers::fillWithSine (buffer, 48000.0, 200.0 + unit (rng) * 4000.0, 0.9f);

        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
    }
}

TEST_CASE ("reset() followed by processBlock does not crash", "[robustness]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    setParam (processor, ParamIDs::inputGain, 12.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.6f);
    juce::MidiBuffer midi;

    processor.processBlock (buffer, midi);

    CHECK_NOTHROW (processor.reset());

    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.6f);
    CHECK_NOTHROW (processor.processBlock (buffer, midi));
    CHECK (TestHelpers::allSamplesFinite (buffer));
}

TEST_CASE ("Block size larger than prepared maximum is chunked, not passed straight into the oversampler", "[robustness]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 256);

    constexpr float ceilingDb = -1.0f;
    setParam (processor, ParamIDs::ceiling, ceilingDb);
    setParam (processor, ParamIDs::release, 20.0f);

    // Some hosts occasionally hand over a block larger than promised at
    // prepareToPlay() (e.g. offline bounce/render, buffer-size
    // renegotiation - see issue #14). 700 is deliberately not a multiple of
    // the prepared 256-sample maximum, so process()'s chunking loop is also
    // exercised on a partial final chunk.
    constexpr int numSamples = 700;
    static_assert (numSamples > 256, "must actually exceed the prepared maximum - see issue #15");

    juce::AudioBuffer<float> buffer (2, numSamples);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.9f);
    juce::MidiBuffer midi;

    // Before issue #14 was fixed, TruePeakLimiterEngine::process() passed
    // this oversized block straight into juce::dsp::Oversampling, whose
    // internal buffer juce::dsp::Oversampling::initProcessing() sized for
    // at most 256 samples at prepareToPlay() time; every processSamplesUp/
    // Down override guards its writes past that only with a debug-only
    // jassert (Release: silent heap corruption with no exception to catch;
    // Debug: an assertion failure). CHECK_NOTHROW alone would therefore not
    // reliably have caught the bug even pre-fix (see issue #15), so this
    // also pins down the actual output: fully finite and still respecting
    // the ceiling, exactly as a correctly chunked call must.
    CHECK_NOTHROW (processor.processBlock (buffer, midi));

    CHECK (TestHelpers::allSamplesFinite (buffer));

    const auto ceilingLinear = juce::Decibels::decibelsToGain (ceilingDb);
    constexpr float toleranceDb = 0.5f; // see LimiterTests.cpp for rationale
    const auto toleranceLinear = ceilingLinear * juce::Decibels::decibelsToGain (toleranceDb);

    CHECK (TestHelpers::peakAbsolute (buffer) <= toleranceLinear);
}

//==============================================================================
// T16 - realtime safety with the full v0.4.0 feature set engaged.
//
// The three cases below are the brief's three T16 clauses: oversized-block
// chunking under the heaviest possible configuration, an allocation guard
// over process(), and a denormal check on the silence that follows a burst.

namespace
{
    // Puts every v0.4.0 feature into its most expensive state: 16x Linear
    // Phase oversampling, True Peak Guard on (its detector then runs on the
    // output too), Weighted 16-bit noise-shaped dither, and the Safe style
    // (three cascaded boxes - the widest smoother). Delta and Unity Gain are
    // deliberately left off here; the allocation case below turns them on.
    //
    // oversampling/osPhase are prepare-latched (docs/manual.md), so this must
    // be called BEFORE prepareToPlay() for the factor to take effect.
    void engageEveryV040Feature (ApotheosisAudioProcessor& processor)
    {
        setParam (processor, ParamIDs::limitStyle, 4.0f);       // Safe
        setParam (processor, ParamIDs::oversampling, 2.0f);     // 16x
        setParam (processor, ParamIDs::osPhase, 1.0f);          // Linear Phase
        setParam (processor, ParamIDs::tpGuard, 1.0f);
        setParam (processor, ParamIDs::dither, 1.0f);           // 16-bit
        setParam (processor, ParamIDs::noiseShaping, 1.0f);     // Weighted
        setParam (processor, ParamIDs::autoRelease, 75.0f);
        setParam (processor, ParamIDs::stereoLink, 60.0f);
        setParam (processor, ParamIDs::clipMix, 25.0f);
    }
}

TEST_CASE ("T16: an oversized block still chunks safely with 16x, True Peak Guard and Weighted dither engaged",
           "[robustness][v040][t16]")
{
    ApotheosisAudioProcessor processor;

    constexpr float ceilingDb = -1.0f;
    engageEveryV040Feature (processor);
    setParam (processor, ParamIDs::ceiling, ceilingDb);
    setParam (processor, ParamIDs::release, 20.0f);
    setParam (processor, ParamIDs::inputGain, 9.0f); // force heavy limiting

    // Prepared for 256, handed 700 - the same not-a-multiple oversized-block
    // scenario as the v0.2.0 case above (issue #14/#15), but now with the
    // 16x oversampler, whose internal buffers are four times larger and
    // whose chunking arithmetic has four times the stride to get wrong.
    processor.prepareToPlay (48000.0, 256);

    constexpr int numSamples = 700;
    static_assert (numSamples > 256, "must actually exceed the prepared maximum");

    juce::AudioBuffer<float> buffer (2, numSamples);
    TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.95f);
    juce::MidiBuffer midi;

    CHECK_NOTHROW (processor.processBlock (buffer, midi));
    CHECK (TestHelpers::allSamplesFinite (buffer));

    const auto ceilingLinear = juce::Decibels::decibelsToGain (ceilingDb);
    constexpr float toleranceDb = 0.5f; // see LimiterTests.cpp for rationale
    CHECK (TestHelpers::peakAbsolute (buffer)
           <= ceilingLinear * juce::Decibels::decibelsToGain (toleranceDb));

    // Repeat over several consecutive oversized blocks so the failure mode
    // is a growing overrun rather than a single lucky call.
    for (int block = 0; block < 8; ++block)
    {
        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.95f);
        CHECK_NOTHROW (processor.processBlock (buffer, midi));
        CHECK (TestHelpers::allSamplesFinite (buffer));
        CHECK (TestHelpers::peakAbsolute (buffer)
               <= ceilingLinear * juce::Decibels::decibelsToGain (toleranceDb));
    }
}

TEST_CASE ("T16: processBlock allocates nothing with every v0.4.0 feature on", "[robustness][v040][t16][allocation]")
{
    ApotheosisAudioProcessor processor;

    engageEveryV040Feature (processor);
    setParam (processor, ParamIDs::deltaListen, 1.0f);
    setParam (processor, ParamIDs::unityGainMonitor, 1.0f);
    setParam (processor, ParamIDs::ceiling, -1.0f);
    setParam (processor, ParamIDs::inputGain, 9.0f);

    constexpr int blockSize = 512;
    processor.prepareToPlay (48000.0, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    // Warm-up outside the guard: prepareToPlay() is allowed to allocate, and
    // so is anything the very first blocks touch lazily (the gated loudness
    // meter's histograms complete their first 100 ms sub-block after ~9
    // blocks at this size, so run comfortably past that).
    for (int block = 0; block < 32; ++block)
    {
        TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.95f);
        processor.processBlock (buffer, midi);
    }

    // Guarded region: steady-state audio-thread work only. Everything the
    // engine needs - lookahead ring buffers, the sliding-minimum deques, the
    // box-smoother accumulators, the dual release states, the guard delay
    // line, the true-peak interpolator's history, the loudness histograms -
    // was sized in prepare() and must not grow now, at any block, in any
    // combination of features (Delta and Unity Gain included).
    {
        ScopedAllocationGuard guard;

        for (int block = 0; block < 64; ++block)
        {
            TestHelpers::fillWithSine (buffer, 48000.0, 1000.0, 0.95f);
            processor.processBlock (buffer, midi);
        }
    }

    CHECK (ScopedAllocationGuard::count() == 0);

    // The guard itself must work, or the assertion above is vacuous: an
    // allocation inside an armed region has to be seen.
    {
        ScopedAllocationGuard guard;
        auto* canary = new int (7);
        CHECK (*canary == 7);
        delete canary;
    }

    CHECK (ScopedAllocationGuard::count() >= 1);
}

TEST_CASE ("T16: the long silence after a loud burst does not slow down (denormals stay flushed)",
           "[robustness][v040][t16][denormal]")
{
    ApotheosisAudioProcessor processor;

    // 4x Minimum Phase (the default chain) on purpose: this case is about
    // denormal flushing in the release tails and the metering filters, and a
    // Debug-build 16x run over a minute of audio would dominate suite
    // runtime without testing anything more.
    setParam (processor, ParamIDs::limitStyle, 1.0f); // Transparent
    setParam (processor, ParamIDs::tpGuard, 1.0f);
    setParam (processor, ParamIDs::dither, 1.0f);
    setParam (processor, ParamIDs::noiseShaping, 1.0f);
    setParam (processor, ParamIDs::autoRelease, 75.0f);
    setParam (processor, ParamIDs::ceiling, -1.0f);
    setParam (processor, ParamIDs::release, 500.0f);
    setParam (processor, ParamIDs::inputGain, 12.0f);

    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    processor.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;

    // A loud burst first: it drives every one-pole in the plugin (gain
    // envelope, both release followers, the auto-release depth integrator,
    // the K-weighting filters, the guard's ballistics) far from zero, so the
    // silence that follows decays through the denormal range instead of
    // starting there.
    for (int block = 0; block < 40; ++block)
    {
        TestHelpers::fillWithSine (buffer, sampleRate, 200.0, 1.0f);
        processor.processBlock (buffer, midi);
    }

    // 60 s of digital silence, per the brief's T16 clause.
    constexpr int silentBlocks = static_cast<int> (60.0 * sampleRate) / blockSize;
    std::vector<double> blockMicros;
    blockMicros.reserve (static_cast<size_t> (silentBlocks));

    for (int block = 0; block < silentBlocks; ++block)
    {
        buffer.clear();

        const auto start = std::chrono::steady_clock::now();
        processor.processBlock (buffer, midi);
        const auto elapsed = std::chrono::steady_clock::now() - start;

        blockMicros.push_back (
            std::chrono::duration_cast<std::chrono::duration<double, std::micro>> (elapsed).count());

        REQUIRE (TestHelpers::allSamplesFinite (buffer));
    }

    REQUIRE (blockMicros.size() > 100);

    const auto medianOf = [] (std::vector<double> values) {
        std::sort (values.begin(), values.end());
        return values[values.size() / 2];
    };

    // The assertion is on MEDIANS of two halves of the silence, not on the
    // maximum or on a high percentile of all blocks. Wall-clock timing of a
    // single block is dominated by scheduler noise on any shared machine
    // (local runs share the box with parallel builds; CI runners are
    // virtualised), so an individual outlier block proves nothing - measured
    // here, the slowest single block ran ~17x the median while every
    // percentile below it sat flat.
    //
    // A denormal stall has a completely different, and much easier to
    // detect, signature: it is systematic and it gets WORSE over time,
    // because the release tails, the auto-release depth integrator and the
    // K-weighting filter states decay steadily further into the denormal
    // range the longer the silence lasts. So the second half of the silence
    // is where denormal cost would concentrate, and comparing its median
    // against the first half's is both immune to one-off outliers and a
    // direct measurement of the thing ScopedNoDenormals exists to prevent.
    const auto midpoint = blockMicros.begin() + static_cast<long> (blockMicros.size() / 2);
    const auto firstHalfMedian = medianOf ({ blockMicros.begin(), midpoint });
    const auto secondHalfMedian = medianOf ({ midpoint, blockMicros.end() });

    auto sorted = blockMicros;
    std::sort (sorted.begin(), sorted.end());

    CAPTURE (firstHalfMedian, secondHalfMedian, sorted[sorted.size() / 2], sorted.back(), silentBlocks);
    REQUIRE (firstHalfMedian > 0.0);
    CHECK (secondHalfMedian < 2.0 * firstHalfMedian);
}
