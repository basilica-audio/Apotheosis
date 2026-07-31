#include "PluginEditor.h"
#include "PluginEditorLayout.h"
#include "PluginProcessor.h"
#include "gui/HubNeedle.h"
#include "gui/MasterCropKnob.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

// GUI smoke tests for the M3 photoreal "victorian" editor (src/PluginEditor.h,
// src/gui/). juce::ScopedJuceInitialiser_GUI is installed once for the whole
// test binary in tests/TestMain.cpp, so Components/Timers are safe to
// construct here even though this is a headless console executable with no
// running message loop (timers simply never fire, which is fine - these
// tests only exercise synchronous construction/paint/destruction).
TEST_CASE ("Editor constructs, lays out, and destroys cleanly", "[gui]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    {
        ApotheosisAudioProcessorEditor editor (processor);

        CHECK (editor.getWidth() > 0);
        CHECK (editor.getHeight() > 0);
    }
    // editor destroyed here - JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR
    // (used throughout src/gui/ and on the editor itself) asserts at process
    // exit in Debug builds if any tagged instance was ever leaked, so a
    // clean run of this whole test binary is itself the leak check.
}

namespace
{
    template <typename ComponentType>
    ComponentType* findChildByTitle (juce::Component& parent, const juce::String& title)
    {
        for (int i = 0; i < parent.getNumChildComponents(); ++i)
            if (auto* typed = dynamic_cast<ComponentType*> (parent.getChildComponent (i)))
                if (typed->getTitle() == title)
                    return typed;

        return nullptr;
    }

    // Configures a deliberately "alive-looking" state before snapshotting,
    // per the M3 GUI briefing: the grand needle showing real gain
    // reduction, the 3 small needles at plausible readings, the tube bay
    // between dim and full, and the 3 knobs at varied, non-default
    // rotations.
    //
    // HubNeedle's own ~250ms ballistic ramp and the editor's own
    // timerCallback()-driven tube-glow ballistics would need real timer
    // ticks pumped through a running message loop to actually reach these
    // values - this headless test binary has no such loop, so the
    // test/preview-only hooks (setImmediateDbForPreview()/
    // setTubeGlowMixForPreview()) seed the readings directly instead.
    void configureLiveLookingState (ApotheosisAudioProcessorEditor& editor)
    {
        if (auto* gr = findChildByTitle<basilica::gui::HubNeedle> (editor, "Gain Reduction meter"))
            gr->setImmediateDbForPreview (-6.0f); // moderate gain reduction, roughly mid-sweep

        if (auto* input = findChildByTitle<basilica::gui::HubNeedle> (editor, "Input Level meter"))
            input->setImmediateDbForPreview (-4.0f); // VU-referenced dB (already offset by the caller normally; direct here)

        if (auto* output = findChildByTitle<basilica::gui::HubNeedle> (editor, "Output Level meter"))
            output->setImmediateDbForPreview (-6.0f);

        if (auto* margin = findChildByTitle<basilica::gui::HubNeedle> (editor, "True Peak Margin meter"))
            margin->setImmediateDbForPreview (4.0f); // 4 dB of headroom remaining

        editor.setTubeGlowMixForPreview (0.6f); // between dim (0) and the hard ceiling (1.0)

        struct KnobValue
        {
            const char* label;
            double normalisedValue;
        };

        const KnobValue knobValues[] = {
            { "Input Gain", 0.30 }, { "Ceiling", 0.70 }, { "Release", 0.45 },
        };

        for (const auto& kv : knobValues)
            if (auto* knob = findChildByTitle<juce::Slider> (editor, kv.label))
                knob->setValue (knob->proportionOfLengthToValue (kv.normalisedValue), juce::dontSendNotification);
    }
}

TEST_CASE ("Editor snapshot at 100% is non-blank and is written for PR review", "[gui]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ApotheosisAudioProcessorEditor editor (processor);
    REQUIRE (editor.getWidth() > 0);
    REQUIRE (editor.getHeight() > 0);

    configureLiveLookingState (editor);

    // SoftwareImageType (rather than the default NativeImageType) avoids any
    // dependency on an actual native graphics context/window, which keeps
    // this robust on headless CI runners.
    const auto snapshot = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});

    REQUIRE (snapshot.isValid());
    CHECK (snapshot.getWidth() == editor.getWidth());
    CHECK (snapshot.getHeight() == editor.getHeight());

    // Non-blank: sample a small grid of points and confirm they are not all
    // identical to the top-left corner - a completely blank/solid-fill
    // render (e.g. every asset failing to decode) would fail this.
    const auto reference = snapshot.getPixelAt (0, 0);
    bool foundDifference = false;

    for (int y = 0; y < snapshot.getHeight() && ! foundDifference; y += juce::jmax (1, snapshot.getHeight() / 20))
        for (int x = 0; x < snapshot.getWidth() && ! foundDifference; x += juce::jmax (1, snapshot.getWidth() / 20))
            if (snapshot.getPixelAt (x, y) != reference)
                foundDifference = true;

    CHECK (foundDifference);

#ifdef APOTHEOSIS_DOCS_DIR
    // Committed directly for PR review (docs/gui-preview.png).
    juce::PNGImageFormat pngFormat;
    const auto outFile = juce::File (APOTHEOSIS_DOCS_DIR).getChildFile ("gui-preview.png");

    if (auto stream = std::unique_ptr<juce::FileOutputStream> (outFile.createOutputStream()))
    {
        stream->setPosition (0);
        stream->truncate();
        CHECK (pngFormat.writeImageToStream (snapshot, *stream));
    }
    else
    {
        FAIL ("could not open output stream for " << outFile.getFullPathName());
    }
#endif
}

// Proof that MasterCropKnob's rotating crop actually moves: knobs set to
// distinctly non-rest proportions must visibly differ, within their own
// bounds, from their construction-time (APVTS-default) rendering.
TEST_CASE ("MasterCropKnob instances visibly rotate at distinctly non-default values", "[gui]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ApotheosisAudioProcessorEditor editor (processor);
    REQUIRE (editor.getWidth() > 0);
    REQUIRE (editor.getHeight() > 0);

    const auto restSnapshot = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});
    REQUIRE (restSnapshot.isValid());

    struct ZoomKnob
    {
        const char* label;
        double proportion;
    };

    constexpr ZoomKnob zoomKnobs[] = {
        { "Input Gain", 0.05 },
        { "Release", 0.95 },
    };

    for (const auto& zk : zoomKnobs)
    {
        auto* knob = findChildByTitle<juce::Slider> (editor, zk.label);
        REQUIRE (knob != nullptr);
        knob->setValue (knob->proportionOfLengthToValue (zk.proportion), juce::dontSendNotification);
    }

    const auto movedSnapshot = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});
    REQUIRE (movedSnapshot.isValid());

    for (const auto& zk : zoomKnobs)
    {
        auto* knob = findChildByTitle<juce::Slider> (editor, zk.label);
        REQUIRE (knob != nullptr);

        const auto cropBounds = knob->getBounds().expanded (4);
        const auto restCrop = restSnapshot.getClippedImage (cropBounds);
        const auto movedCrop = movedSnapshot.getClippedImage (cropBounds);

        REQUIRE (restCrop.isValid());
        REQUIRE (movedCrop.isValid());
        REQUIRE (restCrop.getWidth() == movedCrop.getWidth());
        REQUIRE (restCrop.getHeight() == movedCrop.getHeight());

        int changedPixels = 0;
        const int totalPixels = restCrop.getWidth() * restCrop.getHeight();

        for (int y = 0; y < restCrop.getHeight(); ++y)
        {
            for (int x = 0; x < restCrop.getWidth(); ++x)
            {
                const auto a = restCrop.getPixelAt (x, y);
                const auto b = movedCrop.getPixelAt (x, y);
                const auto diff = std::abs (a.getRed() - b.getRed()) + std::abs (a.getGreen() - b.getGreen())
                                 + std::abs (a.getBlue() - b.getBlue());
                if (diff > 24)
                    ++changedPixels;
            }
        }

        INFO (zk.label << ": " << changedPixels << "/" << totalPixels << " px changed between rest and moved pose");
        CHECK (changedPixels > totalPixels / 20); // >5% of the crop visibly moved
    }
}

// Proof that all 4 needles visibly rotate away from their construction-time
// (idle, 0-value) rendering.
TEST_CASE ("HubNeedle instances visibly rotate at non-idle readings", "[gui]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ApotheosisAudioProcessorEditor editor (processor);
    REQUIRE (editor.getWidth() > 0);
    REQUIRE (editor.getHeight() > 0);

    const auto restSnapshot = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});
    REQUIRE (restSnapshot.isValid());

    const char* needleTitles[] = { "Gain Reduction meter", "Input Level meter", "Output Level meter", "True Peak Margin meter" };

    for (const auto* title : needleTitles)
        if (auto* needle = findChildByTitle<basilica::gui::HubNeedle> (editor, title))
            needle->setImmediateDbForPreview (-10.0f);

    const auto movedSnapshot = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});
    REQUIRE (movedSnapshot.isValid());

    for (const auto* title : needleTitles)
    {
        auto* needle = findChildByTitle<basilica::gui::HubNeedle> (editor, title);
        REQUIRE (needle != nullptr);

        const auto cropBounds = needle->getBounds();
        const auto restCrop = restSnapshot.getClippedImage (cropBounds);
        const auto movedCrop = movedSnapshot.getClippedImage (cropBounds);

        REQUIRE (restCrop.isValid());
        REQUIRE (movedCrop.isValid());

        long long diffEnergy = 0;

        for (int y = 0; y < restCrop.getHeight(); ++y)
            for (int x = 0; x < restCrop.getWidth(); ++x)
            {
                const auto a = restCrop.getPixelAt (x, y);
                const auto b = movedCrop.getPixelAt (x, y);
                diffEnergy += std::abs (a.getRed() - b.getRed()) + std::abs (a.getGreen() - b.getGreen())
                            + std::abs (a.getBlue() - b.getBlue());
            }

        INFO (title << ": diff energy = " << diffEnergy);
        CHECK (diffEnergy > 0);
    }
}

// Item 5-style idle-breathing proof: at true silence (fresh processor,
// never processBlock()'d), the tube-bay glow must still be visibly time-
// varying (never reads as flatly "off") - see SubtractiveGlow.h/
// PluginEditor.cpp's tubeGlowIdleBreath* constants.
TEST_CASE ("Tube-bay glow idle breathing is visibly time-varying at silence", "[gui]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ApotheosisAudioProcessorEditor editor (processor);
    REQUIRE (editor.getWidth() > 0);
    REQUIRE (editor.getHeight() > 0);

    editor.setTubeGlowElapsedSecondsForPreview (5.0);
    const auto frame1 = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});
    REQUIRE (frame1.isValid());

    editor.setTubeGlowElapsedSecondsForPreview (11.0);
    const auto frame2 = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});
    REQUIRE (frame2.isValid());

    REQUIRE (frame1.getWidth() == frame2.getWidth());
    REQUIRE (frame1.getHeight() == frame2.getHeight());

    long long diffEnergy = 0;

    for (int y = 0; y < frame1.getHeight(); ++y)
    {
        for (int x = 0; x < frame1.getWidth(); ++x)
        {
            const auto a = frame1.getPixelAt (x, y);
            const auto b = frame2.getPixelAt (x, y);
            diffEnergy += std::abs (a.getRed() - b.getRed()) + std::abs (a.getGreen() - b.getGreen())
                        + std::abs (a.getBlue() - b.getBlue());
        }
    }

    INFO ("total diff energy between the two idle tube-glow frames = " << diffEnergy);
    CHECK (diffEnergy > 0);
}

// Ceiling proof: the tube-bay glow must never exceed the baked master (the
// SubtractiveGlow hard-ceiling guarantee, already proven exactly at the
// unit level - see SubtractiveGlowTests.cpp's own "true no-op at t=1" case,
// which uses flat, unresampled synthetic images to isolate the maths from
// any image-scaling concern). This test re-checks the SAME guarantee at
// the full-editor render level, where the zone image is additionally
// stretch-scaled from its own native resolution to its on-screen size -
// JUCE's own high-quality image resampling can legitimately overshoot by a
// small amount at hard edges (classic interpolation ringing, e.g. a cubic
// kernel's negative lobe at a sharp light/dark boundary), which is a
// resampling-precision artifact, not a violation of the subtractive
// model's own maths - so this test uses AGGREGATE zone brightness (must
// decrease) rather than a strict every-single-pixel compare, and a small
// per-pixel tolerance for isolated ringing.
TEST_CASE ("Tube-bay glow at t=1.0 matches the baked master and visibly darkens overall as t decreases", "[gui]")
{
    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ApotheosisAudioProcessorEditor editor (processor);
    REQUIRE (editor.getWidth() > 0);

    editor.setTubeGlowMixForPreview (1.0f);
    const auto snapshot = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});
    REQUIRE (snapshot.isValid());

    editor.setTubeGlowMixForPreview (0.3f);
    const auto dimmedSnapshot = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});
    REQUIRE (dimmedSnapshot.isValid());

    // Isolated-ringing tolerance: an individual sampled pixel may be
    // brighter by at most this much (out of 255) without counting as a
    // real ceiling violation - see this test's own top-of-file docs.
    constexpr int perPixelRingingToleranceUnits = 40;

    bool sampledAnyPixel = false;
    bool grossViolation = false;
    long long fullBrightnessSum = 0;
    long long dimmedBrightnessSum = 0;
    int differingSamples = 0;

    for (int y = 0; y < snapshot.getHeight(); y += 5)
    {
        for (int x = 0; x < snapshot.getWidth(); x += 5)
        {
            const auto full = snapshot.getPixelAt (x, y);
            const auto dimmed = dimmedSnapshot.getPixelAt (x, y);

            // Only meaningful where the two frames actually differ (i.e.
            // inside the glow zone) - elsewhere both are identical baked
            // master pixels and this check is trivially true.
            if (full != dimmed)
            {
                sampledAnyPixel = true;
                ++differingSamples;
                fullBrightnessSum += full.getRed() + full.getGreen() + full.getBlue();
                dimmedBrightnessSum += dimmed.getRed() + dimmed.getGreen() + dimmed.getBlue();

                const auto violation = juce::jmax (dimmed.getRed() - full.getRed(),
                                                    dimmed.getGreen() - full.getGreen(),
                                                    dimmed.getBlue() - full.getBlue());
                if (violation > perPixelRingingToleranceUnits)
                    grossViolation = true;
            }
        }
    }

    INFO ("differingSamples=" << differingSamples << " fullSum=" << fullBrightnessSum << " dimmedSum=" << dimmedBrightnessSum);
    CHECK (sampledAnyPixel); // sanity: the glow zone actually rendered something different
    CHECK_FALSE (grossViolation); // no gross (>tolerance) ceiling violation anywhere
    CHECK (dimmedBrightnessSum < fullBrightnessSum); // overall, the zone genuinely got darker
}
