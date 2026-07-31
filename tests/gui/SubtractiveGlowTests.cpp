#include "gui/SubtractiveGlow.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{
    juce::Image makeFlatImage (int w, int h, juce::Colour colour)
    {
        juce::Image image (juce::Image::ARGB, w, h, true);
        juce::Graphics g (image);
        g.fillAll (colour);
        return image;
    }
}

// The runtime model (vent-glow.json's own "runtimeModel" block, cited
// verbatim in SubtractiveGlow.h) guarantees t=1 is IDENTICAL to the base
// master - the hard ceiling this overlay can never exceed, because there is
// no brighter frame to draw. drawZone() implements this as a true no-op
// (zero alpha) at t>=1.
TEST_CASE ("SubtractiveGlow::drawZone is a true no-op at t=1 (never brighter than the baked master)", "[gui]")
{
    constexpr int size = 40;
    const auto master = makeFlatImage (size, size, juce::Colours::orange);
    // A fully-opaque, maximally-bright white glow layer - the worst case for
    // an accidental ceiling violation.
    const auto glow = makeFlatImage (size, size, juce::Colours::white);

    basilica::gui::SubtractiveGlow subtractiveGlow (master, glow, { 0, 0 }, 1.0f);
    REQUIRE (subtractiveGlow.isValid());

    juce::Image canvas (juce::Image::ARGB, size, size, true);
    {
        juce::Graphics g (canvas);
        g.drawImageAt (master, 0, 0); // caller's own baseline draw, as the real editor does
        subtractiveGlow.drawZone (g, juce::Rectangle<float> (0.0f, 0.0f, (float) size, (float) size), 1.0f);
    }

    const auto pixel = canvas.getPixelAt (size / 2, size / 2);
    CHECK (pixel.getRed() == juce::Colours::orange.getRed());
    CHECK (pixel.getGreen() == juce::Colours::orange.getGreen());
    CHECK (pixel.getBlue() == juce::Colours::orange.getBlue());
}

TEST_CASE ("SubtractiveGlow::drawZone darkens the base at t=0 (fully dim)", "[gui]")
{
    constexpr int size = 40;
    const auto master = makeFlatImage (size, size, juce::Colours::white);
    const auto glow = makeFlatImage (size, size, juce::Colours::white); // fully opaque -> full subtraction

    basilica::gui::SubtractiveGlow subtractiveGlow (master, glow, { 0, 0 }, 1.0f);
    REQUIRE (subtractiveGlow.isValid());

    juce::Image canvas (juce::Image::ARGB, size, size, true);
    {
        juce::Graphics g (canvas);
        g.drawImageAt (master, 0, 0);
        subtractiveGlow.drawZone (g, juce::Rectangle<float> (0.0f, 0.0f, (float) size, (float) size), 0.0f);
    }

    const auto pixel = canvas.getPixelAt (size / 2, size / 2);
    INFO ("t=0 pixel = " << pixel.toDisplayString (false).toStdString());
    CHECK (pixel.getRed() < 255);
}

TEST_CASE ("stepGlowMix breathes around the idle centre at silence and never exceeds 1.0", "[gui]")
{
    basilica::gui::GlowMixState state;

    for (double t = 0.0; t < 40.0; t += 1.0)
    {
        const auto mix = basilica::gui::stepGlowMix (state, /*instantaneousDb*/ -100.0f, 1.0f / 30.0f, t,
                                                      0.15f, 0.0f, 6.0f, 0.85f, 0.06f, 5.0f);
        CHECK (mix >= 0.0f);
        CHECK (mix <= 1.0f);
        // At true silence, signalPush is 0 - mix should stay within the
        // idle-breathing band (0.85 +/- 0.06, with a small margin for the
        // ballistic smoother's own settling).
        CHECK (mix >= Catch::Approx (0.85f - 0.06f).margin (0.02f));
        CHECK (mix <= Catch::Approx (0.85f + 0.06f).margin (0.02f));
    }
}

TEST_CASE ("stepGlowMix rises toward the 1.0 ceiling under sustained signal", "[gui]")
{
    basilica::gui::GlowMixState state;
    state.smoothedDb = 6.0f; // pre-settled at the ceiling dB, avoids conflating ballistics with the ceiling assertion

    float mix = 0.0f;
    for (double t = 0.0; t < 5.0; t += 1.0)
        mix = basilica::gui::stepGlowMix (state, /*instantaneousDb*/ 6.0f, 1.0f / 30.0f, t,
                                          0.15f, 0.0f, 6.0f, 0.85f, 0.06f, 5.0f);

    CHECK (mix <= 1.0f);
    CHECK (mix > 0.9f);
}
