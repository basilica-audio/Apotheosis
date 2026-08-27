#include "PluginEditor.h"
#include "PluginEditorLayout.h"
#include "PluginProcessor.h"

#include <BinaryData.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

// Typography-pass proof (suite typo phase, owner decision 2026-07-26): this
// design's dials carry NO baked numerals at all (the render decision that
// removed them is documented in brand/mocks/victorian/prompts.md), and the
// bronze plate carries no baked meter legends or knob labels - all
// lettering is a live JUCE text layer (src/gui/PlateTypography.h, drawn
// last in PluginEditor::paint(), under the needle child components). Three
// proofs: (1) printed numerals/caption darken the parchment dial face
// where the raw master is clean and bright; (2) gilded legends brighten
// the dark bronze bands where the raw master has no bright pixels at all;
// (3) a flat-ground unit render of the one shared glyph draw path. Plus a
// layout invariant keeping knob labels out of the knob hit-areas.
namespace
{
    float fractionBeyond (const juce::Image& image, juce::Rectangle<int> area, int threshold, bool darkerThan)
    {
        int hits = 0, total = 0;

        for (int y = area.getY(); y < area.getBottom(); ++y)
        {
            for (int x = area.getX(); x < area.getRight(); ++x)
            {
                const auto c = image.getPixelAt (x, y);
                const auto lum = (int) std::lround (0.299f * c.getRed() + 0.587f * c.getGreen() + 0.114f * c.getBlue());

                ++total;

                if (darkerThan ? (lum < threshold) : (lum > threshold))
                    ++hits;
            }
        }

        return total > 0 ? (float) hits / (float) total : 0.0f;
    }

    juce::Rectangle<int> toSnapshotRect (juce::Rectangle<float> plateLocal1x)
    {
        return plateLocal1x
            .translated (0.0f, (float) (apth::layout::topStripHeight1x + apth::layout::topStripGap1x))
            .getSmallestIntegerContainer();
    }

    juce::Rectangle<int> toMasterRect (juce::Rectangle<float> plateLocal1x)
    {
        const auto toMaster = (float) apth::layout::masterCanvasWidthPx / (float) apth::layout::plateWidth1x;

        return juce::Rectangle<float> (plateLocal1x.getX() * toMaster, plateLocal1x.getY() * toMaster,
                                       plateLocal1x.getWidth() * toMaster, plateLocal1x.getHeight() * toMaster)
            .getSmallestIntegerContainer();
    }

    // The numeral boxes, computed through the same pivot/radius/jmap
    // derivation PluginEditor.cpp draws with - keeping the test honest to
    // the one production formula rather than a second hand-maintained
    // coordinate list.
    std::vector<juce::Rectangle<float>> numeralBoxes1x()
    {
        using namespace apth::layout;

        constexpr auto masterScale = (float) plateWidth1x / (float) masterCanvasWidthPx;

        const juce::Point<float> pivot1x (
            (float) mainVUNeedle1x.topLeft1x.x + (float) mainVUNeedle1x.componentSize1x * 0.5f,
            (float) mainVUNeedle1x.topLeft1x.y + (float) mainVUNeedle1x.componentSize1x * 0.5f);

        const auto radius1x = grNumeralRadiusMasterPx * masterScale;

        std::vector<juce::Rectangle<float>> boxes;

        for (int i = 0; i < grNumeralCount; ++i)
        {
            const auto db = -grNumeralStepDb * (float) i;
            const auto angleRad = juce::degreesToRadians (
                juce::jmap (db, grRestDb, grFullScaleReductionDb, grRestAngleDeg, grFullScaleAngleDeg));

            const juce::Point<float> centre (pivot1x.x + radius1x * std::sin (angleRad),
                                             pivot1x.y - radius1x * std::cos (angleRad));

            boxes.push_back (juce::Rectangle<float> ((float) grNumeralBoxSize1x, (float) grNumeralBoxSize1x)
                                 .withCentre (centre));
        }

        return boxes;
    }
}

TEST_CASE ("Printed dial numerals and caption darken the clean parchment face", "[gui][typography]")
{
    using namespace apth::layout;

    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ApotheosisAudioProcessorEditor editor (processor);
    REQUIRE (editor.getWidth() > 0);

    const auto snapshot = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});
    REQUIRE (snapshot.isValid());

    const auto master = juce::ImageCache::getFromMemory (BinaryData::master_victorian_png,
                                                         BinaryData::master_victorian_pngSize);
    REQUIRE (master.isValid());

    // Each numeral box sits on parchment (luminance ~150-200 in the raw
    // master; the ink core lands at ~35). Measured against the tightened
    // 16px core box (the production 22px draw box grazes the outermost
    // tick tips, which would contaminate the master-side reading):
    // glyph ink adds 2.7-7% dark coverage below threshold 90, where the
    // clean parchment has 0-1.4%.
    for (const auto& box : numeralBoxes1x())
    {
        const auto core = box.reduced (3.0f);
        const auto snapshotDark = fractionBeyond (snapshot, toSnapshotRect (core), 90, true);
        const auto masterDark = fractionBeyond (master, toMasterRect (core), 90, true);

        CHECK (snapshotDark > masterDark + 0.015f);
    }

    // Caption box, straight up from the pivot (same derivation as the
    // production draw).
    constexpr auto masterScale = (float) plateWidth1x / (float) masterCanvasWidthPx;

    const juce::Point<float> pivot1x (
        (float) mainVUNeedle1x.topLeft1x.x + (float) mainVUNeedle1x.componentSize1x * 0.5f,
        (float) mainVUNeedle1x.topLeft1x.y + (float) mainVUNeedle1x.componentSize1x * 0.5f);

    const auto captionBox = juce::Rectangle<float> ((float) grCaptionWidth1x, (float) grCaptionHeight1x)
                                .withCentre ({ pivot1x.x, pivot1x.y - grCaptionRadiusMasterPx * masterScale });

    const auto snapshotDark = fractionBeyond (snapshot, toSnapshotRect (captionBox), 100, true);
    const auto masterDark = fractionBeyond (master, toMasterRect (captionBox), 100, true);

    CHECK (snapshotDark > masterDark + 0.02f);
}

TEST_CASE ("Gilded legends and knob labels brighten the dark bronze bands", "[gui][typography]")
{
    using namespace apth::layout;

    ApotheosisAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    ApotheosisAudioProcessorEditor editor (processor);
    REQUIRE (editor.getWidth() > 0);

    const auto snapshot = editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f, juce::SoftwareImageType {});
    REQUIRE (snapshot.isValid());

    const auto master = juce::ImageCache::getFromMemory (BinaryData::master_victorian_png,
                                                         BinaryData::master_victorian_pngSize);
    REQUIRE (master.isValid());

    std::vector<juce::Rectangle<float>> boxes;

    // Tightened to the text core (44x10) - the full legend boxes graze
    // each gauge's bezel, whose baked brass highlights would contaminate
    // the master-side reading.
    for (const auto& l : smallMeterLegend1x)
        boxes.push_back ({ (float) l.cx - 22.0f, (float) l.cy - 5.0f, 44.0f, 10.0f });

    for (const auto cx : knobCentreX1x)
        boxes.push_back ({ cx - (float) knobLabelWidth1x * 0.5f,
                           (float) (knobRowY1x + knobDiameter1x / 2 + knobLabelGap1x),
                           (float) knobLabelWidth1x, (float) knobLabelHeight1x });

    // The gold leaf (luminance ~175 at full coverage; the small 9.5px
    // legends render mostly antialiased, so threshold 120 catches their
    // body) is far brighter than the mound/plate grounds it sits on
    // (~50-90 in the raw master; masterBright at 120 stays under 9% even
    // where a bezel highlight grazes a box - measured during this pass).
    for (const auto& box : boxes)
    {
        const auto snapshotBright = fractionBeyond (snapshot, toSnapshotRect (box), 120, false);
        const auto masterBright = fractionBeyond (master, toMasterRect (box), 120, false);

        CHECK (snapshotBright > masterBright + 0.04f);
    }
}

TEST_CASE ("PlateTypography renders glyphs and its offset pass on a flat ground", "[gui][typography]")
{
    // Flat-ground unit proof for the ONE shared draw path every numeral,
    // legend and label goes through.
    basilica::gui::PlateTypography typography (BinaryData::EBGaramondRegular_ttf,
                                               (int) BinaryData::EBGaramondRegular_ttfSize,
                                               BinaryData::EBGaramondSemiBold_ttf,
                                               (int) BinaryData::EBGaramondSemiBold_ttfSize);

    const juce::Colour ground (0xff4a4238); // dark bronze, luminance ~66

    juce::Image canvas (juce::Image::RGB, 160, 24, true);
    {
        juce::Graphics g (canvas);
        g.fillAll (ground);

        // The gilded style: gold main pass, dark offset pass.
        const basilica::gui::EngravedTextStyle style {
            juce::Colour (0xf0d6ad5e), juce::Colour (0x8c000000), 13.0f, 0.14f, true
        };

        typography.drawEngraved (g, "CEILING", canvas.getBounds().toFloat(), 1.0f, style);
    }

    int goldPixels = 0, nonGroundPixels = 0;
    const auto groundLum = 0.299f * ground.getRed() + 0.587f * ground.getGreen() + 0.114f * ground.getBlue();

    for (int y = 0; y < canvas.getHeight(); ++y)
    {
        for (int x = 0; x < canvas.getWidth(); ++x)
        {
            const auto c = canvas.getPixelAt (x, y);
            const auto lum = 0.299f * c.getRed() + 0.587f * c.getGreen() + 0.114f * c.getBlue();

            if (lum > 140.0f)
                ++goldPixels;

            if (std::abs (lum - groundLum) > 8.0f)
                ++nonGroundPixels;
        }
    }

    // 7 semibold capitals at 13px leave a solid body of gold pixels, and
    // the two passes together touch well over 150 pixels. Floors are
    // deliberately cross-platform-loose: the Windows glyph rasterizer
    // renders visibly thinner coverage than macOS for the same face/
    // height (~48 vs ~100 gold pixels here, CI run 33028029166 - which
    // also blends the one-pixel shadow fringe above any usable dark
    // threshold, so shadow presence is asserted via nonGroundPixels
    // rather than a dark-pixel count).
    CHECK (goldPixels > 30);
    CHECK (nonGroundPixels > 150);
}

TEST_CASE ("Lettering never intrudes into a knob's interactive hit-area", "[gui][typography]")
{
    using namespace apth::layout;

    const auto sliderBottom = knobRowY1x + knobDiameter1x / 2;
    const auto labelTop = knobRowY1x + knobDiameter1x / 2 + knobLabelGap1x;

    CHECK (labelTop > sliderBottom);
    CHECK (labelTop + knobLabelHeight1x < plateHeight1x);
}
