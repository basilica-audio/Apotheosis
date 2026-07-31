#include "PluginEditorLayout.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

// Layout-invariant tests for the M3 photoreal "victorian" GUI - asserted
// directly against the same apth::layout constants PluginEditor.cpp lays
// components out with, so this test file and the actual layout can never
// silently drift apart (same discipline as the suite pilot,
// basilica-audio/aureate's own EditorLayoutTests.cpp).
TEST_CASE ("The 4 needle components and the tube-bay zone stay within the plate's own canvas bounds", "[gui][layout]")
{
    using namespace apth::layout;

    const juce::Rectangle<int> plateCanvas { 0, 0, plateWidth1x, plateHeight1x };

    for (const auto& geom : { mainVUNeedle1x, smallMeterTopNeedle1x, smallMeterMidNeedle1x, smallMeterBottomNeedle1x })
        CHECK (plateCanvas.contains (juce::Rectangle<int> (geom.topLeft1x.x, geom.topLeft1x.y, geom.componentSize1x, geom.componentSize1x)));

    CHECK (plateCanvas.contains (tubeBayZone1x));
}

TEST_CASE ("Needle sprite geometry is well-formed for all 4 needles", "[gui][layout]")
{
    using namespace apth::layout;

    for (const auto& geom : { mainVUNeedle1x, smallMeterTopNeedle1x, smallMeterMidNeedle1x, smallMeterBottomNeedle1x })
    {
        CHECK (geom.componentSize1x > 0);
        CHECK (geom.spriteSizeFraction > 0.0f);
        CHECK (geom.spriteSizeFraction < 1.0f); // sprite must not exceed its own component's bounds
    }
}

TEST_CASE ("The 3 small needles share one sweep and the grand needle has its own wider sweep", "[gui][layout]")
{
    using namespace apth::layout;

    // Grand meter (Gain Reduction) sweep is wider than the shared small-
    // meter sweep - a deliberate design choice (the grand dial is
    // physically larger and reads a wider dynamic range), not an oversight.
    CHECK (grFullScaleAngleDeg - grRestAngleDeg > smallMeterFullScaleAngleDeg - smallMeterRestAngleDeg);

    // Suite-wide convention (PluginEditorLayout.h's own docs): rest sits at
    // the negative (left) end, full-scale at the positive (right, red-zone)
    // end - for every sweep, regardless of which native-unit direction
    // maps onto it.
    CHECK (grRestAngleDeg < grFullScaleAngleDeg);
    CHECK (smallMeterRestAngleDeg < smallMeterFullScaleAngleDeg);
}

TEST_CASE ("Knob row geometry is well-formed and the 3 knobs are horizontally distinct", "[gui][layout]")
{
    using namespace apth::layout;

    CHECK (knobDiameter1x > 0);
    CHECK (knobRowY1x > 0);

    for (size_t i = 0; i < knobCentreX1x.size(); ++i)
        for (size_t j = i + 1; j < knobCentreX1x.size(); ++j)
            CHECK (std::abs (knobCentreX1x[i] - knobCentreX1x[j]) >= (float) knobDiameter1x);
}

TEST_CASE ("Tube-glow ceiling matches the grand needle's own full-scale reduction depth", "[gui][layout]")
{
    using namespace apth::layout;

    // Documented coupling (docs/gui-mapping.md's "Tube bay behaviour"
    // section): the tubes reach their hard t=1.0 ceiling at the exact same
    // gain-reduction depth the grand needle itself pins at - asserted here
    // as a structural invariant, not just prose.
    CHECK (tubeGlowCeilingDb == -grFullScaleReductionDb);
    CHECK (tubeGlowIdleBreathCentre > 0.0f);
    CHECK (tubeGlowIdleBreathCentre < 1.0f);
    CHECK (tubeGlowIdleBreathHalfRange > 0.0f);
}
