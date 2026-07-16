#include "params/ParameterIds.h"

#include <BinaryData.h>

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <utility>
#include <vector>

// M2 i18n frame tests (.scaffold/specs/preset-system-m2.md's "I18N" section):
// "the de mapping parses; every TRANS key present in de.txt (script or test
// iterates keys); parameter names verifiably NOT in the mapping."
//
// Unlike PresetManager/PresetBar/Localisation themselves (copied verbatim
// from the pilot, basilica-audio/nave), this coverage is Apotheosis-specific
// scaffolding not present in the pilot - added here because the binding spec
// requires it, independent of what the pilot happened to test.
namespace
{
    // Every TRANS()'d frame string PresetBar.cpp actually calls, paired with
    // resources/i18n/de.txt's exact expected translation (mirrored here as a
    // fixed list - PresetBar.cpp/de.txt are copied verbatim from the pilot
    // and not expected to change per-plugin, see docs/preset-system-notes.md
    // - kept in sync by inspection whenever either file changes). Asserting
    // the exact expected value (rather than merely "differs from the
    // English key") is deliberate: "Init" = "Init" is a legitimate,
    // correctly-mapped entry in de.txt (a loanword, identical in both
    // languages) that a weaker "must differ" check would wrongly flag as
    // missing.
    const std::vector<std::pair<juce::String, juce::String>>& expectedFrameTranslations()
    {
        // juce::CharPointer_UTF8 wraps each raw string literal explicitly so
        // the non-ASCII (umlaut) translations below are interpreted as
        // UTF-8 regardless of platform-default char* assumptions, matching
        // how resources/i18n/de.txt itself is decoded (juce::String::fromUTF8
        // in Localisation.cpp).
        static const std::vector<std::pair<juce::String, juce::String>> pairs {
            { "Init", "Init" },
            { "Factory", juce::String (juce::CharPointer_UTF8 ("Werksvoreinstellungen")) },
            { "User", "Eigene" },
            { "Set current as default", juce::String (juce::CharPointer_UTF8 ("Aktuelle als Standard festlegen")) },
            { "Save", "Speichern" },
            { "Save As...", juce::String (juce::CharPointer_UTF8 ("Speichern unter...")) },
            { "Delete", juce::String (juce::CharPointer_UTF8 ("L\xc3\xb6schen")) },
            { "Import...", juce::String (juce::CharPointer_UTF8 ("Importieren...")) },
            { "Export...", juce::String (juce::CharPointer_UTF8 ("Exportieren...")) },
            { "Enter a name for the new preset:",
              juce::String (juce::CharPointer_UTF8 ("Namen f\xc3\xbcr die neue Voreinstellung eingeben:")) },
            { "Preset name", juce::String (juce::CharPointer_UTF8 ("Name der Voreinstellung")) },
            { "Cancel", juce::String (juce::CharPointer_UTF8 ("Abbrechen")) },
            { "Import a preset or preset bank...",
              juce::String (juce::CharPointer_UTF8 ("Voreinstellung oder Voreinstellungs-Sammlung importieren...")) },
            { "Import failed", juce::String (juce::CharPointer_UTF8 ("Import fehlgeschlagen")) },
            { "Export preset...", juce::String (juce::CharPointer_UTF8 ("Voreinstellung exportieren...")) },
            { "This file is not a valid preset.",
              juce::String (juce::CharPointer_UTF8 ("Diese Datei ist keine g\xc3\xbcltige Voreinstellung.")) },
            { "This preset was saved by an incompatible version of the preset format.",
              juce::String (juce::CharPointer_UTF8 (
                  "Diese Voreinstellung wurde mit einer inkompatiblen Version des Voreinstellungsformats gespeichert.")) },
            { "This preset file belongs to a different plugin.",
              juce::String (juce::CharPointer_UTF8 ("Diese Voreinstellungsdatei geh\xc3\xb6rt zu einem anderen Plugin.")) },
        };
        return pairs;
    }

    // Core/DSP terminology - parameter names, units, technical terms - that
    // must NEVER appear as translated keys in the German mapping (the
    // binding spec: "NEVER translate core/DSP terminology... parameter
    // names, units and technical terms... stay English everywhere").
    const juce::StringArray& parameterNamesThatMustStayEnglish()
    {
        static const juce::StringArray names {
            "Input Gain", "Ceiling", "Release", "Lookahead", "Release Curve", "Dither", "Clip Mix",
            "Attack", "Auto Release", "Stereo Link", "Dither Shape",
        };
        return names;
    }
}

TEST_CASE ("i18n: resources/i18n/de.txt parses as valid LocalisedStrings mapping data", "[i18n][localisation]")
{
    REQUIRE (BinaryData::de_txt != nullptr);
    REQUIRE (BinaryData::de_txtSize > 0);

    const auto text = juce::String::fromUTF8 (BinaryData::de_txt, BinaryData::de_txtSize);
    CHECK (text.isNotEmpty());

    // Constructing a LocalisedStrings from the file contents must not throw
    // and must yield a usable mapping (translate() on a known key returns a
    // different, non-empty string).
    juce::LocalisedStrings mapping (text, true);

    CHECK (mapping.translate ("Save").isNotEmpty());
    CHECK (mapping.translate ("Save") != juce::String ("Save"));
}

TEST_CASE ("i18n: every PresetBar frame string TRANS() calls resolves to its exact expected German translation",
           "[i18n][localisation]")
{
    const auto text = juce::String::fromUTF8 (BinaryData::de_txt, BinaryData::de_txtSize);
    juce::LocalisedStrings mapping (text, true);

    for (const auto& [key, expectedTranslation] : expectedFrameTranslations())
    {
        CAPTURE (key, expectedTranslation);
        CHECK (mapping.translate (key) == expectedTranslation);
    }
}

TEST_CASE ("i18n: parameter/DSP terminology is verifiably NOT present in the German mapping", "[i18n][localisation]")
{
    const auto text = juce::String::fromUTF8 (BinaryData::de_txt, BinaryData::de_txtSize);
    juce::LocalisedStrings mapping (text, true);

    for (const auto& name : parameterNamesThatMustStayEnglish())
    {
        CAPTURE (name);

        // Absent from the mapping => translate() returns the input
        // unchanged (JUCE's documented fallback behaviour for an unmapped
        // key with no further fallback language installed).
        CHECK (mapping.translate (name) == name);
    }

    // Cross-check directly against ParamIDs.h/ParameterLayout.cpp's actual
    // string literals for the four new v0.2.0 controls specifically (not
    // just the hardcoded list above), since those are the newest surface
    // this pass adds.
    for (const auto* rawId : { ParamIDs::attack, ParamIDs::autoRelease, ParamIDs::stereoLink, ParamIDs::ditherShape })
    {
        CAPTURE (rawId);
        // Parameter IDs themselves (camelCase internal identifiers, not
        // display labels) are never TRANS()'d or user-facing text at all -
        // this just documents that raw IDs like "autoRelease" also don't
        // collide with any translation key.
        CHECK (mapping.translate (rawId) == juce::String (rawId));
    }
}
