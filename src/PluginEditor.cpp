#include "PluginEditor.h"
#include "PluginEditorLayout.h"
#include "PluginProcessor.h"
#include "gui/ImageDensity.h"
#include "params/ParameterIds.h"
#include "presets/Localisation.h"

#include <BinaryData.h>

namespace
{
    // Base (@1x, 100% scale) faceplate geometry lives in PluginEditorLayout.h
    // (apth::layout) rather than here, so tests/gui/EditorLayoutTests.cpp can
    // assert layout invariants against the exact constants this file lays
    // components out with - see that header's docs.
    using namespace apth::layout;

    // Which of the three knob-bearing bays a control lives in - matches
    // layout-manifest.json's "controls" list per bay 1:1.
    enum class Bay
    {
        limiter,
        release,
        output
    };

    juce::Rectangle<int> bayRectFor (Bay bay)
    {
        switch (bay)
        {
            case Bay::limiter: return limiterBay1x;
            case Bay::release: return releaseBay1x;
            case Bay::output:  return outputBay1x;
        }

        return {};
    }

    struct BayGrid
    {
        int cols;
        int rows;
    };

    BayGrid bayGridFor (Bay bay)
    {
        switch (bay)
        {
            case Bay::limiter: return { limiterCols, 1 };
            case Bay::release: return { releaseCols, releaseRows };
            case Bay::output:  return { outputCols, 1 };
        }

        return { 1, 1 };
    }

    struct KnobLayoutEntry
    {
        const char* parameterId;
        const char* labelText;
        Bay bay;
        int col;
        int row;
        // Lookahead is prepare-time-latched (see ParameterIds.h::lookahead)
        // - it gets a visually distinct "setup" treatment rather than
        // reading as a live performance knob (design-brief note from the
        // faceplate README).
        bool isSetupParam;
    };

    // Signal-flow-grouped within each bay, matching ParameterLayout.cpp's
    // own ordering: limiter bay = the level core (Input Gain, Ceiling, Clip
    // Mix); release bay = all time-domain behaviour (Attack, Release, Auto
    // Release, Lookahead) plus the Release Curve choice (see choiceLayout
    // below - it shares the release bay's grid); output bay = detection
    // linking (Stereo Link).
    constexpr std::array<KnobLayoutEntry, 8> knobLayout {
        KnobLayoutEntry { ParamIDs::inputGain, "Input Gain", Bay::limiter, 0, 0, false },
        KnobLayoutEntry { ParamIDs::ceiling, "Ceiling", Bay::limiter, 1, 0, false },
        KnobLayoutEntry { ParamIDs::clipMix, "Clip Mix", Bay::limiter, 2, 0, false },
        KnobLayoutEntry { ParamIDs::attack, "Attack", Bay::release, 0, 0, false },
        KnobLayoutEntry { ParamIDs::release, "Release", Bay::release, 1, 0, false },
        KnobLayoutEntry { ParamIDs::autoRelease, "Auto Release", Bay::release, 2, 0, false },
        KnobLayoutEntry { ParamIDs::lookahead, "Lookahead", Bay::release, 0, 1, true },
        KnobLayoutEntry { ParamIDs::stereoLink, "Stereo Link", Bay::output, 0, 0, false },
    };

    struct ChoiceLayoutEntry
    {
        const char* parameterId;
        const char* labelText;
        Bay bay;
        int col;
        int row;
    };

    constexpr std::array<ChoiceLayoutEntry, 3> choiceLayout {
        // Shares the release bay's 3x2 grid with the knobs above - sits at
        // (col 1, row 1), next to Lookahead's (col 0, row 1); (col 2, row 1)
        // is the grid's one deliberately unused cell (5 controls in a 6-cell
        // grid), matching layout-manifest.json's release bay note.
        ChoiceLayoutEntry { ParamIDs::releaseCurve, "Release Curve", Bay::release, 1, 1 },
        // Output bay is a bespoke two-row layout, not the generic grid -
        // col here means "left half" (0) / "right half" (1) of the bay's
        // bottom row, handled as a special case in resized() - see
        // PluginEditorLayout.h's outputCols docs.
        ChoiceLayoutEntry { ParamIDs::dither, "Dither", Bay::output, 0, 0 },
        ChoiceLayoutEntry { ParamIDs::ditherShape, "Dither Shape", Bay::output, 1, 0 },
    };

    juce::Image loadImage (const char* data, int size)
    {
        return juce::ImageCache::getFromMemory (data, size);
    }

    // M2 i18n frame (.scaffold/specs/preset-system-m2.md): selects German
    // (resources/i18n/de.txt) or falls through to English, once, at editor
    // construction - see Localisation.h's docs and silentium's
    // PluginEditor.cpp for the full ordering rationale (member initialisers
    // run in declaration order, so this helper - invoked from presetBar's
    // own initialiser expression - is what guarantees installLocalisation()
    // runs before presetBar exists).
    basilica::presets::PresetManager& initLocalisationThenGetPresetManager (ApotheosisAudioProcessor& processor)
    {
        basilica::presets::installLocalisation (BinaryData::de_txt, BinaryData::de_txtSize);
        return processor.presetManager;
    }

    // Non-parameter, per-session UI state: the stepped scale choice (0/1/2)
    // stored as a plain property directly on apvts.state - see silentium's
    // PluginEditor.cpp for why this deliberately isn't a registered
    // parameter.
    constexpr const char* uiScaleStepProperty = "uiScaleStep";

    basilica::gui::AnalogMeter::Assets makeMeterAssets()
    {
        basilica::gui::AnalogMeter::Assets assets;
        assets.face1x = loadImage (BinaryData::vu_brass_face_480x270_png, BinaryData::vu_brass_face_480x270_pngSize);
        assets.face2x = loadImage (BinaryData::vu_brass_face_960x540_png, BinaryData::vu_brass_face_960x540_pngSize);
        assets.needle1x = loadImage (BinaryData::vu_brass_needle_480x270_png, BinaryData::vu_brass_needle_480x270_pngSize);
        assets.needle2x = loadImage (BinaryData::vu_brass_needle_960x540_png, BinaryData::vu_brass_needle_960x540_pngSize);
        assets.glass1x = loadImage (BinaryData::vu_brass_glass_480x270_png, BinaryData::vu_brass_glass_480x270_pngSize);
        assets.glass2x = loadImage (BinaryData::vu_brass_glass_960x540_png, BinaryData::vu_brass_glass_960x540_pngSize);
        return assets;
    }
}

ApotheosisAudioProcessorEditor::ApotheosisAudioProcessorEditor (ApotheosisAudioProcessor& processorToEdit)
    : juce::AudioProcessorEditor (&processorToEdit),
      audioProcessor (processorToEdit),
      presetBar (initLocalisationThenGetPresetManager (processorToEdit)),
      gainReductionMeter (makeMeterAssets(), "Gain Reduction meter"),
      truePeakMeter (makeMeterAssets(), "True Peak meter"),
      lufsMeter (makeMeterAssets(), "LUFS meter")
{
    setLookAndFeel (&lookAndFeel);

    facePlateImage1x = loadImage (BinaryData::faceplate_apotheosis_900x600_png, BinaryData::faceplate_apotheosis_900x600_pngSize);
    facePlateImage2x = loadImage (BinaryData::faceplate_apotheosis_1800x1200_png, BinaryData::faceplate_apotheosis_1800x1200_pngSize);
    brandIconImage = loadImage (BinaryData::icon256_png, BinaryData::icon256_pngSize);

    // Creation order below doubles as the accessibility/keyboard focus
    // order (JUCE's default FocusTraverser walks children in z-order, i.e.
    // creation order) - kept matching the visual reading order:
    // header/scale control, preset bar, meters (GR / True Peak / LUFS),
    // then the three knob bays left-to-right, top-to-bottom within each.
    titleLabel.setText ("Apotheosis", juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    titleLabel.setFont (juce::Font (juce::FontOptions {}
                                        .withName (juce::Font::getDefaultSerifFontName())
                                        .withHeight (26.0f)
                                        .withStyle ("Bold")));
    titleLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (titleLabel);

    addAndMakeVisible (presetBar);

    // A11y: an explicitly-set AccessibilityHandler title always wins over a
    // button's own text for screen readers (see silentium's
    // applyScaleStep() docs), so the title is re-set on every scale change,
    // not just at construction.
    scaleButton.setComponentID ("scaleButton");
    scaleButton.onClick = [this] { cycleScale(); };
    addAndMakeVisible (scaleButton);

    addAndMakeVisible (gainReductionMeter);
    addAndMakeVisible (truePeakMeter);
    addAndMakeVisible (lufsMeter);

    const auto knobStrip1x = loadImage (BinaryData::knob_brass_strip_160px_128f_png, BinaryData::knob_brass_strip_160px_128f_pngSize);
    const auto knobStrip2x = loadImage (BinaryData::knob_brass_strip_320px_128f_png, BinaryData::knob_brass_strip_320px_128f_pngSize);

    for (size_t i = 0; i < knobLayout.size(); ++i)
    {
        auto& entry = knobLayout[i];
        knobs[i].slider = std::make_unique<basilica::gui::FilmstripKnob> (knobStrip1x, knobStrip2x, 128);
        configureKnob (knobs[i], entry.parameterId, entry.labelText, entry.isSetupParam);
    }

    for (size_t i = 0; i < choiceLayout.size(); ++i)
    {
        auto& entry = choiceLayout[i];
        configureChoice (choices[i], entry.parameterId, entry.labelText);
    }

    setResizable (false, false);

    const auto storedStep = (int) audioProcessor.apvts.state.getProperty (uiScaleStepProperty, 0);
    applyScaleStep (juce::jlimit (0, (int) scaleSteps.size() - 1, storedStep));

    startTimerHz (30);
}

ApotheosisAudioProcessorEditor::~ApotheosisAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void ApotheosisAudioProcessorEditor::configureKnob (Knob& knob, const juce::String& parameterId, const juce::String& labelText, bool isSetupParam)
{
    knob.slider->setPopupDisplayEnabled (true, true, this);

    // Setup-parameter treatment (Lookahead): the accessible name/label text
    // itself flags the "(Setup)" distinction so AT users get the same
    // information the dashed amber frame communicates visually (see
    // paint()'s docs), and the description spells out WHY (prepare-time
    // latched, not live) rather than leaving it to be inferred from the
    // suffix alone.
    const auto displayText = isSetupParam ? labelText + " (Setup)" : labelText;
    knob.slider->setTitle (displayText);
    knob.slider->setName (displayText);

    if (isSetupParam)
        knob.slider->setDescription ("Prepare-time parameter: sizes the limiter's internal buffers and changes the plugin's reported latency. Changes take effect at the next audio engine restart, not live during playback.");

    addAndMakeVisible (*knob.slider);

    if (auto* param = audioProcessor.apvts.getParameter (parameterId))
    {
        const auto defaultValue = param->getNormalisableRange().convertFrom0to1 (param->getDefaultValue());
        knob.slider->setDoubleClickReturnValue (true, defaultValue);
    }

    knob.label.setText (displayText, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    knob.label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (knob.label);

    // SliderAttachment MUST be constructed before the textFromValueFunction
    // override below, not after: JUCE 8.0.14's SliderParameterAttachment
    // constructor (juce_ParameterAttachments.cpp:128) itself assigns
    // `slider.textFromValueFunction` as part of wiring the attachment -
    // setting our own function BEFORE this point would be silently
    // clobbered the moment the attachment is created (see silentium's
    // PluginEditor.cpp, the documented JUCE 8.0.14 ordering bug).
    knob.attachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, parameterId, *knob.slider);

    if (auto* param = audioProcessor.apvts.getParameter (parameterId))
    {
        // Every parameter declares its unit via .withLabel() in
        // ParameterLayout.cpp (dB/ms/%), but SliderAttachment's own
        // textFromValueFunction drops it - this feeds both the popup value
        // display and the accessibility value string (juce_Slider.cpp's
        // SliderAccessibilityHandler::ValueInterface::getCurrentValueAsString()
        // calls Slider::getTextFromValue(), which calls this same
        // function). Still uses the parameter's own getText() so the
        // reported precision/rounding matches what the host itself would
        // display.
        knob.slider->textFromValueFunction = [param] (double v)
        {
            return param->getText (param->convertTo0to1 ((float) v), 0) + " " + param->getLabel();
        };
        knob.slider->updateText();
    }
}

void ApotheosisAudioProcessorEditor::configureChoice (Choice& choice, const juce::String& parameterId, const juce::String& labelText)
{
    // Dark-stone/gold colour pair reused directly from
    // BasilicaLookAndFeel's own WCAG-AA-verified caption pair
    // (BasilicaLookAndFeelContrastTests.cpp asserts it clears 4.5:1) rather
    // than a second hand-picked colour that could silently drift out of
    // sync - see that test file's docs.
    const auto textColour = basilica::gui::BasilicaLookAndFeel::getLabelTextColour();
    const auto backingColour = basilica::gui::BasilicaLookAndFeel::getLabelBackingChipColour();

    choice.box.setColour (juce::ComboBox::backgroundColourId, backingColour);
    choice.box.setColour (juce::ComboBox::textColourId, textColour);
    choice.box.setColour (juce::ComboBox::outlineColourId, textColour.withAlpha (0.6f));
    choice.box.setColour (juce::ComboBox::arrowColourId, textColour);
    choice.box.setTitle (labelText);
    addAndMakeVisible (choice.box);

    // ComboBoxAttachment does not populate the box itself (see its JUCE doc
    // comment); pull the choice strings straight from the live APVTS
    // parameter (AudioParameterChoice::getAllValueStrings() returns its
    // `choices` array) rather than duplicating the string list here, so the
    // GUI can never drift out of sync with ParameterLayout.cpp. Item IDs are
    // 1-based to match ComboBox's convention; ComboBoxAttachment maps them
    // back to the parameter's 0-based choice index.
    if (auto* parameter = audioProcessor.apvts.getParameter (parameterId))
        choice.box.addItemList (parameter->getAllValueStrings(), 1);

    choice.label.setText (labelText, juce::dontSendNotification);
    choice.label.setJustificationType (juce::Justification::centred);
    choice.label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (choice.label);

    choice.attachment = std::make_unique<ComboBoxAttachment> (audioProcessor.apvts, parameterId, choice.box);
}

void ApotheosisAudioProcessorEditor::cycleScale()
{
    applyScaleStep ((scaleStepIndex + 1) % (int) scaleSteps.size());
}

void ApotheosisAudioProcessorEditor::applyScaleStep (int newStepIndex)
{
    scaleStepIndex = juce::jlimit (0, (int) scaleSteps.size() - 1, newStepIndex);
    audioProcessor.apvts.state.setProperty (uiScaleStepProperty, scaleStepIndex, nullptr);

    const auto percentText = juce::String ((int) (scaleSteps[(size_t) scaleStepIndex] * 100.0f)) + "%";
    scaleButton.setButtonText (percentText);
    scaleButton.setTitle ("Window scale, " + percentText);

    const auto scale = scaleSteps[(size_t) scaleStepIndex];
    setSize ((int) std::lround ((float) baseEditorWidth * scale),
             (int) std::lround ((float) baseEditorHeight * scale));
}

void ApotheosisAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    const auto scale = scaleSteps[(size_t) scaleStepIndex];
    const auto plateBounds = juce::Rectangle<float> (0.0f, (float) topStripHeight1x * scale + (float) topStripGap1x * scale,
                                                      (float) plateWidth1x * scale, (float) plateHeight1x * scale);

    const auto& plateImage = basilica::gui::pickImageForWidth (facePlateImage1x, facePlateImage2x,
                                                               plateWidth1x, (int) plateBounds.getWidth());
    if (plateImage.isValid())
        g.drawImage (plateImage, plateBounds);

    if (brandIconImage.isValid())
    {
        const auto d = (float) roundelRadius1x * 1.7f * scale;
        const auto cx = (float) roundelCentre1x.x * scale;
        const auto cy = plateBounds.getY() + (float) roundelCentre1x.y * scale;
        g.drawImage (brandIconImage, juce::Rectangle<float> (d, d).withCentre ({ cx, cy }));
    }

    // Lookahead "setup" parameter frame: a dashed amber outline distinct
    // from the plate's own engraved gold - communicates "this control
    // behaves differently" (prepare-time-latched, not live) at a glance,
    // independent of reading its label text. See PluginEditor.h's
    // lookaheadSetupFrameBounds docs.
    if (! lookaheadSetupFrameBounds.isEmpty())
    {
        juce::Path frame;
        frame.addRoundedRectangle (lookaheadSetupFrameBounds.toFloat(), 6.0f);

        juce::Path dashed;
        const float dashLengths[] = { 5.0f, 3.5f };
        juce::PathStrokeType (1.6f).createDashedStroke (dashed, frame, dashLengths, 2);

        g.setColour (juce::Colour (0xffc99a3e));
        g.fillPath (dashed);
    }
}

void ApotheosisAudioProcessorEditor::resized()
{
    const auto scale = scaleSteps[(size_t) scaleStepIndex];
    const auto s = [scale] (int v) { return (int) std::lround ((float) v * scale); };

    auto bounds = getLocalBounds();
    auto topStrip = bounds.removeFromTop (s (topStripHeight1x));

    scaleButton.setBounds (topStrip.removeFromRight (s (scaleButtonWidth1x)));
    presetBar.setBounds (topStrip);

    // Everything below is expressed in plate-local coordinates (the base
    // @1x table above), then offset by the top strip + gap and scaled.
    const auto toPlateRect = [&] (juce::Rectangle<int> plateLocal)
    {
        return juce::Rectangle<int> (s (plateLocal.getX()),
                                     s (topStripHeight1x + topStripGap1x) + s (plateLocal.getY()),
                                     s (plateLocal.getWidth()),
                                     s (plateLocal.getHeight()));
    };

    titleLabel.setBounds (toPlateRect (headerBay1x.withWidth (roundelCentre1x.x - headerBay1x.getX() - roundelRadius1x - 8)));

    // The VU layers' dial content only occupies the central half of their
    // canvas (see AnalogMeter::contentFractionOfCanvas) - expand each
    // meter's bounds around its column's centre so the VISIBLE dial fills
    // the engraved bay. The overhang is fully transparent and
    // mouse-transparent.
    const auto expandMeterBounds = [] (juce::Rectangle<int> bay)
    {
        const auto factor = 1.0f / basilica::gui::AnalogMeter::contentFractionOfCanvas;
        return bay.withSizeKeepingCentre ((int) std::lround ((float) bay.getWidth() * factor),
                                          (int) std::lround ((float) bay.getHeight() * factor));
    };

    const auto metersBay = toPlateRect (metersBay1x);
    const auto meterColW = metersBay.getWidth() / numMeters;

    basilica::gui::AnalogMeter* meters[numMeters] = { &gainReductionMeter, &truePeakMeter, &lufsMeter };

    for (int i = 0; i < numMeters; ++i)
    {
        const auto colX = metersBay.getX() + i * meterColW;
        const juce::Rectangle<int> column (colX, metersBay.getY(), meterColW, metersBay.getHeight());
        meters[(size_t) i]->setBounds (expandMeterBounds (column));
    }

    const auto knobDiam = s (knobDiameter1x);
    const auto labelH = s (knobLabelHeight1x);
    const auto choiceBoxH = s (choiceBoxHeight1x);

    // Output bay row split (see PluginEditorLayout.h's outputCols docs):
    // row 0 (top) = Stereo Link's full-width knob; row 1 (bottom) =
    // Dither/Dither Shape's two half-width combo boxes. Computed once here,
    // shared by both loops below via the `entry.bay == Bay::output` special
    // case, so the knob row and the combo row can never disagree about
    // where the split actually is.
    const auto outputBayRect = toPlateRect (outputBay1x);
    const auto outputKnobRowH = labelH + knobDiam + s (8);

    for (size_t i = 0; i < knobLayout.size(); ++i)
    {
        const auto& entry = knobLayout[i];

        juce::Rectangle<int> cell;

        if (entry.bay == Bay::output)
        {
            cell = { outputBayRect.getX(), outputBayRect.getY(), outputBayRect.getWidth(), outputKnobRowH };
        }
        else
        {
            const auto bayRect = toPlateRect (bayRectFor (entry.bay));
            const auto grid = bayGridFor (entry.bay);
            const auto cellW = bayRect.getWidth() / grid.cols;
            const auto cellH = bayRect.getHeight() / grid.rows;
            cell = { bayRect.getX() + entry.col * cellW, bayRect.getY() + entry.row * cellH, cellW, cellH };
        }

        knobs[i].label.setBounds (cell.getX(), cell.getY(), cell.getWidth(), labelH);
        knobs[i].slider->setBounds (juce::Rectangle<int> (knobDiam, knobDiam)
                                        .withCentre ({ cell.getCentreX(), cell.getY() + labelH + (cell.getHeight() - labelH) / 2 }));

        if (entry.isSetupParam)
            lookaheadSetupFrameBounds = cell.reduced (s (4));
    }

    for (size_t i = 0; i < choiceLayout.size(); ++i)
    {
        const auto& entry = choiceLayout[i];

        juce::Rectangle<int> cell;

        if (entry.bay == Bay::output)
        {
            const auto comboRowY = outputBayRect.getY() + outputKnobRowH;
            const auto comboRowH = outputBayRect.getBottom() - comboRowY;
            const auto halfW = outputBayRect.getWidth() / 2;
            cell = { outputBayRect.getX() + entry.col * halfW, comboRowY, halfW, comboRowH };
        }
        else
        {
            const auto bayRect = toPlateRect (bayRectFor (entry.bay));
            const auto grid = bayGridFor (entry.bay);
            const auto cellW = bayRect.getWidth() / grid.cols;
            const auto cellH = bayRect.getHeight() / grid.rows;
            cell = { bayRect.getX() + entry.col * cellW, bayRect.getY() + entry.row * cellH, cellW, cellH };
        }

        choices[i].label.setBounds (cell.getX(), cell.getY(), cell.getWidth(), labelH);
        choices[i].box.setBounds (juce::Rectangle<int> (cell.getWidth() - s (10), choiceBoxH)
                                      .withCentre ({ cell.getCentreX(), cell.getY() + labelH + (cell.getHeight() - labelH) / 2 }));
    }
}

void ApotheosisAudioProcessorEditor::timerCallback()
{
    // Gain Reduction: TruePeakLimiterEngine publishes this already as
    // gainToDecibels(minGainAppliedThisBlock) - i.e. <= 0 dB, 0 = no
    // reduction, more negative = more reduction (see
    // TruePeakLimiterEngine.cpp's processChunk()). The shared VU face's
    // baked tick table (AnalogMeter.cpp, copied verbatim from
    // render_vu_meter.py) is a generic 0-centred dial: 0 dB sits
    // straight-up/"rest", negative values sweep left, positive sweep
    // right. Feeding this value directly therefore reads exactly as a
    // "gain reduction downward from rest" meter - the needle sits at rest
    // with no reduction and swings away (left) as reduction increases -
    // using the suite's one generic VU face rather than a bespoke
    // GR-specific dial asset, per the M3 briefing's explicit allowance
    // ("the VU face asset is generic; needle mapping per meter documented
    // in code").
    gainReductionMeter.setTargetDb (audioProcessor.getGainReductionDb());

    // True Peak: the engine's own dBTP readout, on the same 0-centred
    // scale - a well-behaved limiter output sits at/below the Ceiling
    // parameter (default -1 dBTP), comfortably inside the tick table's
    // -20..+3 dB span.
    truePeakMeter.setTargetDb (audioProcessor.getOutputTruePeakDb());

    // LUFS: Momentary (400 ms integration, the engine's fastest/most
    // "VU-like" loudness readout - see TruePeakLimiterEngine's class docs)
    // rather than Short-Term (3 s) or Integrated (whole-programme average),
    // which would read as near-static on a physically-styled needle meter.
    // Typical mastering targets (roughly -14 to -9 LUFS streaming/loud) sit
    // within the same generic tick table's range; quieter material simply
    // pins toward the table's -20 dB floor, an accepted limitation of
    // reusing one generic VU face across three different metrics (see this
    // function's top-of-file docs).
    lufsMeter.setTargetDb (audioProcessor.getMomentaryLufs());
}
