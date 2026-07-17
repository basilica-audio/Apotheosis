#include "PluginEditorLayout.h"

#include <catch2/catch_test_macros.hpp>

// Layout-invariant tests asserted directly against the same apth::layout
// constants PluginEditor.cpp lays components out with (see
// PluginEditorLayout.h), so this test file and the actual layout can never
// silently drift apart - same discipline as the M3 pilot's
// EditorLayoutTests.cpp (basilica-audio/silentium).
TEST_CASE ("Meters bay starts at or below the header bay's bottom edge", "[gui][layout]")
{
    using namespace apth::layout;

    CHECK (metersBay1x.getY() >= headerBay1x.getBottom());
}

TEST_CASE ("The three knob-bearing bays start at or below the meters bay's bottom edge", "[gui][layout]")
{
    using namespace apth::layout;

    CHECK (limiterBay1x.getY() >= metersBay1x.getBottom());
    CHECK (releaseBay1x.getY() >= metersBay1x.getBottom());
    CHECK (outputBay1x.getY() >= metersBay1x.getBottom());
}

TEST_CASE ("The three knob-bearing bays share the same top and bottom edge", "[gui][layout]")
{
    using namespace apth::layout;

    CHECK (limiterBay1x.getY() == releaseBay1x.getY());
    CHECK (releaseBay1x.getY() == outputBay1x.getY());
    CHECK (limiterBay1x.getBottom() == releaseBay1x.getBottom());
    CHECK (releaseBay1x.getBottom() == outputBay1x.getBottom());
}

TEST_CASE ("The three knob-bearing bays do not horizontally overlap, left to right", "[gui][layout]")
{
    using namespace apth::layout;

    CHECK (limiterBay1x.getRight() <= releaseBay1x.getX());
    CHECK (releaseBay1x.getRight() <= outputBay1x.getX());
}

TEST_CASE ("Header bay starts at or below the plate's top edge", "[gui][layout]")
{
    using namespace apth::layout;

    CHECK (headerBay1x.getY() >= 0);
}

TEST_CASE ("Limiter and output bay grid cells are tall enough for a label plus a full-diameter knob with no overlap", "[gui][layout]")
{
    using namespace apth::layout;

    // Single-row bays (limiter/output): the whole bay height is one cell.
    CHECK (limiterBay1x.getHeight() - knobLabelHeight1x >= knobDiameter1x);
    CHECK (outputBay1x.getHeight() - knobLabelHeight1x >= knobDiameter1x);
}

TEST_CASE ("Release bay grid cells are tall enough for a label plus a full-diameter knob with no overlap", "[gui][layout]")
{
    using namespace apth::layout;

    const auto cellH = releaseBay1x.getHeight() / releaseRows;
    CHECK (cellH - knobLabelHeight1x >= knobDiameter1x);
}

TEST_CASE ("Every laid-out bay stays within the plate's own canvas bounds", "[gui][layout]")
{
    using namespace apth::layout;

    const juce::Rectangle<int> plateCanvas { 0, 0, plateWidth1x, plateHeight1x };

    for (const auto& bay : { headerBay1x, metersBay1x, limiterBay1x, releaseBay1x, outputBay1x })
        CHECK (plateCanvas.contains (bay));
}

TEST_CASE ("The header roundel sits within the header bay's vertical span", "[gui][layout]")
{
    using namespace apth::layout;

    CHECK (roundelCentre1x.y >= headerBay1x.getY() - roundelRadius1x);
    CHECK (roundelCentre1x.y <= headerBay1x.getBottom() + roundelRadius1x);
}
