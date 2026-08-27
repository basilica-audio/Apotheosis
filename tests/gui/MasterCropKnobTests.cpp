#include "gui/MasterCropKnob.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE ("MasterCropKnob::angleForProportionDegrees maps proportion 0.5 to 0deg (12 o'clock)", "[gui]")
{
    using basilica::gui::MasterCropKnob;

    CHECK (MasterCropKnob::angleForProportionDegrees (0.5, -135.0f, 135.0f) == Catch::Approx (0.0f));
    CHECK (MasterCropKnob::angleForProportionDegrees (0.0, -135.0f, 135.0f) == Catch::Approx (-135.0f));
    CHECK (MasterCropKnob::angleForProportionDegrees (1.0, -135.0f, 135.0f) == Catch::Approx (135.0f));

    SECTION ("out-of-range proportions clamp rather than extrapolate")
    {
        CHECK (MasterCropKnob::angleForProportionDegrees (-0.5, -135.0f, 135.0f) == Catch::Approx (-135.0f));
        CHECK (MasterCropKnob::angleForProportionDegrees (1.5, -135.0f, 135.0f) == Catch::Approx (135.0f));
    }

    SECTION ("monotonically increasing across the whole range")
    {
        auto previous = MasterCropKnob::angleForProportionDegrees (0.0, -135.0f, 135.0f);
        for (double p = 0.05; p <= 1.0; p += 0.05)
        {
            const auto next = MasterCropKnob::angleForProportionDegrees (p, -135.0f, 135.0f);
            CHECK (next > previous);
            previous = next;
        }
    }
}

namespace
{
    // A flat-coloured synthetic "master" so buildFeatheredCrop()'s alpha
    // ramp can be asserted independently of any real artwork.
    juce::Image makeFlatMaster (int size, juce::Colour colour)
    {
        juce::Image image (juce::Image::ARGB, size, size, true);
        juce::Graphics g (image);
        g.fillAll (colour);
        return image;
    }
}

TEST_CASE ("MasterCropKnob::buildFeatheredCrop is opaque at the centre and fully transparent past the crop radius", "[gui]")
{
    using basilica::gui::MasterCropKnob;

    constexpr int masterSize = 400;
    constexpr float radiusPx = 100.0f;
    constexpr float contentFraction = 0.94f;
    constexpr float featherFraction = 0.12f;

    const auto master = makeFlatMaster (masterSize, juce::Colours::orange);
    const auto crop = MasterCropKnob::buildFeatheredCrop (
        master, juce::Point<float> ((float) masterSize * 0.5f, (float) masterSize * 0.5f),
        radiusPx, contentFraction, featherFraction);

    REQUIRE (crop.isValid());

    const auto outerRadius = radiusPx * contentFraction;
    const auto innerRadius = outerRadius * (1.0f - featherFraction);
    const auto centreXY = crop.getWidth() / 2;

    SECTION ("canvas is sized to exactly 2x the outer (94%) radius")
    {
        // Exact, not approximate: buildFeatheredCrop sizes its canvas
        // deterministically to ceil(2 * outerRadius) + 2 - the +2 px
        // fully-transparent border ring documented at the definition. Here
        // outerRadius = radiusPx * contentFraction = 100 * 0.94 = 94 px, so
        // the canvas is ceil(188) + 2 = 190 px square.
        CHECK (crop.getWidth() == 190);
        CHECK (crop.getHeight() == crop.getWidth());
    }

    SECTION ("fully opaque at the crop's own centre")
    {
        CHECK (crop.getPixelAt (centreXY, centreXY).getAlpha() == 255);
    }

    SECTION ("fully opaque everywhere inside the inner (feather-start) radius")
    {
        const auto x = centreXY + (int) (innerRadius * 0.5f);
        CHECK (crop.getPixelAt (x, centreXY).getAlpha() == 255);
    }

    SECTION ("alpha ramps down (not a hard cutoff) between the inner and outer radius")
    {
        const auto midFeatherX = centreXY + (int) ((innerRadius + outerRadius) * 0.5f);
        const auto alpha = crop.getPixelAt (midFeatherX, centreXY).getAlpha();
        INFO ("alpha at feather midpoint = " << (int) alpha);
        CHECK (alpha > 0);
        CHECK (alpha < 255);
    }

    SECTION ("fully transparent at/past the crop's own edge - never reaches the knob's baked outer rim")
    {
        CHECK (crop.getPixelAt (0, centreXY).getAlpha() == 0);
        CHECK (crop.getPixelAt (crop.getWidth() - 1, centreXY).getAlpha() == 0);
    }

    SECTION ("opaque region carries the master's own colour, unmodified in hue")
    {
        const auto centrePixel = crop.getPixelAt (centreXY, centreXY);
        CHECK (centrePixel.getRed() == juce::Colours::orange.getRed());
        CHECK (centrePixel.getGreen() == juce::Colours::orange.getGreen());
        CHECK (centrePixel.getBlue() == juce::Colours::orange.getBlue());
    }
}

TEST_CASE ("MasterCropKnob constructs and lays out cleanly from a real master crop", "[gui]")
{
    const auto master = makeFlatMaster (200, juce::Colours::grey);

    basilica::gui::MasterCropKnob knob (master, juce::Point<float> (100.0f, 100.0f), 50.0f);
    knob.setBounds (0, 0, 60, 60);

    CHECK (knob.getWidth() == 60);
    // Real-time-safety/leak sanity: JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR
    // asserts at process exit in Debug builds if any tagged instance is ever
    // leaked - a clean run of this whole test binary is itself the leak
    // check for the crop image + component teardown.
}
