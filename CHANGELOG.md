# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.3.0] - 2026-07-18

### Added

- **Photoreal skeuomorphic GUI (M3)**, replicating the suite pilot's pattern (`basilica-audio/silentium`) 1:1: a pre-rendered stone/gunmetal faceplate (`resources/gui/faceplate_apotheosis_*.png`, engraved section bays laid out per `.scaffold/gui-assets/faceplate-apotheosis-v1/layout-manifest.json`), brass filmstrip knobs (128 frames, -135deg..+135deg) for all eight continuous parameters, and three glass-covered analog needle meters - **Gain Reduction**, **True Peak**, and **LUFS** - with ~300 ms ballistic smoothing, fed from `TruePeakLimiterEngine`'s existing metering atomics. See `docs/gui-preview.png` for the rendered result and `docs/gui-components.md` for the component architecture, the three-meter dB-mapping rationale, and this plugin's specific layout choices (mixed knob/combo-box bay grids; no bespoke asset for the three discrete choice parameters).
- **Suite-reusable GUI component family copied verbatim** (`src/gui/`): `FilmstripKnob`, `FilmstripToggle` (unused here - Apotheosis has no boolean parameters), `AnalogMeter`, `BasilicaLookAndFeel`, `ImageDensity.h`.
- **Lookahead "setup" treatment**: since Lookahead is prepare-time-latched (sizes real-time buffers, changes reported latency - takes effect at the next engine restart, not live), its knob is labelled "Lookahead (Setup)", carries an accessibility description explaining why, and is enclosed in a dashed amber frame distinguishing it from the other, live-responsive knobs.
- **Stepped window scaling** (100/150/200%, via a control next to the preset bar) - no free resize, because the artwork is pre-rendered at fixed density tiers. The chosen step persists in the plugin state (a plain `uiScaleStep` property on the APVTS tree) and round-trips through host session save/reload.
- **Accessibility**: `FilmstripKnob`/`AnalogMeter` expose accessible titles, units-suffixed values, and (for meters) a read-only on-demand value interface; keyboard focus draws a visible gold focus ring; the discrete choice combo boxes are styled with `BasilicaLookAndFeel`'s WCAG-AA-verified (>= 4.5:1) gold-on-dark colour pair.
- `tests/gui/` (7 files, 23 new test cases, 103 total, all green): filmstrip frame-math edges, toggle frame-table mapping, meter ballistics step response and tick-angle interpolation, WCAG contrast verification, editor layout invariants (bay geometry vs the manifest), editor accessibility (knob/meter/choice/scale-button accessible values), and an offscreen editor snapshot (written to `build/gui-preview.png`, committed as `docs/gui-preview.png`) verified non-blank.

### Changed

- `docs/architecture.md`, `docs/manual.md`, `README.md` updated for the v0.3.0 GUI, its metering display, and the Lookahead setup-control treatment.
- CMake project version bumped to 0.3.0.

## [0.2.0] - 2026-07-16

### Added

- **Deep-dive rework (research-derived voicing, `docs/design-brief.md`/`docs/research-notes.md`):** four new controls closing documented feature gaps against the software reference class, sourced from public help/manual documentation and general DSP literature - **not measured, benchmarked, or reverse-engineered against any competitor's actual binary/DSP**. Every new control's default reproduces v1's exact prior output bit-for-bit (`tests/RegressionTests.cpp`) - none of them change what Apotheosis sounds like unless deliberately moved:
  - **Attack** (0-50 ms, default 0 ms): a transient/sustain *classifier* (not a gain-reduction ramp) - short gain-reduction events recover via a fixed, near-instant coefficient regardless of Release/Release Curve/Auto Release; longer events use the normal Release-governed path. At 0 ms every event is classified "sustained" - bit-identical to v1.
  - **Auto Release** (0-100%, default 0%): program-dependent modulation of the *effective* Release time from a slow (multi-second), asymmetric running average of recent gain-reduction depth - a from-scratch, reasoned implementation of the reference class's documented qualitative principle, **not a copy of any vendor's proprietary IRC/ARC algorithm**. No-op at 0%.
  - **Stereo Link** (0-100%, default 100%): crossfades each channel's true-peak detector input between fully max-linked (v1's only behaviour) and fully independent per-channel detection. Required the gain envelope and sliding-window-minimum to become per-channel internally; bit-identical to v1 at 100%.
  - **Dither Shape** (Flat/Shaped, default Flat): a fixed, project-owned noise-shaping filter option crossed with the existing Dither bit-depth choice, pushing quantisation-noise energy toward higher frequencies. Flat is bit-identical to v1's plain TPDF dither.
- **M2 preset system** (`src/presets/`, copied verbatim from the suite pilot `basilica-audio/nave` per `docs/preset-system-notes.md`'s replication recipe): factory/user preset discovery, save/load/rename/delete, default resolution (user Default > factory Default > built-in defaults), single-file and zip-bank import/export, and a `PresetBar` docked at the top of the editor. Eight factory presets (`presets/factory/*.json`, `docs/presets.md`) covering the v1-compatible default plus a starting point for each new v0.2.0 control.
- **German frame-string localisation** (`resources/i18n/de.txt`, `src/presets/Localisation.*`): the preset bar's interface text (not parameter names/units, which always stay English) appears in German automatically when the host system's language is German.
- `tests/AttackAutoReleaseTests.cpp`, `tests/StereoLinkDitherShapeTests.cpp`, `tests/StateMigrationTests.cpp`, `tests/RegressionTests.cpp`, `tests/PresetManagerTests.cpp`, `tests/LocalisationTests.cpp`: full coverage of the brief's ten numbered guarantees plus the M2 preset spec's minimum test list, broadening the suite from 45 to 80 Catch2 test cases.
- CI: `.github/workflows/release.yml` now creates the GitHub release object itself (idempotent `create-release` job) before the macOS/Windows build jobs attempt to attach assets to it - both jobs previously assumed the release already existed and failed with "release not found" on a fresh tag push.

### Changed

- `docs/architecture.md`, `docs/manual.md` updated for the full v0.2.0 signal path (Attack, Auto Release, Stereo Link, Dither Shape), the M2 preset system, and the i18n frame; `docs/design-brief.md` and `docs/research-notes.md` added (the binding brief and its sourcing for this pass).
- State migration: old v1 saved state (seven parameters, no v0.2.0 IDs) loads without crashing, with all four new parameters falling back to their v2 defaults - unusually simple since every new default already reproduces v1's exact behaviour (`tests/StateMigrationTests.cpp`).

## [0.1.2] - 2026-07-16

### Changed

- Housekeeping: canonical squircle icon cutout embedded into the plugin binary (`ICON_BIG`) and README/manual, org link sweep, heavy-music copy reframe, README pointed at GitHub Releases, and the signed tag-triggered release CI workflow added.

### Fixed

- **Audio-thread safety (#14):** `TruePeakLimiterEngine::process()` now chunks any incoming block larger than the `maximumBlockSize` declared to `prepare()` down into prepare()-sized sub-blocks before handing them to `juce::dsp::Oversampling`, instead of passing an oversized block straight through. `juce::dsp::Oversampling`'s internal buffer is sized to exactly that maximum at prepare()-time, and every `processSamplesUp`/`processSamplesDown` override only guards its writes with a debug-only `jassert` (compiled out under `NDEBUG`/Release) - so an oversized block (offline bounce/render, host buffer-size renegotiation) previously risked silent heap corruption in a Release AU/VST3 build.
- **Tests (#15):** `RobustnessTests.cpp`'s "block size larger than prepared maximum" test previously constructed a buffer exactly matching (not exceeding) the prepared maximum, so it never exercised the oversized-block path fixed above. It now prepares at 256 samples and processes a 700-sample block, and also asserts the ceiling guarantee still holds on the result (not just `CHECK_NOTHROW`, which would not reliably catch silent heap corruption).
- **Dither ceiling overshoot (#9):** dither (16-bit/24-bit TPDF) was added after the final oversampled-domain ceiling clamp and after downsampling, with no subsequent clamp - a base-rate output sample could exceed the nominal Ceiling by up to ~1 LSB. Dither is now re-clamped to the same Ceiling immediately after being applied, so the never-exceed-Ceiling guarantee holds with dither on too. Most noticeable at very low Ceiling settings, where the (proportional) headroom margin shrinks below dither's fixed absolute LSB size.

## [0.1.1] - 2026-07-14

### Fixed

- Release Curve and Dither combo boxes rendered empty because `ComboBoxAttachment` does not auto-populate items from `AudioParameterChoice`; the editor now explicitly populates both boxes from the live APVTS parameter's `getAllValueStrings()` before attaching.

## [0.1.0] - 2026-07-14

### Added

- Project bootstrap: README, license, contributing guide, architecture and build docs, ADRs, and CI workflow.
- DSP core: initial working Apotheosis signal path (Input Gain, 4x-oversampled true-peak detection, lookahead gain-reduction envelope, release smoothing, ceiling clamp) with unit tests.
- **Release Curve** parameter (Exponential/Linear/Smooth, default Exponential): shapes the release (increasing-gain) phase only - attack always stays instantaneous via the lookahead minimum, regardless of curve. Exponential matches the original v0.1 one-pole behaviour exactly.
- **Clip Mix** parameter (0-100%, default 0%): blends the transparent gain-reduction limiter path with an alternate tanh soft-clip "clipper" path, applied directly to the lookahead-delayed signal. Both paths (and every blend between them) pass through the same final hard ceiling clamp, so the never-exceed-ceiling guarantee holds at any Clip Mix setting; 0% is bit-identical to the pure limiter path.
- **Dither** parameter (Off/16-bit/24-bit, default Off): TPDF dither added after downsampling, at the output word length. Off is bit-identical to the pre-dither signal path.
- **Metering** (engine-side): gain reduction, output true peak, and Momentary (400 ms)/Short-Term (3 s)/Integrated LUFS loudness (K-weighted per a documented, real-time-safe approximation of ITU-R BS.1770-4), published via relaxed atomics on `TruePeakLimiterEngine`/`ApotheosisAudioProcessor` for a future GUI (roadmap M3) or any host/test consumer. No visual display yet - this is the DSP computation and readout API.
- Editor controls for the new Release Curve/Dither/Clip Mix parameters (two combo boxes + one knob), so every automatable parameter has a working v0.1 control.
- `docs/manual.md`: full user manual (what Apotheosis is, where it sits in a chain, signal flow, complete parameter reference, usage tips).
- Broadened Catch2 suite (19 -> 44 test cases): Release Curve/Dither/Clip Mix unit and regression tests, metering tests (idle defaults, gain-reduction/true-peak/LUFS behaviour, reset semantics), sample-rate sweep (44.1-192 kHz) with true-peak-ceiling verification at every rate, mono/stereo/rejected bus-layout coverage, long-run (several-second) NaN/Inf stability including the new metering state, and rapid automation of every parameter.

### Changed

- `docs/architecture.md`, `README.md`, and `CLAUDE.md` updated to describe the full v0.1.0 signal path (Release Curve, Clip Mix, Dither, metering) and parameter table; README's roadmap table corrected to match the repository's actual GitHub milestones (M1 DSP & tests, M2 presets/state, M3 GUI & a11y, M4 release).
