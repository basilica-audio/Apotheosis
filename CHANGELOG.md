# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.4.0] - 2026-07-27

State-of-the-art DSP pass. Seven new controls, a rewritten gain-envelope
architecture, a measured (rather than assumed) true-peak ceiling, and
delivery-grade loudness metering. **Every new parameter's default reproduces
v0.2.0's output bit-for-bit** - verified against committed v0.2.0 golden
renders, not asserted - with one documented exception noted under *Changed*.

No editor changes: the seven parameters are host-visible and automatable
through the APVTS layout, and hosts surface them in their generic parameter
views. Wiring them into the photoreal editor is a separate GUI PR.

### Added

- **Style** (`limitStyle`, Classic/Transparent/Punchy/Bus/Safe, default
  Classic): the release's headline feature. The four non-Classic styles
  replace the rectangular sliding-minimum attack with a **cascaded-box FIR
  smoother** and the binary transient classifier with **two concurrent
  release followers** (a depth-capped fast stage for transient tops, a slow
  stage carrying programme gain reduction, min-combined).
  - The smoother makes zero overshoot a **structural property**: a finite
    mean of values each at least the window minimum is itself at least that
    minimum. Asserted with the safety clamp *bypassed* over 10 000
    randomised signals x 4 styles x 3 lookahead settings, so the test proves
    the algorithm rather than the clamp.
  - This is what removes low-frequency intermodulation distortion: 50 Hz
    driven 3 dB over the ceiling at 10 ms lookahead measures under 1 % THD in
    Transparent, and Classic on identical input is asserted at 3x or more.
  - Classic is the **literal v0.2.0 code path** behind a top-level dispatch,
    not a conditional threaded through it.
  - In non-Classic styles, **Attack** gains a second meaning: it sets the
    smoother's span, capped at Lookahead, with 0 ms meaning "use the full
    lookahead". Range, name and default unchanged.
- **Oversampling** (`oversampling`, 4x/8x/16x, default 4x) and **OS Filter**
  (`osPhase`, Minimum/Linear Phase, default Minimum). 8x/16x and every Linear
  Phase configuration use custom **decimator-weighted** stage specs (stage 0
  spends -106 dB on the way down vs -86 dB up), correcting JUCE's stock
  priorities - audible alias energy folds back in the decimator. Measured
  alias floors: -70 dB at 4x, -90 dB at 8x, -100 dB at 16x Linear;
  pass-through flat to +-0.1 dB to 20 kHz for all six combinations.
  Auto-derates one factor step at 96 kHz and above, two at 176.4 kHz and
  above, without changing the stored parameter value.
  **Both are prepare-latched with exactly the existing `lookahead` contract**
  - they take effect at the next host-driven `prepareToPlay()`, not live.
- **True Peak Guard** (`tpGuard`, default Off): a BS.1770-4-compliant 4x
  interpolating detector on the base-rate output, ducking by exactly the
  measured excess for exactly its duration (attack under 0.1 ms, release
  5 ms). Turns the Ceiling from a margin into a measurement.
- **Noise Shaping** (`noiseShaping`, Legacy/Weighted, default Legacy): a
  9th-order weighted error-feedback requantiser with per-channel TPDF,
  measuring at least 15 dB of F-weighted noise-floor improvement over flat
  TPDF at 16-bit. Coefficients are project-owned (least-squares fit of
  `|1 - C(z)|` to an inverse F-weighted audibility curve; Lipshitz/
  Wannamaker/Vanderkooy JAES 1991-class method) - **no proprietary curve is
  copied**. When Weighted, it supersedes `ditherShape`.
- **Delta** (`deltaListen`, default Off): monitors what the limiter removes,
  via a second filter-identical oversampler path so the subtraction actually
  nulls (a signal 6 dB under the ceiling gives a delta below -100 dBFS RMS).
  Crossfaded over 10 ms; bypasses dither; still clamped.
- **Unity Gain** (`unityGainMonitor`, default Off): trims the output by
  minus Input Gain for loudness-matched drive auditioning. Delta wins when
  both are on.
- **Loudness Range (LRA)** per EBU Tech 3342, verified against the Tech 3342
  vectors to within 1 LU (`getLoudnessRangeLu()`).
- **True-peak max-hold and maximum-gain-reduction readouts**
  (`getTruePeakMaxHoldDb()`, `getMaxGainReductionDb()`).
- **Three factory presets** (11 total): Transparent Mastering, Punchy Loud
  Style, Safe Archival (True Peak). The eight existing presets are
  byte-identical.
- **Committed v0.2.0 golden fixtures** (`tests/fixtures/v020/`, raw float32
  renders plus a manifest with settings, SHA-256 and generating platform),
  generated in the branch's first commit while the engine was still
  byte-identical to v0.2.0. Every later commit is compared against them.

### Changed

- **Latency increases by 6 samples at sample rates below 176.4 kHz** (+0 at
  and above). The True Peak Guard's alignment delay is always in the signal
  path whether the guard is on or off, so automating `tpGuard` can never
  glitch host delay compensation. It is a **pure integer delay** - no sample
  value changes - and it is constant per rate-policy tier, not fs-scaled.
  Null-test workflows comparing against a v0.2.0 render need to shift by
  exactly this constant.
- **Integrated LUFS is now spec-gated - a metering-only change, no audio
  change.** Full ITU-R BS.1770-4: 400 ms blocks at 75 % overlap, -70 LUFS
  absolute gate plus the -10 LU relative gate, two-pass over an O(1)-memory
  histogram. Verified against the EBU Tech 3341 minimum set to within
  0.1 LU. v0.2.0 shipped a documented approximation, so **Integrated
  readings will differ from v0.2.0's on the same material** - the new values
  are the correct ones. The "documented deviation" caveats are removed from
  `docs/architecture.md`.
- **The output true-peak readout is now a measurement**, produced by the
  BS.1770-4 interpolator at the base rate after downsampling, rather than an
  oversampled-domain absolute-peak estimate. Also metering-only.
- **Auto Release is buffer-size invariant in non-Classic styles**: the
  gain-reduction depth integrator runs at a fixed 100 Hz internal cadence
  instead of once per host chunk, so a render at 64 samples nulls against
  the same render at 2048 to at least 80 dB. Classic keeps the v0.2.0
  chunk-rate law verbatim, and is exempt by design.
- **State schema bumped to v3.** `stateVersion` is written as a property on
  the APVTS root tree; absent-property states are inferred as v1/v2 and all
  seven new IDs fall back to their neutral defaults. Foreign root-tree
  properties survive the round-trip untouched.
- Parameter IDs gained a binding repo contract: **never extend an existing
  `AudioParameterChoice`'s StringArray**, because choice parameters store
  normalised values and appending an entry silently re-maps every saved
  session. Guarded by a choice-mapping freeze test. This is why noise
  shaping is a new parameter rather than a third `ditherShape` entry.

### Fixed

- The Weighted dither path uses **one RNG per channel**, distinctly seeded,
  rather than v0.2.0's shared draw stream. The Legacy path keeps the shared
  stream and its exact draw order deliberately, which is what makes it
  bit-identical rather than merely equivalent.

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
