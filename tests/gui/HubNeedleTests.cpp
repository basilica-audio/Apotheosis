#include "gui/HubNeedle.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

// HubNeedle's ballistic integration and value->angle mapping are pure,
// static functions precisely so they're testable without a running
// juce::Timer/message loop (see HubNeedle.h's docs).
TEST_CASE ("HubNeedle::stepBallistics step response", "[gui]")
{
    using basilica::gui::HubNeedle;

    SECTION ("non-positive dt or tau snaps straight to target (defensive floor, never divides by zero)")
    {
        CHECK (HubNeedle::stepBallistics (-20.0f, 0.0f, 0.0f, 0.25f) == Catch::Approx (0.0f));
        CHECK (HubNeedle::stepBallistics (-20.0f, 0.0f, 1.0f / 30.0f, 0.0f) == Catch::Approx (0.0f));
    }

    SECTION ("repeated stepping monotonically approaches target without overshoot")
    {
        constexpr float tau = HubNeedle::ballisticsTauSeconds;
        constexpr float dt = 1.0f / 30.0f;
        constexpr float target = 3.0f;

        auto smoothed = -20.0f;
        auto previous = smoothed;

        for (int i = 0; i < 300; ++i)
        {
            smoothed = HubNeedle::stepBallistics (smoothed, target, dt, tau);
            CHECK (smoothed >= previous);
            CHECK (smoothed <= target);
            previous = smoothed;
        }

        CHECK (smoothed == Catch::Approx (target).margin (0.01f));
    }
}

// Unlike sibling basilica-audio/aureate's tubecomp dial (a calibrated
// 9-point piecewise table - that design has printed numerals), every
// victorian dial has NO baked numerals (see
// brand/mocks/victorian/prompts.md) - HubNeedle::linearAngleDegreesForValue
// is therefore a plain jmap+clamp, exercised here directly against
// representative per-meter calibrations from PluginEditorLayout.h.
TEST_CASE ("HubNeedle::linearAngleDegreesForValue maps rest/full-scale endpoints exactly and interpolates linearly between them", "[gui]")
{
    using basilica::gui::HubNeedle;

    SECTION ("grand meter (Gain Reduction) calibration: 0 dB -> rest, deep reduction -> full scale")
    {
        // PluginEditorLayout.h: grRestDb=0, grFullScaleReductionDb=-12,
        // grRestAngleDeg=-50, grFullScaleAngleDeg=+40.
        CHECK (HubNeedle::linearAngleDegreesForValue (0.0f, 0.0f, -12.0f, -50.0f, 40.0f) == Catch::Approx (-50.0f));
        CHECK (HubNeedle::linearAngleDegreesForValue (-12.0f, 0.0f, -12.0f, -50.0f, 40.0f) == Catch::Approx (40.0f));
        CHECK (HubNeedle::linearAngleDegreesForValue (-6.0f, 0.0f, -12.0f, -50.0f, 40.0f) == Catch::Approx (-5.0f)); // exact midpoint
    }

    SECTION ("true-peak-margin calibration (restValue > fullScaleValue - a SHRINKING value deflects the needle)")
    {
        // PluginEditorLayout.h: marginRestDb=12 (safe) -> restAngleDeg
        // (calm/left), marginFullScaleDb=0 (at ceiling) -> fullScaleAngleDeg
        // (hot/right) - restValue is numerically LARGER than fullScaleValue
        // here, unlike the grand meter above; the mapping must still work.
        CHECK (HubNeedle::linearAngleDegreesForValue (12.0f, 12.0f, 0.0f, -40.0f, 32.0f) == Catch::Approx (-40.0f));
        CHECK (HubNeedle::linearAngleDegreesForValue (0.0f, 12.0f, 0.0f, -40.0f, 32.0f) == Catch::Approx (32.0f));
        CHECK (HubNeedle::linearAngleDegreesForValue (6.0f, 12.0f, 0.0f, -40.0f, 32.0f) == Catch::Approx (-4.0f)); // exact midpoint
    }

    SECTION ("values beyond either endpoint clamp, never extrapolate")
    {
        CHECK (HubNeedle::linearAngleDegreesForValue (5.0f, 0.0f, -12.0f, -50.0f, 40.0f) == Catch::Approx (-50.0f)); // beyond rest (0 dB GR is the max, can't exceed it)
        CHECK (HubNeedle::linearAngleDegreesForValue (-30.0f, 0.0f, -12.0f, -50.0f, 40.0f) == Catch::Approx (40.0f)); // beyond full scale
        CHECK (HubNeedle::linearAngleDegreesForValue (20.0f, 12.0f, 0.0f, -40.0f, 32.0f) == Catch::Approx (-40.0f)); // beyond margin's rest end
        CHECK (HubNeedle::linearAngleDegreesForValue (-5.0f, 12.0f, 0.0f, -40.0f, 32.0f) == Catch::Approx (32.0f)); // beyond margin's full-scale end
    }

    SECTION ("monotonic across the whole range, in the sweep's own direction")
    {
        auto previous = HubNeedle::linearAngleDegreesForValue (0.0f, 0.0f, -12.0f, -50.0f, 40.0f);

        for (float db = -0.5f; db >= -12.0f; db -= 0.5f)
        {
            const auto next = HubNeedle::linearAngleDegreesForValue (db, 0.0f, -12.0f, -50.0f, 40.0f);
            CHECK (next >= previous);
            previous = next;
        }
    }
}

TEST_CASE ("HubNeedle exposes a read-only, unit-suffixed accessible value", "[gui][a11y]")
{
    basilica::gui::HubNeedle::Assets assets; // deliberately default/invalid image - fine, this test never calls paint()
    basilica::gui::HubNeedle needle (assets, "Gain Reduction meter", 0.5f, 0.5f, 0.65f, -0.018f,
                                     0.0f, -12.0f, -50.0f, 40.0f);

    // createAccessibilityHandler() directly (not getAccessibilityHandler()) -
    // the latter only returns non-null once the component has a live native
    // window peer, which this headless, no-message-loop test binary never
    // has.
    const auto handler = needle.createAccessibilityHandler();
    REQUIRE (handler != nullptr);

    auto* valueInterface = handler->getValueInterface();
    REQUIRE (valueInterface != nullptr);

    CHECK (valueInterface->isReadOnly());

    const auto valueText = valueInterface->getCurrentValueAsString();
    INFO ("HubNeedle accessible value = \"" << valueText.toStdString() << "\"");
    CHECK (valueText.endsWith ("dB"));
}

TEST_CASE ("HubNeedle::setImmediateDbForPreview seeds both target and smoothed reading immediately", "[gui]")
{
    basilica::gui::HubNeedle::Assets assets;
    basilica::gui::HubNeedle needle (assets, "Gain Reduction meter", 0.5f, 0.5f, 0.65f, -0.018f,
                                     0.0f, -12.0f, -50.0f, 40.0f);

    needle.setImmediateDbForPreview (-7.0f);

    const auto handler = needle.createAccessibilityHandler();
    REQUIRE (handler != nullptr);
    auto* valueInterface = handler->getValueInterface();
    REQUIRE (valueInterface != nullptr);

    CHECK (valueInterface->getCurrentValueAsString().getFloatValue() == Catch::Approx (-7.0f));
}

TEST_CASE ("HubNeedle::angleDegreesForValue (instance method) matches the static function with the instance's own stored calibration", "[gui]")
{
    basilica::gui::HubNeedle::Assets assets;
    basilica::gui::HubNeedle needle (assets, "True Peak Margin meter", 0.5f, 0.5f, 0.65f, -4.714f,
                                     12.0f, 0.0f, -40.0f, 32.0f);

    CHECK (needle.angleDegreesForValue (12.0f) == Catch::Approx (-40.0f));
    CHECK (needle.angleDegreesForValue (0.0f) == Catch::Approx (32.0f));
    CHECK (needle.angleDegreesForValue (6.0f)
           == Catch::Approx (basilica::gui::HubNeedle::linearAngleDegreesForValue (6.0f, 12.0f, 0.0f, -40.0f, 32.0f)));
}
