#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "dsp/TruePeakLimiterEngine.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_cryptography/juce_cryptography.h>

#include <cmath>
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

//==============================================================================
// T0/T1: golden v0.2.0 fixtures (v0.4.0 brief, section 6).
//==============================================================================
//
// The A-vs-B cases above compare two engines of the SAME build, which cannot
// detect a regression both sides share. The golden-fixture harness below
// pins the actual v0.2.0 output to disk: the hidden [.golden-gen] generator
// was run exactly once, at the first commit of the v0.4.0 branch - while the
// engine was still byte-identical to origin/main @ 8558679 (v0.2.0) - and
// the resulting raw float32 renders live in tests/fixtures/v020/ together
// with a manifest (settings, SHA-256 per file, generation platform and
// compiler). Every later commit re-renders the same corpus through the
// refactored engine and compares against those files (T1 below).
//
// Platform pin: bit-exact float renders are only guaranteed on the
// generation platform (macOS arm64 - local dev and the macOS CI runner).
// Elsewhere (Windows CI - different compiler, libm, FMA contraction) the
// comparison runs with a 1e-7 absolute tolerance instead; the in-binary
// A-vs-B Guarantee-1 cases above keep running bit-exact everywhere as the
// second net.
namespace GoldenFixtures
{
    constexpr double fixtureSampleRate = 48000.0;
    constexpr int fixtureBlockSize = 2048;
    constexpr int fixtureNumBlocks = 4;
    constexpr int fixtureNumSamples = fixtureBlockSize * fixtureNumBlocks;
    constexpr int fixtureNumChannels = 2;

    // Arbitrary but frozen: the dither fixture's RNG seed. Changing it would
    // invalidate tests/fixtures/v020/dither16.f32.
    constexpr juce::int64 ditherFixtureSeed = 0x5EEDA5D17LL;

    struct FixtureSpec
    {
        const char* fileName;
        const char* description;
        int corpusIndex; // index into makeTestCorpus(); -1 = dither fixture (silence input)
    };

    // The 5-signal corpus mirrors makeTestCorpus() above (docs/design-brief.md
    // Guarantee 1's list); the sixth entry is the 16-bit Legacy-dither
    // fixture needed by T12 (tests/StereoLinkDitherShapeTests.cpp).
    inline const std::vector<FixtureSpec>& allFixtureSpecs()
    {
        static const std::vector<FixtureSpec> specs = {
            { "signal0_sine300.f32", "300 Hz sine, amplitude 0.7", 0 },
            { "signal1_sine5k.f32", "5 kHz sine, amplitude 0.9", 1 },
            { "signal2_nearNyquistIsp.f32", "0.45*fs sine, amplitude 0.98 (near-Nyquist ISP)", 2 },
            { "signal3_silence.f32", "digital silence", 3 },
            { "signal4_sine1kFullScale.f32", "1 kHz sine, amplitude 1.0 (full scale)", 4 },
            { "dither16_legacy.f32", "silence through 16-bit Legacy (v0.2.0 flat TPDF) dither, seeded RNG", -1 },
        };
        return specs;
    }

    inline juce::File fixtureDirectory()
    {
        return juce::File (APOTHEOSIS_TESTS_DIR).getChildFile ("fixtures").getChildFile ("v020");
    }

    // Renders one fixture through a fresh engine, streaming one continuous
    // fixtureNumSamples-long signal through fixtureNumBlocks sequential
    // prepare()-sized chunks (NOT the in-place recirculation the A-vs-B
    // cases use - a continuous stream keeps the later guard-delay alignment
    // in T1 a single global shift instead of a per-iteration accumulation).
    // Settings per the v0.4.0 brief section 6 T0: corpus renders use
    // inputGain +4 dB / ceiling -1 dB / release 60 ms (all other controls at
    // their defaults); the dither fixture uses all-default settings plus
    // 16-bit dither with a seeded RNG over silence (so the entire output IS
    // the deterministic dither noise sequence, and no delay alignment can
    // ever apply to it).
    inline juce::AudioBuffer<float> renderFixture (const FixtureSpec& spec)
    {
        TruePeakLimiterEngine engine;
        juce::dsp::ProcessSpec processSpec { fixtureSampleRate,
                                             static_cast<juce::uint32> (fixtureBlockSize),
                                             static_cast<juce::uint32> (fixtureNumChannels) };
        engine.prepare (processSpec);

        juce::AudioBuffer<float> buffer (fixtureNumChannels, fixtureNumSamples);
        buffer.clear();

        if (spec.corpusIndex >= 0)
        {
            engine.setInputGainDb (4.0f);
            engine.setCeilingDb (-1.0f);
            engine.setReleaseMs (60.0f);

            makeTestCorpus()[static_cast<size_t> (spec.corpusIndex)] (buffer, fixtureSampleRate);
        }
        else
        {
            engine.setDitherMode (1); // 16-bit
            engine.setDitherSeedForTests (ditherFixtureSeed);
        }

        juce::dsp::AudioBlock<float> whole (buffer);

        for (int block = 0; block < fixtureNumBlocks; ++block)
        {
            auto chunk = whole.getSubBlock (static_cast<size_t> (block * fixtureBlockSize),
                                            static_cast<size_t> (fixtureBlockSize));
            engine.process (chunk);
        }

        return buffer;
    }

    // Raw float32, native (little-endian on every supported platform)
    // byte order, channel 0's samples then channel 1's.
    inline juce::MemoryBlock toRawBytes (const juce::AudioBuffer<float>& buffer)
    {
        juce::MemoryBlock bytes;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            bytes.append (buffer.getReadPointer (channel),
                          static_cast<size_t> (buffer.getNumSamples()) * sizeof (float));

        return bytes;
    }

    inline bool loadFixture (const juce::File& file, juce::AudioBuffer<float>& out)
    {
        juce::MemoryBlock bytes;

        if (! file.loadFileAsData (bytes))
            return false;

        const auto expectedBytes = static_cast<size_t> (fixtureNumChannels)
                                    * static_cast<size_t> (fixtureNumSamples) * sizeof (float);

        if (bytes.getSize() != expectedBytes)
            return false;

        out.setSize (fixtureNumChannels, fixtureNumSamples);
        const auto* data = static_cast<const float*> (bytes.getData());

        for (int channel = 0; channel < fixtureNumChannels; ++channel)
            for (int sample = 0; sample < fixtureNumSamples; ++sample)
                out.setSample (channel, sample, data[channel * fixtureNumSamples + sample]);

        return true;
    }

    // Generation platform pin (see the file-level comment above): fixtures
    // were generated on macOS arm64, where float renders are reproducible
    // bit-for-bit; elsewhere the comparison tolerates cross-compiler float
    // drift up to 1e-7 absolute.
#if JUCE_MAC && JUCE_ARM
    constexpr bool onGenerationPlatform = true;
#else
    constexpr bool onGenerationPlatform = false;
#endif

    // Base-rate output alignment between a fixture (rendered by the v0.2.0
    // engine) and the current engine. v0.2.0 had no true-peak-guard delay
    // line; the v0.4.0 engine inserts a constant per-rate-policy-tier guard
    // delay (brief section 3.2's table: +6 base samples below 176.4 kHz).
    // The dither fixture is exempt by construction (silence in, dither
    // noise injected at the very end of the chain, after the delay).
    //
    // NOTE (commit 1): still 0 - the guard delay line does not exist yet.
    // The F2 commit updates this to the engine's per-rate constant.
    inline int fixtureAlignmentSamples()
    {
        return 0;
    }
}

// Hidden generator (leading '.' tag: never part of a default run). Run
// exactly once via `Tests "[.golden-gen]"` at the branch's first commit -
// see the harness comment above. Re-running it later would overwrite the
// fixtures with post-refactor output and defeat the whole purpose; the
// committed files in tests/fixtures/v020/ are the source of truth.
TEST_CASE ("Golden fixture generator: render and commit the v0.2.0 reference outputs", "[.golden-gen]")
{
    const auto dir = GoldenFixtures::fixtureDirectory();
    REQUIRE (dir.createDirectory().wasOk());

    auto* manifestObject = new juce::DynamicObject();
    manifestObject->setProperty ("format", "apotheosis-golden-fixtures-1");
    manifestObject->setProperty ("engineVersion", "0.2.0 (origin/main @ 8558679)");
    manifestObject->setProperty ("generatedDate", juce::Time::getCurrentTime().toISO8601 (true));
    manifestObject->setProperty ("platform", juce::SystemStats::getOperatingSystemName()
                                              + " / " + juce::SystemStats::getCpuModel());
#if defined (__VERSION__)
    manifestObject->setProperty ("compiler", juce::String (__VERSION__));
#endif
    manifestObject->setProperty ("sampleRate", GoldenFixtures::fixtureSampleRate);
    manifestObject->setProperty ("blockSize", GoldenFixtures::fixtureBlockSize);
    manifestObject->setProperty ("numBlocks", GoldenFixtures::fixtureNumBlocks);
    manifestObject->setProperty ("numChannels", GoldenFixtures::fixtureNumChannels);
    manifestObject->setProperty ("layout", "raw float32, native byte order, channel 0 fully then channel 1");
    manifestObject->setProperty ("corpusSettings", "inputGain +4 dB, ceiling -1 dBTP, release 60 ms, all other controls at defaults");
    manifestObject->setProperty ("ditherFixtureSettings", "all defaults + 16-bit dither, RNG seed 0x5EEDA5D17, silence input");

    juce::Array<juce::var> files;

    for (const auto& spec : GoldenFixtures::allFixtureSpecs())
    {
        const auto rendered = GoldenFixtures::renderFixture (spec);
        const auto bytes = GoldenFixtures::toRawBytes (rendered);
        const auto file = dir.getChildFile (spec.fileName);

        REQUIRE (file.replaceWithData (bytes.getData(), bytes.getSize()));

        auto* fileObject = new juce::DynamicObject();
        fileObject->setProperty ("name", spec.fileName);
        fileObject->setProperty ("signal", spec.description);
        fileObject->setProperty ("sha256", juce::SHA256 (bytes.getData(), bytes.getSize()).toHexString());
        files.add (juce::var (fileObject));
    }

    manifestObject->setProperty ("files", files);

    const auto manifestFile = dir.getChildFile ("manifest.json");
    REQUIRE (manifestFile.replaceWithText (juce::JSON::toString (juce::var (manifestObject)) + "\n"));
}

TEST_CASE ("T1: current engine output at v0.2.0-equivalent settings matches the committed golden fixtures",
           "[regression][golden][guarantee1]")
{
    const auto dir = GoldenFixtures::fixtureDirectory();
    INFO ("fixture directory: " << dir.getFullPathName());
    REQUIRE (dir.isDirectory()); // generated+committed at the branch's first commit - see the harness comment

    // Manifest integrity: every committed fixture must hash to the value
    // recorded at generation time (guards against silent fixture edits or
    // git/filesystem corruption making T1 pass vacuously against a
    // different reference than the one generated at 8558679).
    const auto manifest = juce::JSON::parse (dir.getChildFile ("manifest.json").loadFileAsString());
    REQUIRE (manifest.isObject());

    for (const auto& spec : GoldenFixtures::allFixtureSpecs())
    {
        CAPTURE (spec.fileName, spec.description);

        juce::AudioBuffer<float> fixture;
        REQUIRE (GoldenFixtures::loadFixture (dir.getChildFile (spec.fileName), fixture));

        // Hash check against the manifest entry.
        {
            const auto bytes = GoldenFixtures::toRawBytes (fixture);
            const auto actualHash = juce::SHA256 (bytes.getData(), bytes.getSize()).toHexString();
            juce::String expectedHash;

            for (const auto& entry : *manifest.getProperty ("files", {}).getArray())
                if (entry.getProperty ("name", {}).toString() == spec.fileName)
                    expectedHash = entry.getProperty ("sha256", {}).toString();

            REQUIRE (actualHash == expectedHash);
        }

        const auto rendered = GoldenFixtures::renderFixture (spec);

        // Guard-delay alignment (brief section 3.2): a pure integer delay is
        // NOT an audio-content change, so the comparison shifts by the
        // per-rate constant and compares the overlapping range. The dither
        // fixture is exempt (noise injected after the delay line - see
        // renderFixture()).
        const auto alignment = spec.corpusIndex >= 0 ? GoldenFixtures::fixtureAlignmentSamples() : 0;
        const auto comparableSamples = GoldenFixtures::fixtureNumSamples - alignment;

        int mismatches = 0;
        int firstMismatchChannel = -1;
        int firstMismatchSample = -1;
        double worstAbsoluteDifference = 0.0;

        for (int channel = 0; channel < GoldenFixtures::fixtureNumChannels; ++channel)
        {
            for (int sample = 0; sample < comparableSamples; ++sample)
            {
                const auto expected = fixture.getSample (channel, sample);
                const auto actual = rendered.getSample (channel, sample + alignment);
                const auto difference = std::abs (static_cast<double> (actual) - static_cast<double> (expected));

                const auto matches = GoldenFixtures::onGenerationPlatform ? (actual == expected)
                                                                          : (difference <= 1.0e-7);

                if (! matches)
                {
                    if (mismatches == 0)
                    {
                        firstMismatchChannel = channel;
                        firstMismatchSample = sample;
                    }

                    ++mismatches;
                    worstAbsoluteDifference = juce::jmax (worstAbsoluteDifference, difference);
                }
            }
        }

        CAPTURE (alignment, firstMismatchChannel, firstMismatchSample, worstAbsoluteDifference,
                 GoldenFixtures::onGenerationPlatform);
        CHECK (mismatches == 0);
    }
}
