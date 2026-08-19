#include "MasterCropKnob.h"

#include "KeyboardSteps.h"

#include <cmath>

namespace basilica::gui
{
    MasterCropKnob::MasterCropKnob (const juce::Image& masterImage, juce::Point<float> centreInMasterPx,
                                    float knobRadiusInMasterPx, float contentFraction,
                                    float minAngleDegIn, float maxAngleDegIn)
        : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox),
          cropImage (buildFeatheredCrop (masterImage, centreInMasterPx, knobRadiusInMasterPx, contentFraction)),
          minAngleDeg (minAngleDegIn), maxAngleDeg (maxAngleDegIn)
    {
        setMouseDragSensitivity (normalDragSensitivity);
        setScrollWheelEnabled (true);
        setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);

        // Keyboard navigation (issue #5, WCAG 2.1.1): juce::Slider::init()
        // ships with setWantsKeyboardFocus(false) in JUCE 8.0.14
        // (juce_Slider.cpp:1461) - without opting back in, Tab never
        // reaches the knob, the focus ring in paint() can never show, and
        // keyPressed() below never fires.
        setWantsKeyboardFocus (true);
    }

    MasterCropKnob::~MasterCropKnob() = default;

    float MasterCropKnob::angleForProportionDegrees (double normalisedValue, float minAngleDeg, float maxAngleDeg) noexcept
    {
        const auto clamped = juce::jlimit (0.0, 1.0, normalisedValue);
        return minAngleDeg + (float) clamped * (maxAngleDeg - minAngleDeg);
    }

    juce::Image MasterCropKnob::buildFeatheredCrop (const juce::Image& masterImage, juce::Point<float> centreInMasterPx,
                                                     float radiusPx, float contentFraction, float featherFraction)
    {
        const auto outerRadius = juce::jmax (1.0f, radiusPx * contentFraction);
        const auto innerRadius = outerRadius * (1.0f - juce::jlimit (0.0f, 1.0f, featherFraction));

        // +2px margin beyond 2*outerRadius: without it, the canvas's own
        // outermost pixel ROW sits at radius (outerRadius - 0.5) from the
        // canvas centre (pixel centres, not edges), which is measurably
        // INSIDE outerRadius and therefore never quite reaches alpha=0 - a
        // faint (~4%) sliver would otherwise survive right at the crop's
        // own edge. The margin guarantees a genuinely fully-transparent
        // border ring exists within the canvas.
        const auto canvasSize = juce::jmax (2, (int) std::ceil (outerRadius * 2.0f) + 2);

        juce::Image crop (juce::Image::ARGB, canvasSize, canvasSize, true);

        if (! masterImage.isValid())
            return crop;

        const auto srcLeft = centreInMasterPx.x - (float) canvasSize * 0.5f;
        const auto srcTop = centreInMasterPx.y - (float) canvasSize * 0.5f;
        const auto centrePx = (float) canvasSize * 0.5f;

        juce::Image::BitmapData dst (crop, juce::Image::BitmapData::writeOnly);

        for (int y = 0; y < canvasSize; ++y)
        {
            for (int x = 0; x < canvasSize; ++x)
            {
                const auto dx = (float) x + 0.5f - centrePx;
                const auto dy = (float) y + 0.5f - centrePx;
                const auto r = std::sqrt (dx * dx + dy * dy);

                float alpha;
                if (r <= innerRadius)
                    alpha = 1.0f;
                else if (r >= outerRadius)
                    alpha = 0.0f;
                else
                    alpha = 1.0f - (r - innerRadius) / (outerRadius - innerRadius);

                if (alpha <= 0.0f)
                {
                    dst.setPixelColour (x, y, juce::Colours::transparentBlack);
                    continue;
                }

                const auto srcX = juce::jlimit (0, masterImage.getWidth() - 1, (int) std::floor (srcLeft + (float) x));
                const auto srcY = juce::jlimit (0, masterImage.getHeight() - 1, (int) std::floor (srcTop + (float) y));

                const auto colour = masterImage.getPixelAt (srcX, srcY);
                dst.setPixelColour (x, y, colour.withMultipliedAlpha (alpha));
            }
        }

        return crop;
    }

    void MasterCropKnob::paint (juce::Graphics& g)
    {
        if (! cropImage.isValid())
            return;

        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

        const auto bounds = getLocalBounds().toFloat();
        const auto scale = bounds.getWidth() / (float) cropImage.getWidth();

        const auto angleDeg = angleForProportionDegrees (valueToProportionOfLength (getValue()), minAngleDeg, maxAngleDeg);
        const auto radians = juce::degreesToRadians (angleDeg);

        const auto imageHalfW = (float) cropImage.getWidth() * 0.5f;
        const auto imageHalfH = (float) cropImage.getHeight() * 0.5f;

        const auto transform = juce::AffineTransform::translation (-imageHalfW, -imageHalfH)
                                    .scaled (scale)
                                    .rotated (radians)
                                    .translated (bounds.getCentreX(), bounds.getCentreY());

        g.drawImageTransformed (cropImage, transform);

        // WCAG 2.4.7 Focus Visible: this paint() override fully replaces
        // juce::Slider::paint(), so nothing else in the render path ever
        // draws a keyboard-focus indicator - a minimal, self-contained ring
        // (this component carries no LookAndFeel dependency, to stay
        // portable to sibling plugins on their own toolchains).
        if (hasKeyboardFocus (true))
        {
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.drawEllipse (bounds.reduced (1.0f), 1.5f);
        }
    }

    bool MasterCropKnob::keyPressed (const juce::KeyPress& key)
    {
        return handleSliderKeyPress (*this, key) || juce::Slider::keyPressed (key);
    }

    void MasterCropKnob::mouseDown (const juce::MouseEvent& e)
    {
        setMouseDragSensitivity (e.mods.isShiftDown() ? fineDragSensitivity : normalDragSensitivity);
        Slider::mouseDown (e);
    }

    void MasterCropKnob::mouseDrag (const juce::MouseEvent& e)
    {
        setMouseDragSensitivity (e.mods.isShiftDown() ? fineDragSensitivity : normalDragSensitivity);
        Slider::mouseDrag (e);
    }
}
