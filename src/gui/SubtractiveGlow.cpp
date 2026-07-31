#include "SubtractiveGlow.h"
#include "Flicker.h"

#include <cmath>

namespace basilica::gui
{
    SubtractiveGlow::SubtractiveGlow (const juce::Image& masterImage, const juce::Image& glowImage,
                                      juce::Point<int> glowOffsetInMasterPx, float additiveGain)
    {
        if (! masterImage.isValid() || ! glowImage.isValid())
            return;

        const auto w = glowImage.getWidth();
        const auto h = glowImage.getHeight();

        dimImage = juce::Image (juce::Image::ARGB, w, h, false);
        juce::Image::BitmapData dst (dimImage, juce::Image::BitmapData::writeOnly);

        const auto subtractChannel = [additiveGain] (juce::uint8 baseChannel, juce::uint8 glowChannel, float alphaFraction)
        {
            const auto value = (float) baseChannel - (float) glowChannel * alphaFraction * additiveGain;
            return (juce::uint8) juce::jlimit (0.0f, 255.0f, value);
        };

        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                const auto glow = glowImage.getPixelAt (x, y);
                const auto alphaFraction = (float) glow.getAlpha() / 255.0f;

                const auto srcX = juce::jlimit (0, masterImage.getWidth() - 1, glowOffsetInMasterPx.x + x);
                const auto srcY = juce::jlimit (0, masterImage.getHeight() - 1, glowOffsetInMasterPx.y + y);
                const auto base = masterImage.getPixelAt (srcX, srcY);

                const auto dimR = subtractChannel (base.getRed(), glow.getRed(), alphaFraction);
                const auto dimG = subtractChannel (base.getGreen(), glow.getGreen(), alphaFraction);
                const auto dimB = subtractChannel (base.getBlue(), glow.getBlue(), alphaFraction);

                dst.setPixelColour (x, y, juce::Colour::fromRGB (dimR, dimG, dimB));
            }
        }
    }

    void SubtractiveGlow::drawZone (juce::Graphics& g, juce::Rectangle<float> destRectOnScreen, float t) const
    {
        const auto dimAlpha = juce::jlimit (0.0f, 1.0f, 1.0f - t);

        if (! isValid() || dimAlpha <= 0.001f)
            return;

        juce::Graphics::ScopedSaveState saveState (g);
        g.setOpacity (dimAlpha);
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (dimImage, destRectOnScreen, juce::RectanglePlacement::stretchToFit, false);
    }

    float stepGlowMix (GlowMixState& state, float instantaneousDb, float dtSeconds, double nowSeconds,
                       float tauSeconds, float floorDb, float ceilingDb,
                       float idleCentre, float idleHalfRange, float phaseSeed) noexcept
    {
        if (tauSeconds > 0.0f && dtSeconds > 0.0f)
        {
            const auto alpha = 1.0f - std::exp (-dtSeconds / tauSeconds);
            state.smoothedDb += (instantaneousDb - state.smoothedDb) * alpha;
        }
        else
        {
            state.smoothedDb = instantaneousDb;
        }

        const auto signalPush = juce::jlimit (0.0f, 1.0f, juce::jmap (state.smoothedDb, floorDb, ceilingDb, 0.0f, 1.0f));

        const auto idleWanderUnit = flickerMultiplier (nowSeconds, state.startTimeSeconds, phaseSeed, 1.0f, slowDriftLayers) - 1.0f;
        const auto idleBreath = idleCentre + idleHalfRange * idleWanderUnit;

        return juce::jlimit (0.0f, 1.0f, idleBreath + signalPush);
    }
}
