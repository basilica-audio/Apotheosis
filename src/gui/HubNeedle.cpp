#include "HubNeedle.h"

#include <algorithm>
#include <cmath>

namespace basilica::gui
{
    HubNeedle::HubNeedle (Assets assetsIn, juce::String accessibleTitle,
                          float pivotXFractionIn, float pivotYFractionIn, float spriteSizeFractionIn,
                          float bakedAngleDegIn,
                          float restValueIn, float fullScaleValueIn, float restAngleDegIn, float fullScaleAngleDegIn)
        : assets (std::move (assetsIn)), title (std::move (accessibleTitle)),
          pivotXFraction (pivotXFractionIn), pivotYFraction (pivotYFractionIn),
          spriteSizeFraction (spriteSizeFractionIn), bakedAngleDeg (bakedAngleDegIn),
          restValue (restValueIn), fullScaleValue (fullScaleValueIn),
          restAngleDeg (restAngleDegIn), fullScaleAngleDeg (fullScaleAngleDegIn)
    {
        setTitle (title);
        setDescription (title);

        // Pure display - never steals mouse events from controls that may
        // sit under (or within the bounding box of) this component.
        setInterceptsMouseClicks (false, false);

        // See the header's own docs on this constructor: seed the idle
        // reading to THIS needle's own restValue, not a hardcoded 0.0f.
        targetDb.store (restValue, std::memory_order_relaxed);
        smoothedDb = restValue;
    }

    HubNeedle::~HubNeedle() = default;

    float HubNeedle::linearAngleDegreesForValue (float value, float restValue, float fullScaleValue,
                                                  float restAngleDeg, float fullScaleAngleDeg) noexcept
    {
        const auto lo = std::min (restValue, fullScaleValue);
        const auto hi = std::max (restValue, fullScaleValue);
        const auto clamped = juce::jlimit (lo, hi, value);

        // juce::jmap's own formula (targetMin + (v-srcMin)/(srcMax-srcMin) *
        // (targetMax-targetMin)) is correct regardless of whether
        // restValue < fullScaleValue or the reverse - the fraction
        // (clamped-restValue)/(fullScaleValue-restValue) still lands in
        // [0,1] either way, which is what lets the true-peak-margin needle
        // (restValue=12 > fullScaleValue=0) share this exact function with
        // every other needle (restValue < fullScaleValue) with no special
        // casing.
        return juce::jmap (clamped, restValue, fullScaleValue, restAngleDeg, fullScaleAngleDeg);
    }

    float HubNeedle::angleDegreesForValue (float value) const noexcept
    {
        return linearAngleDegreesForValue (value, restValue, fullScaleValue, restAngleDeg, fullScaleAngleDeg);
    }

    float HubNeedle::stepBallistics (float currentSmoothed, float target, float dtSeconds, float tauSeconds) noexcept
    {
        if (tauSeconds <= 0.0f || dtSeconds <= 0.0f)
            return target;

        const auto alpha = 1.0f - std::exp (-dtSeconds / tauSeconds);
        return currentSmoothed + (target - currentSmoothed) * alpha;
    }

    void HubNeedle::tick (float dtSeconds) noexcept
    {
        const auto target = targetDb.load (std::memory_order_relaxed);
        const auto next = stepBallistics (smoothedDb, target, dtSeconds, ballisticsTauSeconds);

        if (! juce::approximatelyEqual (next, smoothedDb))
        {
            smoothedDb = next;
            repaint();
        }
    }

    void HubNeedle::setImmediateDbForPreview (float db) noexcept
    {
        targetDb.store (db, std::memory_order_relaxed);
        smoothedDb = db;
        repaint();
    }

    void HubNeedle::paint (juce::Graphics& g)
    {
        if (! assets.needleSprite.isValid())
            return;

        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

        const auto bounds = getLocalBounds().toFloat();
        const auto pivotX = bounds.getWidth() * pivotXFraction;
        const auto pivotY = bounds.getHeight() * pivotYFraction;

        const auto spriteDrawSize = spriteSizeFraction * juce::jmin (bounds.getWidth(), bounds.getHeight());
        const auto spriteScale = spriteDrawSize / (float) assets.needleSprite.getWidth();

        const auto targetDeg = angleDegreesForValue (smoothedDb);

        // CRITICAL: rotationToApply = targetDeg - bakedAngleDeg. The sprite's
        // own rod already sits at bakedAngleDeg (its pose in the master
        // render it was cut from) - drawing it with targetDeg's own value as
        // the rotation would double-apply that baked pose. See HubNeedle.h's
        // top-of-file docs.
        const auto rotationToApplyDeg = targetDeg - bakedAngleDeg;
        const auto rotationRadians = juce::degreesToRadians (rotationToApplyDeg);

        const auto imageHalfW = 0.5f * (float) assets.needleSprite.getWidth();
        const auto imageHalfH = 0.5f * (float) assets.needleSprite.getHeight();

        const auto transform = juce::AffineTransform::translation (-imageHalfW, -imageHalfH)
                                    .scaled (spriteScale)
                                    .rotated (rotationRadians)
                                    .translated (pivotX, pivotY);

        g.drawImageTransformed (assets.needleSprite, transform, false);
    }

    // Read-only text value interface exposing the current ballistic-smoothed
    // reading, mirroring basilica-audio/silentium's AnalogMeter::
    // MeterValueInterface (JUCE 8.0.14's own juce::AccessibilityTextValueInterface
    // shape).
    class HubNeedle::ValueInterface final : public juce::AccessibilityTextValueInterface
    {
    public:
        explicit ValueInterface (const HubNeedle& ownerIn) noexcept : owner (ownerIn) {}

        bool isReadOnly() const override { return true; }

        juce::String getCurrentValueAsString() const override
        {
            return juce::String (owner.smoothedDb, 1) + " dB";
        }

        void setValueAsString (const juce::String&) override {}

    private:
        const HubNeedle& owner;
    };

    std::unique_ptr<juce::AccessibilityHandler> HubNeedle::createAccessibilityHandler()
    {
        return std::make_unique<juce::AccessibilityHandler> (
            *this,
            juce::AccessibilityRole::label,
            juce::AccessibilityActions {},
            juce::AccessibilityHandler::Interfaces { std::make_unique<ValueInterface> (*this) });
    }
}
