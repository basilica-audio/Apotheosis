# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **A factory-preset headroom gate** (`tests/PresetHeadroomTests.cpp`). Every shipped factory
  preset is rendered through the real `AudioProcessor` at 48 kHz against the suite reference
  programme — four plucked notes spanning E1 41.203 Hz to A5 880.000 Hz, twelve harmonics each,
  peak-normalised to **−12 dBFS**, the level a track is conventionally recorded at and therefore
  the level a preset's author must be assumed to have voiced for — and its output peak asserted
  **below 0 dBFS**. A preset added later that clips this reference fails here.

  The case asserts how many factory presets it exercised (11), so a preset library that
  stopped loading is distinguishable from every preset passing, and it measures **both** ways a
  user arrives at a preset: a restored session (state first, then `prepareToPlay()`, so every
  smoothed stage is primed at the preset's own values) and a mid-session click in the preset
  browser (parameters jump while the DSP is still primed for the old ones). Those are not the
  same measurement — in `basilica-audio/Aureate` the difference was a 17.6 dB blast the
  session-load path could not see at all. The recall path is held to "below full scale **or**
  below where you already were", so a transition is blamed only for clipping it *introduced*.

  **Nothing needed fixing.** All 11 presets already pass on both paths, at −5.87 to
  −12.01 dBFS on session load (worst *Punchy Loud Style* at −5.87 dBFS) and no worse
  than −6.33 dBFS on recall; the departure state renders at −12.01 dBFS. No
  preset is raised toward the line either — the gate is a ceiling, not a level-matching target,
  and relative loudness between presets stays a taste question.

## [0.6.1] — 2026-08-28
### Changed

- **The suite now presents itself as Basilica Audio in every host.** `COMPANY_NAME` moves from
  `Yves Vogl` to `Basilica Audio`, so Apotheosis files under the brand in Logic's plugin manager,
  Cubase's vendor column and Reaper's FX browser instead of under a person's name. **Plugin
  identity is untouched** and no session is affected: the VST3 class ID derives from
  `PLUGIN_MANUFACTURER_CODE` + `PLUGIN_CODE` alone (JUCE 8.0.14, `juce_VST3ModuleInfo.h`'s
  `VST3Interface::jucePluginId`) and the Audio Unit triple stays `(aufx, <PLUGIN_CODE>, Yvsv)` -
  both diffed on a real build before and after the change. The bundle ID stays
  `com.yvesvogl.apotheosis` on purpose, because changing it is what would break existing projects, and
  `COMPANY_COPYRIGHT` still names the copyright holder rather than the trading name. See
  [`docs/branding.md`](docs/branding.md) and basilica-audio/.github ADR 0001.
- **User presets now live under `Basilica Audio`, and the ones you already saved come with them.**
  The folder moves to `~/Library/Audio/Presets/Basilica Audio/Apotheosis/` (macOS) and
  `%APPDATA%\Basilica Audio\Apotheosis\Presets\` (Windows). On first launch `PresetManager` copies
  every preset out of the old `Yves Vogl` folder into the new one. It **copies rather than moves**,
  so an older build - or a downgrade - still finds its presets where it left them, and it never
  overwrites a file already present under the new name. Nothing is deleted, ever.
- **Plugin metadata now carries the vendor URL, the copyright string, a real description and
  the VST3 sub-category.** `COMPANY_WEBSITE`, `COMPANY_COPYRIGHT` and `DESCRIPTION` were never
  set, so a shipped bundle carried an empty `NSHumanReadableCopyright`, an empty VST3 vendor
  URL, and an AU `description` that was just the plugin name again; `VST3_CATEGORIES` fell back
  to JUCE's bare `Fx` default, which filed every plugin in the suite under the same
  undifferentiated heading in a VST3 host's browser. Apotheosis now declares
  `Fx Dynamics Mastering` (JUCE 8.0.14, `juce_add_plugin`). **Plugin identity is unchanged** — the VST3 class
  ID is derived from `PLUGIN_MANUFACTURER_CODE` + `PLUGIN_CODE` alone
  (`juce_VST3ModuleInfo.h`'s `VST3Interface::jucePluginId`) and the AU type/subtype/manufacturer
  triple is untouched, so existing sessions keep resolving to the same plugin.

### Fixed

- **Release notes are the changelog again, not a list of PR titles.** `release.yml` now builds the
  release body from this file's section for the tag being released, via the suite-wide
  `basilica-audio/.github/release-notes` action, and appends what a downloader actually needs: what
  each archive contains, the signing status per platform stated accurately (macOS signed, notarised
  and stapled; Windows **not** code-signed, so SmartScreen will warn), the install paths, the AU
  rescan hint, and links to the manual and the product page. A tag whose version has no section in
  this file now fails the release job rather than publishing an empty page.
- **The README no longer tells users the binaries do not exist.** The Installation section
  said *"No pre-built binaries are published yet"* while the banner four lines above it linked
  the Releases page, and the banner in turn described the macOS builds as *"currently
  unsigned"*. Both claims were false. The Installation section now describes the actual
  download-and-copy flow, and the banner states what the release workflow actually produces:
  verified against the shipped `v0.6.0` `.component` with `codesign --verify --strict`
  (`Developer ID Application: Yves Vogl (M5WT732AY5)`), `spctl -a -t open`
  (`source=Notarized Developer ID`) and `stapler validate`.
- **The documented factory-preset count matches what ships** (eight -> eleven); `presets/factory/` holds 11.
- **Removed committed scratch/diagnostic test files** that were documented in their own
  headers as throwaway probes: `tests/DiagTest.cpp`.

### Added

- **A `Documentation` section in the README** pointing at the user manual, the factory-preset
  reference, the changelog and the product page — the manual was only reachable from a
  sentence in the middle of the Signal flow section.

## [0.6.0] - 2026-08-20

An accessibility release. Every knob on the victorian faceplate is now reachable and
operable from the keyboard alone, with step sizes a person can actually use. Nothing in
the audio path changed - a v0.5.0 session loads and renders bit-identically.

### Added

- **WAI-ARIA-style keyboard stepping on the knobs** (`src/gui/KeyboardSteps.h`, PR #32).
  Arrow moves 1% of the control's range, Shift+Arrow 0.1% (the keyboard analog of the
  suite's Shift-drag fine mode), PageUp/PageDown 10%, Home/End the range extremes. Steps
  are taken in the slider's proportional domain, so a skewed range sweeps as evenly under
  the arrow keys as it does under a mouse drag, and the result is still snapped to the
  parameter's own interval grid - quantisation is never violated. This needed its own
  helper rather than just a focus flag: JUCE's stock handler steps by the raw parameter
  interval (0.01 dB across Input Gain's 36 dB range - 3600 presses end to end) and
  refuses outright the moment any modifier key is down, so Shift+Arrow did nothing at all.
  Ctrl/Cmd-modified arrows are deliberately left to the host as shortcuts.
- Two new `tests/gui/EditorAccessibilityTests.cpp` cases pinning the contract: focus
  reachability of all three knobs and the scale button (with a count assertion, so a
  zero-match loop cannot pass vacuously), and coarse/fine/page/Home/End stepping plus
  Ctrl/Cmd passthrough on Input Gain.

### Fixed

- **The three knobs (Input Gain, Ceiling, Release) could not be reached by keyboard at
  all** (PR #32). `juce::Slider::init()` ships `setWantsKeyboardFocus(false)` (JUCE
  8.0.14, `juce_Slider.cpp:1461`) and `MasterCropKnob` never opted back in, so Tab
  skipped straight past all three, the WCAG 2.4.7 focus ring already drawn in `paint()`
  could never appear, and no key press ever reached a knob. They now take focus in
  reading order and show their ring while they hold it.

### Known limitations

- This release covers **keyboard** operation (WCAG 2.1.1, 2.4.7). Assistive-technology
  increment and decrement actions - VoiceOver's rotor, NVDA's value adjustment - do not
  go through `keyPressed()`; they go through JUCE's accessibility value interface, which
  still reports the raw parameter interval as its step size. A screen-reader user
  therefore still moves Input Gain 0.01 dB per action. Closing that gap means giving each
  control a custom `AccessibilityHandler` with its own value interface, which is the next
  step and is not part of this release.

## [0.5.0] - 2026-08-19

The M3 GUI release: the previous filmstrip-knob/AnalogMeter editor is replaced by the
photoreal "victorian" faceplate, reusing the suite-wide component family piloted by
aureate's M3 GUI (`HubNeedle`, `MasterCropKnob`, `SubtractiveGlow`, `Flicker`).

### Added

- **M3 photoreal GUI (victorian design)** (PR #24). A grand gain-reduction meter plus
  three small meters (input level, output level, and true-peak margin, i.e. Ceiling
  minus measured true peak), all four needles sharing one convention: rest is the
  calmest/safest reading on the left, deflection to the right as the reading gets
  hotter. Three physical knobs (Input Gain, Ceiling, Release); every other parameter
  remains fully automatable/preset-only - mapping table, the measured (numeral-free)
  dial sweeps, and the rationale in `docs/gui-mapping.md`.
- Four-tube bay whose glow follows limiting intensity (`SubtractiveGlow`, subtractive
  model): idle at t ~ 0.75 with flicker, rising to the hard t = 1.0 ceiling (identical
  to the baked master) as gain reduction reaches the grand needle's full-scale 12 dB -
  the tubes and the needle top out together.
- New real-time-safe `getInputLevelDb()` / `getOutputLevelDb()` peak metering on the
  processor.
- Sample-rate-matrix reprepare test: one instance driven through 44.1k -> 96k -> 192k ->
  44.1k with parameter churn between every reprepare, for both the default and heaviest
  (16x Linear Phase) oversampling modes, asserting `getLatencySamples()` matches the
  manual's published latency table at every step (PR #30).

### Fixed

- The allocation-guard self-test's canary was a plain new-expression, which [expr.new]
  permits the compiler to elide, and whose apparent pass was in fact masked by Catch2's
  own internal allocations (measured: count 4 with the old pattern, 0 for the bare
  canary under -O2). The canary now uses a direct `::operator new` call written through
  a volatile pointer, reporting exactly 1 allocation in both Debug and Release - the
  suite-wide elision-safe pattern from requiem/triptych/silentium (PR #30).

### Changed

- `docs/manual.md`: full reported-latency table across every Oversampling x OS Filter
  combination at 44.1/48/96/192 kHz, "what the oversampling factors buy you" (measured
  alias floors and pass-through flatness), "what is verified, and to what bound", and a
  "Known limitations" section (PR #28).
- Branding: v3 flat squircle icon (no dish/ring) (PR #29).

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
  - Low-frequency behaviour, measured: 50 Hz driven 3 dB over the ceiling at
    10 ms lookahead measures well under the 1 % THD bound in Transparent
    (~1.7e-8). **Classic measures identically clean at that setting** - this
    engine's sliding-minimum window has always been lookahead-sized, so at
    10 ms it already bridges the 50 Hz half-period. The distortion the
    smoother addresses is therefore a narrower win than a Classic-vs-styles
    THD gap would suggest; what the test does establish is that the window is
    what does the work (shrink the lookahead to 3 ms and the same Transparent
    render distorts to 5.5 %).
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
- Both TPDF generators drew their two random samples in a single expression
  (`rng.nextFloat() - rng.nextFloat()`), whose operand evaluation order C++
  leaves unspecified. Since each draw advances the RNG, compilers that chose
  opposite orders produced exactly **negated** dither noise, so a seeded
  render was not reproducible across toolchains (Windows disagreed with
  macOS). The draws are now sequenced explicitly. Audibly inconsequential -
  TPDF noise is symmetric, so both variants were equally valid dither - but
  it makes the seeded-fixture bit-exactness contract hold on every platform.

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
