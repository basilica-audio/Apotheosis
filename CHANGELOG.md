# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- **Audio-thread safety (#14):** `TruePeakLimiterEngine::process()` now chunks any incoming block larger than the `maximumBlockSize` declared to `prepare()` down into prepare()-sized sub-blocks before handing them to `juce::dsp::Oversampling`, instead of passing an oversized block straight through. `juce::dsp::Oversampling`'s internal buffer is sized to exactly that maximum at prepare()-time, and every `processSamplesUp`/`processSamplesDown` override only guards its writes with a debug-only `jassert` (compiled out under `NDEBUG`/Release) - so an oversized block (offline bounce/render, host buffer-size renegotiation) previously risked silent heap corruption in a Release AU/VST3 build.
- **Tests (#15):** `RobustnessTests.cpp`'s "block size larger than prepared maximum" test previously constructed a buffer exactly matching (not exceeding) the prepared maximum, so it never exercised the oversized-block path fixed above. It now prepares at 256 samples and processes a 700-sample block, and also asserts the ceiling guarantee still holds on the result (not just `CHECK_NOTHROW`, which would not reliably catch silent heap corruption).

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
