#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Suite-reusable "amber tube vent" glow overlay, following the tubecomp
// design's own measured SUBTRACTIVE runtime model (see
// brand/mocks/tubecomp/components/vent-glow.json's "runtimeModel" block,
// citied verbatim below): the glow was derived as (base - registered dim),
// so the base master ITSELF is the t=1 ("full glow") frame - there is no
// separate "hot" asset to composite in, which is what makes it structurally
// impossible for this layer to ever render brighter than the baked master
// (the suite's standing "hard ceiling" rule for these breathing/flicker
// overlays).
//
//   frame = clamp(base - glow_rgb*(alpha/255)*additiveGain*(1-t)), t in [0,1]
//   t=1 -> identical to the base master; t=0 -> fully dim
//
// This component precomputes the t=0 ("fully dim") frame ONCE at
// construction (message-thread only - real per-pixel work, never touched
// from paint()/the audio thread), then draws it at opacity (1-t) on top of
// whatever the caller already painted for that same screen region: because
// that precomputed image is itself `base - fullSubtraction`, alpha-blending
// it over the base at opacity (1-t) is an EXACT linear reproduction of the
// formula above (base*t + (base-fullSubtraction)*(1-t) == base -
// fullSubtraction*(1-t)) - no separate per-frame pixel math needed.
namespace basilica::gui
{
    class SubtractiveGlow
    {
    public:
        SubtractiveGlow() = default;

        // masterImage: the design's full master render (the t=1 frame).
        // glowImage: the design's own glow diff sprite (e.g.
        // vent_glow_tubecomp.png) - RGB is the light's own hue (colour-
        // normalised so its brightest channel is 255), alpha is the diff
        // magnitude, per vent-glow.json's own "convention" field.
        // glowOffsetInMasterPx: where glowImage's own (0,0) sits within
        // masterImage's coordinate space (vent-glow.json's offsetX/offsetY).
        SubtractiveGlow (const juce::Image& masterImage, const juce::Image& glowImage,
                         juce::Point<int> glowOffsetInMasterPx, float additiveGain = 1.0f);

        // Draws the precomputed fully-dim frame at opacity (1-t) into
        // destRectOnScreen - a true no-op at t>=1 (0 alpha, so the caller's
        // own already-drawn base master shows through completely unmodified
        // - the hard ceiling). destRectOnScreen should correspond to this
        // layer's own footprint (glowOffsetInMasterPx sized by glowImage's
        // own width/height) at the caller's current on-screen scale.
        void drawZone (juce::Graphics& g, juce::Rectangle<float> destRectOnScreen, float t) const;

        bool isValid() const noexcept { return dimImage.isValid(); }

    private:
        juce::Image dimImage; // sized exactly to the glow layer's own canvas (glowImage's width/height)
    };

    // Idle-breathing + signal-driven mix state, ballistically smoothed
    // (see stepGlowMix()) - one instance per independently-flickering glow
    // zone.
    struct GlowMixState
    {
        float smoothedDb = -100.0f;
        double startTimeSeconds = 0.0;
    };

    // Suite-standard breathing ballistics: ballistic-smooths
    // instantaneousDb (tauSeconds), maps it across [floorDb, ceilingDb] to a
    // signal-driven push in [0,1], and ADDS an unconditional slow multi-sine
    // idle wander (Flicker.h's slowDriftLayers, own phaseSeed) centred on
    // idleCentre with a peak deviation of idleHalfRange - so the glow
    // visibly breathes even at total silence (Basilica Audio's suite-wide
    // "item 5" rule: idle flicker must never read as "off"). The final
    // jlimit(0,1) is the SAME hard ceiling drawZone()'s own t>=1 no-op
    // relies on - this can never signal a brighter-than-baked frame.
    float stepGlowMix (GlowMixState& state, float instantaneousDb, float dtSeconds, double nowSeconds,
                       float tauSeconds, float floorDb, float ceilingDb,
                       float idleCentre, float idleHalfRange, float phaseSeed) noexcept;
}
