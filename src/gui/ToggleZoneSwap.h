#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Suite-reusable toggle-switch overlay: the design's master render bakes
// every toggle lever in ONE physical position (its "default" pose - see the
// design's own layout-manifest.json/toggles.json provenance) - this helper
// draws a small, pre-cropped "other position" image over that same zone
// when the bound parameter is away from its default, and draws nothing
// (the baked master already shows through) when it's at its default. This
// mirrors basilica-audio/silentium's master-06 crop-swap technique, except
// the crop assets here are already zone-sized (toggle-N-down.png, e.g.
// 122x160px) rather than cut at paint()-time from a second full-plate
// master copy, since the design's own extraction pipeline (toggles.json)
// already produced per-zone crops.
//
// Suite convention (binding for every tubecomp-family plugin sharing this
// design): the master's own baked lever pose is UP, and UP always means
// "this control's own APVTS default value" - so a fresh plugin instance's
// visuals always match its own state with no mismatch/pop at construction,
// regardless of which logical (on/off, or choice index 0/1) value that
// default happens to be. See docs/gui-mapping.md for the concrete per-
// toggle default<->pose table.
namespace basilica::gui
{
    struct ToggleZoneSwap
    {
        // Draws `otherPositionZoneImage` (the pre-cropped "away from
        // default" pose, already sized to this toggle's own zone) into
        // destRectOnScreen when isAwayFromDefault is true; a no-op
        // otherwise (the baseline master, already drawn underneath by the
        // caller, shows through unmodified).
        static void draw (juce::Graphics& g, const juce::Image& otherPositionZoneImage,
                          juce::Rectangle<float> destRectOnScreen, bool isAwayFromDefault)
        {
            if (! isAwayFromDefault || ! otherPositionZoneImage.isValid())
                return;

            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (otherPositionZoneImage, destRectOnScreen, juce::RectanglePlacement::stretchToFit, false);
        }
    };
}
