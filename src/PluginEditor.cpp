#include "PluginEditor.h"
#include "PluginEditorLayout.h"
#include "PluginProcessor.h"
#include "params/ParameterIds.h"
#include "presets/Localisation.h"

#include <BinaryData.h>

#include <cmath>
#include <utility>

namespace
{
    // Base (@1x, 100% scale) faceplate geometry lives in PluginEditorLayout.h
    // (apth::layout) rather than here, so tests/gui/EditorLayoutTests.cpp can
    // assert layout invariants against the exact constants this file lays
    // components out with.
    using namespace apth::layout;

    struct KnobLayoutEntry
    {
        const char* parameterId;
        const char* labelText; // accessible name (title/tooltip surface)
        const char* engravedLabel; // the plate's own gilded caps (typography pass)
        float cxMaster, cyMaster, rMaster; // true measured knob geometry (crop source) - layout-manifest.json "knobs"
        float cx1x; // interactive slider hit-area X centre (Y is the shared knobRowY1x)
    };

    // Mapping decided for this M3 GUI pass (docs/gui-mapping.md has the
    // full rationale table): the 3 baked knob positions get Apotheosis's
    // three headline continuous PERFORMANCE controls - Input Gain, Ceiling,
    // Release, left to right, matching layout-manifest.json's own knob
    // index order (1/2/3 = left-to-right).
    constexpr std::array<KnobLayoutEntry, 3> knobLayout {
        KnobLayoutEntry { ParamIDs::inputGain, "Input Gain", "INPUT GAIN", 698.0f, 545.0f, 57.0f, knobCentreX1x[0] },
        KnobLayoutEntry { ParamIDs::ceiling, "Ceiling", "CEILING", 867.0f, 546.0f, 55.0f, knobCentreX1x[1] },
        KnobLayoutEntry { ParamIDs::release, "Release", "RELEASE", 1038.0f, 543.0f, 57.0f, knobCentreX1x[2] },
    };

    // ==================== typography pass ====================
    // See PluginEditorLayout.h's typography block for placement provenance
    // and docs/gui-mapping.md's typography section for the full rationale.
    constexpr const char* grCaptionText = "GAIN REDUCTION";
    constexpr const char* smallMeterLegendText[3] = { "INPUT", "OUTPUT", "MARGIN" };

    // Printed ink on the parchment dial face (bright ground, ~150-200
    // luminance): warm sepia near-black, with only a whisper of a lit lip -
    // a printed face, not a deep engraving.
    const basilica::gui::EngravedTextStyle dialNumeralStyle {
        juce::Colour (0xdd2b1a0e), juce::Colour (0x16fff1cf), 15.0f, 0.02f, true
    };
    const basilica::gui::EngravedTextStyle dialCaptionStyle {
        juce::Colour (0xc42b1a0e), juce::Colour (0x14fff1cf), 11.0f, 0.22f, true
    };

    // Gilded lettering on the dark bronze plate (luminance ~50-90, where
    // incision ink would vanish): antique gold with a dark drop shadow one
    // scaled pixel below - EngravedTextStyle's two passes double as
    // shadow + leaf here (the "highlight" pass is simply the darker,
    // offset one).
    const basilica::gui::EngravedTextStyle gildedLegendStyle {
        juce::Colour (0xf0d6ad5e), juce::Colour (0x8c000000), 9.5f, 0.18f, true
    };
    const basilica::gui::EngravedTextStyle gildedKnobLabelStyle {
        juce::Colour (0xf0d6ad5e), juce::Colour (0x8c000000), 13.0f, 0.14f, true
    };

    // Tube-bay glow breathing ballistics (SubtractiveGlow.h's
    // stepGlowMix()) - driven from the processor's own gain-reduction
    // reading. Idle (no gain reduction) breathes around
    // tubeGlowIdleBreathCentre (0.75, +/-0.05), rising to the hard t=1.0
    // ceiling (the base master, tubes at full glow) as the limiter engages
    // more heavily - see docs/gui-mapping.md's "Tube bay behaviour"
    // section and PluginEditorLayout.h's own tubeGlow* constant docs.
    float tubeGlowInstantaneousDepthDb (float gainReductionDb) noexcept
    {
        // gainReductionDb is <= 0 (0 = no reduction, more negative = more
        // reduction - see TruePeakLimiterEngine::getGainReductionDb()'s
        // docs); stepGlowMix() wants a positive "depth" value increasing
        // toward tubeGlowCeilingDb as the effect engages harder.
        return juce::jmax (0.0f, -gainReductionDb);
    }

    juce::Image loadImage (const char* data, int size)
    {
        return juce::ImageCache::getFromMemory (data, size);
    }

    // M2 i18n frame: selects German (resources/i18n/de.txt) or falls through
    // to English, once, at editor construction - see Localisation.h's docs.
    // `presetBar` is a member initialised via the constructor's initialiser
    // list, and its own constructor already calls TRANS() on every button
    // label - member initialisers run in declaration order regardless of the
    // order they're written in, so this helper (called from presetBar's own
    // initialiser expression below) is what actually guarantees
    // installLocalisation() runs before presetBar exists.
    basilica::presets::PresetManager& initLocalisationThenGetPresetManager (ApotheosisAudioProcessor& processor)
    {
        basilica::presets::installLocalisation (BinaryData::de_txt, BinaryData::de_txtSize);
        return processor.presetManager;
    }

    // Non-parameter, per-session UI state: the stepped scale choice (0/1/2)
    // stored as a plain property directly on apvts.state.
    constexpr const char* uiScaleStepProperty = "uiScaleStep";

    basilica::gui::HubNeedle::Assets loadNeedleAsset (const char* data, int size)
    {
        basilica::gui::HubNeedle::Assets assets;
        assets.needleSprite = loadImage (data, size);
        return assets;
    }
}

ApotheosisAudioProcessorEditor::ApotheosisAudioProcessorEditor (ApotheosisAudioProcessor& processorToEdit)
    : juce::AudioProcessorEditor (&processorToEdit),
      audioProcessor (processorToEdit),
      presetBar (initLocalisationThenGetPresetManager (processorToEdit)),
      typography (BinaryData::EBGaramondRegular_ttf, BinaryData::EBGaramondRegular_ttfSize,
                  BinaryData::EBGaramondSemiBold_ttf, BinaryData::EBGaramondSemiBold_ttfSize)
{
    masterImage = loadImage (BinaryData::master_victorian_png, BinaryData::master_victorian_pngSize);

    // Creation order doubles as the accessibility/keyboard focus order
    // (JUCE's default FocusTraverser walks children in z-order, i.e.
    // creation order) - kept matching visual reading order: preset bar +
    // scale control, the grand GR needle, the 3 small needles top-to-
    // bottom, then the knob row left-to-right.
    addAndMakeVisible (presetBar);

    scaleButton.setComponentID ("scaleButton");
    scaleButton.onClick = [this] { cycleScale(); };
    addAndMakeVisible (scaleButton);

    gainReductionNeedle = std::make_unique<basilica::gui::HubNeedle> (
        loadNeedleAsset (BinaryData::needle_mainVU_victorian_png, BinaryData::needle_mainVU_victorian_pngSize),
        "Gain Reduction meter",
        mainVUNeedle1x.spriteSizeFraction, mainVUNeedle1x.spriteSizeFraction, mainVUNeedle1x.spriteSizeFraction,
        mainVUNeedle1x.bakedAngleDeg,
        grRestDb, grFullScaleReductionDb, grRestAngleDeg, grFullScaleAngleDeg);
    addAndMakeVisible (*gainReductionNeedle);

    inputLevelNeedle = std::make_unique<basilica::gui::HubNeedle> (
        loadNeedleAsset (BinaryData::needle_smallMeterTop_victorian_png, BinaryData::needle_smallMeterTop_victorian_pngSize),
        "Input Level meter",
        smallMeterTopNeedle1x.spriteSizeFraction, smallMeterTopNeedle1x.spriteSizeFraction, smallMeterTopNeedle1x.spriteSizeFraction,
        smallMeterTopNeedle1x.bakedAngleDeg,
        vuRestDb, vuFullScaleDb, smallMeterRestAngleDeg, smallMeterFullScaleAngleDeg);
    addAndMakeVisible (*inputLevelNeedle);

    outputLevelNeedle = std::make_unique<basilica::gui::HubNeedle> (
        loadNeedleAsset (BinaryData::needle_smallMeterMid_victorian_png, BinaryData::needle_smallMeterMid_victorian_pngSize),
        "Output Level meter",
        smallMeterMidNeedle1x.spriteSizeFraction, smallMeterMidNeedle1x.spriteSizeFraction, smallMeterMidNeedle1x.spriteSizeFraction,
        smallMeterMidNeedle1x.bakedAngleDeg,
        vuRestDb, vuFullScaleDb, smallMeterRestAngleDeg, smallMeterFullScaleAngleDeg);
    addAndMakeVisible (*outputLevelNeedle);

    truePeakMarginNeedle = std::make_unique<basilica::gui::HubNeedle> (
        loadNeedleAsset (BinaryData::needle_smallMeterBottom_victorian_png, BinaryData::needle_smallMeterBottom_victorian_pngSize),
        "True Peak Margin meter",
        smallMeterBottomNeedle1x.spriteSizeFraction, smallMeterBottomNeedle1x.spriteSizeFraction, smallMeterBottomNeedle1x.spriteSizeFraction,
        smallMeterBottomNeedle1x.bakedAngleDeg,
        marginRestDb, marginFullScaleDb, smallMeterRestAngleDeg, smallMeterFullScaleAngleDeg);
    addAndMakeVisible (*truePeakMarginNeedle);

    for (size_t i = 0; i < knobLayout.size(); ++i)
    {
        auto& entry = knobLayout[i];
        knobs[i].slider = std::make_unique<basilica::gui::MasterCropKnob> (
            masterImage, juce::Point<float> (entry.cxMaster, entry.cyMaster), entry.rMaster);
        configureKnob (knobs[i], entry.parameterId, entry.labelText);
    }

    auto glowImage = loadImage (BinaryData::tube_glow_victorian_png, BinaryData::tube_glow_victorian_pngSize);
    tubeGlow = basilica::gui::SubtractiveGlow (
        masterImage, glowImage,
        { tubeBayZoneMasterPx.getX(), tubeBayZoneMasterPx.getY() }, 1.0f);
    tubeGlowState.startTimeSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    tubeGlowMix = tubeGlowIdleBreathCentre;

    setResizable (false, false);

    const auto storedStep = (int) audioProcessor.apvts.state.getProperty (uiScaleStepProperty, 0);
    applyScaleStep (juce::jlimit (0, (int) scaleSteps.size() - 1, storedStep));

    startTimerHz (30);
}

ApotheosisAudioProcessorEditor::~ApotheosisAudioProcessorEditor() = default;

void ApotheosisAudioProcessorEditor::configureKnob (Knob& knob, const juce::String& parameterId, const juce::String& labelText)
{
    knob.slider->setPopupDisplayEnabled (true, true, this);
    knob.slider->setTitle (labelText);
    knob.slider->setName (labelText);
    addAndMakeVisible (*knob.slider);

    if (auto* param = audioProcessor.apvts.getParameter (parameterId))
    {
        const auto defaultValue = param->getNormalisableRange().convertFrom0to1 (param->getDefaultValue());
        knob.slider->setDoubleClickReturnValue (true, defaultValue);
    }

    // SliderAttachment MUST be constructed before the textFromValueFunction
    // override below - JUCE 8.0.14's SliderParameterAttachment constructor
    // itself assigns slider.textFromValueFunction as part of wiring the
    // attachment, which would silently clobber an override set beforehand.
    knob.attachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, parameterId, *knob.slider);

    if (auto* param = audioProcessor.apvts.getParameter (parameterId))
    {
        knob.slider->textFromValueFunction = [param] (double v)
        {
            return param->getText (param->convertTo0to1 ((float) v), 0) + " " + param->getLabel();
        };
        knob.slider->updateText();
    }
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
    const auto s = [scale] (float v) { return v * scale; };

    const auto stripHeight = (float) topStripHeight1x * scale;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff17141a), 0.0f, 0.0f,
                                             juce::Colour (0xff0b090d), 0.0f, stripHeight, false));
    g.fillRect (juce::Rectangle<float> (0.0f, 0.0f, (float) getWidth(), stripHeight));
    g.setColour (juce::Colour (0xff5a4420));
    g.fillRect (juce::Rectangle<float> (0.0f, stripHeight - 1.0f * scale, (float) getWidth(), 1.0f * scale));

    const auto plateOrigin = juce::Point<float> (0.0f, stripHeight + (float) topStripGap1x * scale);
    const auto plateBounds = juce::Rectangle<float> (plateOrigin.x, plateOrigin.y,
                                                      (float) plateWidth1x * scale, (float) plateHeight1x * scale);

    const auto toScreenRect = [&] (juce::Rectangle<int> local1x)
    {
        return juce::Rectangle<float> (plateOrigin.x + s ((float) local1x.getX()),
                                       plateOrigin.y + s ((float) local1x.getY()),
                                       s ((float) local1x.getWidth()),
                                       s ((float) local1x.getHeight()));
    };

    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);

    // 1. Baseline plate: the single master render, filling the plate bounds.
    // Bakes the oak frame, brass plate, empty grand GR dial (tick arc + red
    // zone), 3 empty small dials, tube bay at full glow, and all 3 knobs at
    // 12 o'clock - nothing else is drawn for any of those elements.
    if (masterImage.isValid())
        g.drawImage (masterImage, plateBounds, juce::RectanglePlacement::centred, false);

    // (2. The 3 knobs are separate MasterCropKnob child components, drawn
    // automatically after this method returns - see resized() for their
    // bounds and MasterCropKnob.cpp for the rotating-crop draw itself.)

    // 3. Tube-bay glow layer (SUBTRACTIVE, see SubtractiveGlow.h) - a single
    // cross-blend of the whole glow-sprite footprint, ballistically driven
    // by updateTubeGlowMix() (called from timerCallback()) or directly by
    // the test/preview hooks below.
    {
        const auto destRect = toScreenRect (tubeBayZone1x);
        tubeGlow.drawZone (g, destRect, tubeGlowMix);
    }

    // 4. Typography layer (suite typo phase - PlateTypography.h): printed
    // grand-dial numerals + caption on the parchment, gilded small-meter
    // legends and knob labels on the bronze. Drawn LAST within paint() (so
    // the tube-glow blit can never cover it), but still UNDER the needle
    // child components - exactly like a real printed dial face beneath its
    // needle.
    drawPlateTypography (g, plateOrigin, scale);

    // (The 4 needles are separate HubNeedle child components, drawn after
    // this method returns - see resized() for their bounds. Everything
    // else - oak frame, brass plate engraving, honeycomb mesh, every dial
    // face/tick arc/red zone, the knobs' own baked outer rim - stays BAKED
    // in the master, no draw calls for any of it.)
}

void ApotheosisAudioProcessorEditor::drawPlateTypography (juce::Graphics& g, juce::Point<float> plateOrigin, float scale) const
{
    constexpr auto masterScale = (float) plateWidth1x / (float) masterCanvasWidthPx;

    const auto toScreen = [&] (juce::Rectangle<float> local1x)
    {
        return juce::Rectangle<float> (plateOrigin.x + local1x.getX() * scale,
                                       plateOrigin.y + local1x.getY() * scale,
                                       local1x.getWidth() * scale,
                                       local1x.getHeight() * scale);
    };

    // Grand dial: numerals at 0/-3/-6/-9/-12 dB GR, each centred on the
    // pivot-concentric numeral circle at the EXACT angle the needle itself
    // would deflect to for that reading (same jmap through the same
    // grRest/grFullScale constants - see HubNeedle's own value->angle
    // mapping), printed unsigned like a classic GR-meter face, OUTSIDE the
    // baked tick arc on the open upper parchment.
    const juce::Point<float> grPivot1x (
        (float) mainVUNeedle1x.topLeft1x.x + (float) mainVUNeedle1x.componentSize1x * 0.5f,
        (float) mainVUNeedle1x.topLeft1x.y + (float) mainVUNeedle1x.componentSize1x * 0.5f);

    const auto numeralRadius1x = grNumeralRadiusMasterPx * masterScale;

    for (int i = 0; i < grNumeralCount; ++i)
    {
        const auto db = -grNumeralStepDb * (float) i;
        const auto angleDeg = juce::jmap (db, grRestDb, grFullScaleReductionDb,
                                          grRestAngleDeg, grFullScaleAngleDeg);
        const auto angleRad = juce::degreesToRadians (angleDeg);

        const juce::Point<float> centre1x (grPivot1x.x + numeralRadius1x * std::sin (angleRad),
                                           grPivot1x.y - numeralRadius1x * std::cos (angleRad));

        const juce::Rectangle<float> box1x ((float) grNumeralBoxSize1x, (float) grNumeralBoxSize1x);

        typography.drawEngraved (g, juce::String ((int) -db),
                                 toScreen (box1x.withCentre (centre1x)), scale, dialNumeralStyle);
    }

    // Function caption, straight up from the pivot, between the numeral
    // arc and the baked gear bridge.
    {
        const juce::Point<float> captionCentre1x (grPivot1x.x,
                                                  grPivot1x.y - grCaptionRadiusMasterPx * masterScale);
        const juce::Rectangle<float> box1x ((float) grCaptionWidth1x, (float) grCaptionHeight1x);

        typography.drawEngraved (g, grCaptionText, toScreen (box1x.withCentre (captionCentre1x)), scale, dialCaptionStyle);
    }

    // Small-meter legends (gilded, printed on each gauge's own face mound
    // below the pivot - see PluginEditorLayout.h for why on-face is the
    // only consistent placement this column allows).
    for (size_t i = 0; i < 3; ++i)
    {
        const auto& l = smallMeterLegend1x[i];
        const juce::Rectangle<float> box1x ((float) (l.cx - l.w / 2), (float) (l.cy - l.h / 2),
                                            (float) l.w, (float) l.h);

        typography.drawEngraved (g, smallMeterLegendText[i], toScreen (box1x), scale, gildedLegendStyle);
    }

    // Gilded knob labels, centred under each knob's interactive hit-area.
    for (const auto& entry : knobLayout)
    {
        const juce::Rectangle<float> box1x (entry.cx1x - (float) knobLabelWidth1x * 0.5f,
                                            (float) (knobRowY1x + knobDiameter1x / 2 + knobLabelGap1x),
                                            (float) knobLabelWidth1x, (float) knobLabelHeight1x);

        typography.drawEngraved (g, entry.engravedLabel, toScreen (box1x), scale, gildedKnobLabelStyle);
    }
}

void ApotheosisAudioProcessorEditor::resized()
{
    const auto scale = scaleSteps[(size_t) scaleStepIndex];
    const auto s = [scale] (int v) { return (int) std::lround ((float) v * scale); };

    auto bounds = getLocalBounds();
    auto topStrip = bounds.removeFromTop (s (topStripHeight1x));

    scaleButton.setBounds (topStrip.removeFromRight (s (scaleButtonWidth1x)).reduced (0, s (2)));
    presetBar.setBounds (topStrip.reduced (0, s (2)));

    // Everything below is expressed in plate-local coordinates (the base
    // @1x table in PluginEditorLayout.h), then offset by the top strip +
    // gap and scaled.
    const auto toPlatePoint = [&] (juce::Point<int> plateLocal)
    {
        return juce::Point<int> (s (plateLocal.x),
                                 s (topStripHeight1x + topStripGap1x) + s (plateLocal.y));
    };

    const auto placeNeedle = [&] (basilica::gui::HubNeedle& needle, const NeedleGeometry1x& geom)
    {
        const auto topLeftScreen = toPlatePoint (geom.topLeft1x);
        const auto size = s (geom.componentSize1x);
        needle.setBounds (topLeftScreen.x, topLeftScreen.y, size, size);
    };

    placeNeedle (*gainReductionNeedle, mainVUNeedle1x);
    placeNeedle (*inputLevelNeedle, smallMeterTopNeedle1x);
    placeNeedle (*outputLevelNeedle, smallMeterMidNeedle1x);
    placeNeedle (*truePeakMarginNeedle, smallMeterBottomNeedle1x);

    for (size_t i = 0; i < knobLayout.size(); ++i)
    {
        auto& entry = knobLayout[i];
        const auto diameter = s (knobDiameter1x);

        knobs[i].slider->setBounds (juce::Rectangle<int> (diameter, diameter)
                                        .withCentre (toPlatePoint ({ (int) std::lround (entry.cx1x), knobRowY1x })));
    }

    // Tube-glow repaint region: recomputed here so timerCallback()'s
    // per-tick repaint() call only invalidates this area rather than the
    // whole plate.
    const auto toPlateRect = [&] (juce::Rectangle<int> local1x)
    {
        return juce::Rectangle<int> (toPlatePoint (local1x.getPosition()), toPlatePoint (local1x.getBottomRight()));
    };

    tubeGlowRepaintBounds = toPlateRect (tubeBayZone1x).expanded (s (4));
}

void ApotheosisAudioProcessorEditor::updateTubeGlowMix() noexcept
{
    const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    constexpr float dt = 1.0f / 30.0f;

    tubeGlowMix = basilica::gui::stepGlowMix (
        tubeGlowState, tubeGlowInstantaneousDepthDb (audioProcessor.getGainReductionDb()), dt, now,
        tubeGlowTauSeconds, tubeGlowFloorDb, tubeGlowCeilingDb,
        tubeGlowIdleBreathCentre, tubeGlowIdleBreathHalfRange, tubeGlowPhaseSeed);
}

void ApotheosisAudioProcessorEditor::timerCallback()
{
    // Grand meter - GAIN REDUCTION. The engine's own reading is already
    // <=0 dB (0 = no reduction), matching HubNeedle's own restValue/
    // fullScaleValue calibration directly (see PluginEditorLayout.h's
    // grRestDb/grFullScaleReductionDb docs) - no conversion needed here.
    gainReductionNeedle->setTargetDb (audioProcessor.getGainReductionDb());

    // Small meters top/mid - INPUT/OUTPUT level, Standard-A suite VU
    // convention (0 VU = -18 dBFS - PluginEditorLayout.h's
    // vuZeroReferenceDbfs).
    inputLevelNeedle->setTargetDb (audioProcessor.getInputLevelDb() - vuZeroReferenceDbfs);
    outputLevelNeedle->setTargetDb (audioProcessor.getOutputLevelDb() - vuZeroReferenceDbfs);

    // Small meter bottom - TRUE-PEAK MARGIN: how much headroom remains
    // between the current Ceiling and the engine's own oversampled
    // true-peak reading (getOutputTruePeakDb() - see that getter's own
    // docs for why this is the right measurement for a MARGIN reading,
    // distinct from the level meters above).
    if (auto* ceilingParam = audioProcessor.apvts.getParameter (ParamIDs::ceiling))
    {
        const auto ceilingDb = ceilingParam->getNormalisableRange().convertFrom0to1 (ceilingParam->getValue());
        truePeakMarginNeedle->setTargetDb (ceilingDb - audioProcessor.getOutputTruePeakDb());
    }

    gainReductionNeedle->tick (1.0f / 30.0f);
    inputLevelNeedle->tick (1.0f / 30.0f);
    outputLevelNeedle->tick (1.0f / 30.0f);
    truePeakMarginNeedle->tick (1.0f / 30.0f);

    updateTubeGlowMix();
    repaint (tubeGlowRepaintBounds);
}

void ApotheosisAudioProcessorEditor::recomputeTubeGlowForPreview() noexcept
{
    updateTubeGlowMix();
    repaint (tubeGlowRepaintBounds);
}

void ApotheosisAudioProcessorEditor::setTubeGlowElapsedSecondsForPreview (double elapsedSeconds) noexcept
{
    tubeGlowState.startTimeSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0 - elapsedSeconds;
    updateTubeGlowMix();
    repaint (tubeGlowRepaintBounds);
}

void ApotheosisAudioProcessorEditor::setTubeGlowMixForPreview (float t) noexcept
{
    tubeGlowMix = juce::jlimit (0.0f, 1.0f, t);
    repaint (tubeGlowRepaintBounds);
}
