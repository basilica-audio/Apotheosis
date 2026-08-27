#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Engraved lettering drawn live over the baked master render - the suite's
// typography pass (owner decision 2026-07-26: text baked into the AI master
// renders never survived the render loop legibly, so scale numerals, plaque
// lettering and control labels are set locally as a sharp JUCE text layer
// instead, in the suite serif EB Garamond).
//
// Deliberately design-agnostic (the aureate/requiem/tenebrae/apotheosis
// component-family convention, see docs/gui-mapping.md): no BinaryData.h
// include here - the two embedded font faces are passed in as raw
// (data, size) pairs by the editor, so this file is copyable verbatim to
// sibling plugins whose BinaryData namespaces differ.
//
// The engraved read itself: text cut INTO metal shows a dark incision body
// with a thin lit lip on the incision's lower edge (the plate's key light
// comes from above, so it catches the far/lower inner wall of the cut).
// drawEngraved() therefore draws the highlight pass offset DOWN by one
// scaled pixel underneath the ink pass - the inverse of silentium's
// raised gold-on-dark label convention (BasilicaLookAndFeel::drawLabel
// there), which is correct for embossed light-on-dark lettering but reads
// as floating text on a bright brass or steel ground.
namespace basilica::gui
{
    struct EngravedTextStyle
    {
        juce::Colour ink;       // the incision's own dark fill
        juce::Colour highlight; // the lit lower lip of the incision
        float height1x;         // font height at 100% window scale
        float kerning;          // extra tracking, as a fraction of height (FontOptions::withKerningFactor)
        bool semiBold;
    };

    class PlateTypography
    {
    public:
        PlateTypography (const char* regularTtfData, int regularTtfSize,
                         const char* semiBoldTtfData, int semiBoldTtfSize)
            : regular (juce::Typeface::createSystemTypefaceFor (regularTtfData, (size_t) regularTtfSize)),
              semiBold (juce::Typeface::createSystemTypefaceFor (semiBoldTtfData, (size_t) semiBoldTtfSize))
        {
        }

        // The suite serif at a given height, resolved from the embedded
        // faces with a defensive platform-serif fallback (same convention
        // as silentium's BasilicaLookAndFeel::getSerifFont) - a parse
        // failure of the embedded TTF must degrade to legible text, never
        // to silence.
        juce::Font font (float height, bool wantSemiBold, float kerning) const
        {
            const auto& typeface = wantSemiBold ? semiBold : regular;

            if (typeface == nullptr)
                return juce::Font (juce::FontOptions {}
                                       .withName (juce::Font::getDefaultSerifFontName())
                                       .withHeight (height)
                                       .withKerningFactor (kerning));

            return juce::Font (juce::FontOptions { typeface }
                                   .withHeight (height)
                                   .withKerningFactor (kerning));
        }

        // Draws one engraved line, centred in `area` (already in screen
        // pixels). `scale` is the editor's stepped window scale - it sizes
        // both the font and the one-pixel highlight offset, so the engraved
        // read survives the 100/150/200% steps instead of thinning out.
        void drawEngraved (juce::Graphics& g, const juce::String& text,
                           juce::Rectangle<float> area, float scale,
                           const EngravedTextStyle& style) const
        {
            g.setFont (font (style.height1x * scale, style.semiBold, style.kerning));

            const auto lipOffset = juce::jmax (1.0f, scale);

            g.setColour (style.highlight);
            g.drawText (text, area.translated (0.0f, lipOffset), juce::Justification::centred, false);

            g.setColour (style.ink);
            g.drawText (text, area, juce::Justification::centred, false);
        }

    private:
        juce::Typeface::Ptr regular, semiBold;
    };
}
