cmake_minimum_required(VERSION 3.24)

# Must be set before the first project() call to take effect.
# CI overrides CMAKE_OSX_ARCHITECTURES explicitly for release/universal builds;
# local builds are left at the native architecture.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS deployment target" FORCE)

# JUCE's build tooling (juceaide) and some CMake find-modules it triggers
# (e.g. FindThreads) expect the C toolchain to be available; declaring it
# here at the top level avoids it being enabled implicitly deeper inside
# JUCE's CMake helpers, which has been observed to break Ninja's generate
# step (missing CMAKE_C_COMPILE_OBJECT rule variable).
project(Apotheosis VERSION 0.6.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(cmake/CPM.cmake)

# CPMAddPackage automatically respects the CPM_SOURCE_CACHE environment
# variable if it is set, reusing a shared package cache across builds/checkouts.
if(DEFINED ENV{CPM_SOURCE_CACHE})
    message(STATUS "Apotheosis: using CPM_SOURCE_CACHE at $ENV{CPM_SOURCE_CACHE}")
endif()

CPMAddPackage(
    NAME JUCE
    GITHUB_REPOSITORY juce-framework/JUCE
    GIT_TAG 8.0.14
    GIT_SHALLOW TRUE
)

# ==============================================================================
# Plugin target
# ==============================================================================

juce_add_plugin(Apotheosis
    # The trading name, not the legal person. This is the string a host groups
    # plugins by - Logic's plugin manager, Cubase's vendor column, Reaper's FX
    # browser - and the suite ships under one brand. Display only: the VST3
    # class ID derives from PLUGIN_MANUFACTURER_CODE + PLUGIN_CODE alone (JUCE
    # 8.0.14, juce_VST3ModuleInfo.h, VST3Interface::jucePluginId) and the Audio
    # Unit identity triple stays (aufx, Apotheosis's PLUGIN_CODE, Yvsv), so no
    # existing session is disturbed. See docs/branding.md.
    COMPANY_NAME "Basilica Audio"
    # DELIBERATELY NOT RENAMED to com.basilicaaudio.*, and not an oversight.
    # The bundle ID is how a host recognises this plugin across sessions, and
    # how every preset file ever written names it ("plugin" field). Changing it
    # makes every existing project treat this as a different plugin, for no
    # user-visible benefit - nothing a customer sees flows from it. Recorded in
    # basilica-audio/.github docs/adr/0001-suite-vendor-identity.md.
    BUNDLE_ID com.yvesvogl.apotheosis
    PLUGIN_MANUFACTURER_CODE Yvsv
    PLUGIN_CODE Apth
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Apotheosis"
    COMPANY_WEBSITE "https://basilica-audio.github.io/website/"
    # Names the legal person, not the trading name above, and stays that way:
    # Basilica Audio is how the suite is sold, Yves Vogl is who holds the
    # copyright. Aligning the two would make this notice less accurate.
    COMPANY_COPYRIGHT "Copyright (c) 2026 Yves Vogl"
    DESCRIPTION "Lookahead true-peak brickwall limiter for the master bus"
    VST3_CATEGORIES Fx Dynamics Mastering
    ICON_BIG "docs/assets/icon.png"
    COPY_PLUGIN_AFTER_BUILD TRUE
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
)

# ==============================================================================
# Embedded assets: M2 preset system factory presets (presets/factory/*.json)
# and the M2 i18n frame's German translation (resources/i18n/de.txt). See
# src/presets/PresetManager.h and src/presets/Localisation.h - both consume
# these purely via BinaryData:: symbols passed in from PluginProcessor.cpp/
# PluginEditor.cpp, never by including BinaryData.h themselves, so they stay
# portable to sibling plugins (see docs/preset-system-notes.md pattern,
# pioneered by basilica-audio/nave).
# ==============================================================================

juce_add_binary_data(ApotheosisBinaryData SOURCES
    presets/factory/default.json
    presets/factory/punchyMaster.json
    presets/factory/denseLoudModern.json
    presets/factory/wideImagePreserve.json
    presets/factory/streamingSafeHighLoudness.json
    presets/factory/adaptiveRiding.json
    presets/factory/brightClipperBlend.json
    presets/factory/cleanExportDithered.json
    presets/factory/transparentMastering.json
    presets/factory/punchyLoudStyle.json
    presets/factory/safeArchivalTruePeak.json
    resources/i18n/de.txt
    # M3 photoreal GUI assets (resources/gui/, copied from the suite's
    # Blender pre-render pipeline at .scaffold/gui-assets/ - see each asset
    # family's README there for provenance/verification). The repo is
    # self-contained: these copies are the ones the plugin ships with.
    resources/gui/knob_brass_strip_160px_128f.png
    resources/gui/knob_brass_strip_320px_128f.png
    resources/gui/toggle_brass_strip_100px_4f.png
    resources/gui/toggle_brass_strip_200px_4f.png
    resources/gui/vu_brass_face_480x270.png
    resources/gui/vu_brass_face_960x540.png
    resources/gui/vu_brass_needle_480x270.png
    resources/gui/vu_brass_needle_960x540.png
    resources/gui/vu_brass_glass_480x270.png
    resources/gui/vu_brass_glass_960x540.png
    resources/gui/faceplate_apotheosis_900x600.png
    resources/gui/faceplate_apotheosis_1800x1200.png
    # Brand icon (already the plugin's ICON_BIG; embedded here additionally
    # so the editor can draw it in the faceplate's header roundel).
    docs/assets/icon-256.png
    # M3 photoreal GUI ("victorian" faceplate design, owner-approved -
    # supersedes the faceplate/knob/vu assets above, which are left
    # embedded and unreferenced by the current editor rather than removed,
    # see PluginEditorLayout.h's top-of-file docs) - copied verbatim from
    # brand/mocks/victorian/ (repo-relative to the suite root, one level
    # above this repo; see docs/gui-mapping.md for the full provenance and
    # parameter-mapping table). Exactly one baked master faceplate plus its
    # small set of live-overlay source assets:
    #   - master_victorian.png: the SOLE faceplate baseline (copied from
    #     master-01-base.png) - oak frame, brass plate, empty grand GR dial
    #     (tick arc + red zone baked in), tube bay at full glow, 3 empty
    #     small dials, 3 knobs at 12 o'clock.
    #   - needle_{mainVU,smallMeterTop,smallMeterMid,smallMeterBottom}_
    #     victorian.png: the 4 master-extracted, pivot-centred needle
    #     sprites (components/needle-*.png) - rotated live by
    #     src/gui/HubNeedle.
    #   - tube_glow_victorian.png: the tube-bay glow diff layer
    #     (components/tube-glow.png) - src/gui/SubtractiveGlow builds a
    #     "fully dim" frame from this once at construction and cross-blends
    #     it against the baseline live (see that class's own runtime-model
    #     docs).
    resources/gui/master_victorian.png
    resources/gui/needle_mainVU_victorian.png
    resources/gui/needle_smallMeterTop_victorian.png
    resources/gui/needle_smallMeterMid_victorian.png
    resources/gui/needle_smallMeterBottom_victorian.png
    resources/gui/tube_glow_victorian.png
    # Suite typography: EB Garamond (OFL, resources/fonts/OFL-EBGaramond.txt),
    # the same two faces siblings silentium/aureate embed - used by
    # src/gui/PlateTypography for the live dial numerals, meter legends and
    # knob labels drawn over the master render (the suite-wide typography
    # pass: this design's own render decision removed all baked numerals -
    # brand/mocks/victorian/prompts.md's "IMPORTANT SCALE CORRECTION" - and
    # the owner decision of 2026-07-26 sets all lettering locally as a
    # sharp JUCE text layer instead).
    resources/fonts/EBGaramond-Regular.ttf
    resources/fonts/EBGaramond-SemiBold.ttf
)

# ==============================================================================
# SharedCode: an INTERFACE target carrying the plugin's sources, definitions,
# and library links so that both the plugin targets (AU/VST3/Standalone) and
# the Tests executable compile the exact same code (Pamplejuce pattern, see
# https://github.com/sudara/pamplejuce), avoiding ODR violations between them.
# ==============================================================================

add_library(SharedCode INTERFACE)

target_compile_features(SharedCode INTERFACE cxx_std_20)

file(GLOB_RECURSE ApotheosisSources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

target_sources(SharedCode INTERFACE ${ApotheosisSources})

target_include_directories(SharedCode INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")

target_compile_definitions(SharedCode INTERFACE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    # AGPLv3 usage permits disabling the splash screen.
    JUCE_DISPLAY_SPLASH_SCREEN=0
    # processBlock() holds a juce::ScopedNoDenormals, which forces FTZ/DAZ for
    # the whole audio callback, so juce_dsp's per-block snap-to-zero passes are
    # redundant work on the audio thread. juce_dsp.h (JUCE 8.0.14) documents
    # disabling the flag as safe exactly in this case. It also keeps denormal
    # tail tests honest: the library no longer masks a missing FTZ path.
    # Fleet decision: basilica-audio/Crypta#99.
    JUCE_DSP_ENABLE_SNAP_TO_ZERO=0
)

# Add any extra juce:: modules a specific plugin needs (e.g. juce_audio_formats
# for reading impulse-response files) to this list.
target_link_libraries(SharedCode INTERFACE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
    ApotheosisBinaryData
)

target_link_libraries(Apotheosis PRIVATE SharedCode)

# ==============================================================================
# Tests
# ==============================================================================

enable_testing()
set(CTEST_OUTPUT_ON_FAILURE ON)

CPMAddPackage(
    NAME Catch2
    GITHUB_REPOSITORY catchorg/Catch2
    GIT_TAG v3.15.2
)

file(GLOB_RECURSE ApotheosisTestSources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h")

add_executable(Tests ${ApotheosisTestSources})

target_compile_features(Tests PRIVATE cxx_std_20)

target_link_libraries(Tests PRIVATE SharedCode Catch2::Catch2)

# Copy over the plugin target's compile definitions (JucePlugin_* macros etc.)
# so plugin code compiles identically when built as part of the Tests binary.
target_compile_definitions(Tests PRIVATE
    $<TARGET_PROPERTY:Apotheosis,COMPILE_DEFINITIONS>
    JUCE_MODAL_LOOPS_PERMITTED=1
    # Absolute path to docs/, so tests/gui/EditorSnapshotTests.cpp can write
    # the committed docs/gui-preview.png directly rather than a copy step.
    APOTHEOSIS_DOCS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/docs"
    # Test-only hooks in SharedCode (deterministic dither seeding for the
    # golden-fixture harness, the T2 ceiling-clamp bypass) - never defined
    # for the plugin targets, so shipping binaries carry none of them.
    APOTHEOSIS_TESTING=1
    # Absolute path to the tests/ source directory, so the Tests binary can
    # locate the committed golden fixtures (tests/fixtures/v020) regardless
    # of the build/working directory ctest happens to use.
    APOTHEOSIS_TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests"
)

# juce::SHA256 for the golden-fixture manifest integrity hashes
# (tests/RegressionTests.cpp) - Tests binary only, not the plugin.
target_link_libraries(Tests PRIVATE juce::juce_cryptography)

set_target_properties(Tests PROPERTIES XCODE_GENERATE_SCHEME ON)

include(${Catch2_SOURCE_DIR}/extras/Catch.cmake)

if(CMAKE_GENERATOR STREQUAL "Xcode")
    set(ApotheosisDiscoveryMode PRE_TEST)
else()
    set(ApotheosisDiscoveryMode POST_BUILD)
endif()

catch_discover_tests(Tests DISCOVERY_MODE ${ApotheosisDiscoveryMode})
