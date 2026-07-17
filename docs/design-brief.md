# Apotheosis — Design Brief v2 (binding; supersedes v1's implicit spec)

Lookahead, oversampled, true-peak brickwall limiter for the master bus, built on the
documented mechanisms of the software reference class (a two-stage transient/sustain
limiting architecture, program-dependent release, stereo-link control, noise-shaped dither)
— packaged as a single deterministic, oversampled, tested plugin. Research-driven rewrite:
every new default below is sourced (see `docs/research-notes.md`, this session's
`apotheosis-research-notes.md`) or explicitly reasoned where no source exists. **No brand or
person names in parameters, UI or marketing copy** — generic descriptors only ("Attack",
"Auto Release", "Stereo Link", "Dither Shape"); the manual/research notes may cite public
sources (FabFilter Pro-L 2's documented two-stage architecture, iZotope Ozone Maximizer's
IRC lineage, the ITU-R BS.1770 true-peak spec) freely, since those are the honestly-disclosed
reference class, not implied endorsement.

## Why v1 falls short (the three core corrections)

1. **There is no Attack — v1 cannot separate "transient" from "sustained" peaks.** The
   reference class's single most load-bearing character control is a **two-stage
   transient/sustain architecture**: short peaks are caught and released back near-
   instantly regardless of the Release setting, while longer/sustained peaks are governed
   by Release; an Attack-equivalent control sets the crossover duration between the two
   ("Shorter attack times will allow the release stage to set in sooner" — official Pro-L 2
   docs, see research notes §2a). v1's sliding-window-minimum treats every peak identically:
   attack is *always* instantaneous, full stop (`docs/architecture.md`: "Decreasing gain
   (attack) is applied immediately, regardless of Release Curve"). This is a legitimate,
   different, defensible topology — not a bug — but it means v1 structurally cannot
   reproduce the reference class's best-documented loudness/punch trade-off ("short
   lookahead + long attack + fast release" — research notes §2a), because v1 has no axis
   along which to make that trade at all.
2. **Release has *shape* but no *program-dependence*.** v1's Release Curve
   (Exponential/Linear/Smooth) controls *how* a single fixed Release time is approached, not
   *what that time effectively is* given the material. The reference class's entire IRC/ARC
   lineage exists because a single time constant is a compromise: "[analyzes] source
   material and applies limiting psychoacoustically, reacting quickly to transients while
   responding more slowly to bass tones" (iZotope Ozone Maximizer IRC I, research notes §3).
   v1 has zero program-dependent behavior — the same Release value behaves identically
   whether it's chasing a single transient or riding out a dense wall of sustained material.
3. **Stereo handling and dither are both fixed with no user control, where the reference
   class exposes both.** v1's detector is unconditionally max-linked across channels ("the
   greater of the two (linked) channels' absolute values" — `TruePeakLimiterEngine.h`); the
   reference class exposes Stereo Link as a real, separately-tunable control because full
   linking can pull the whole image toward mono-triggering behavior on hard-panned content
   (research notes §2c). Separately, v1's Dither is plain flat TPDF only; the reference class
   (Pro-L 2) offers a noise-shaping choice that "push[es] more quantization noise into high
   frequencies where human hearing is less sensitive" (research notes §5) — a low-risk,
   LSB-bounded feature gap, explicitly flagged in sourcing as low-stakes but real.

## Topology (additive; v1's oversampled core is not replaced)

```
in → Input Gain → [4x oversampled: true-peak detect (Stereo Link-weighted, NEW)
       → lookahead sliding-window-minimum gain envelope
       → Attack classifier (NEW: transient vs sustain routing)
       → Release (curve-shaped, Auto-Release-modulated, NEW)
       → apply gain to lookahead-delayed signal → Clip Mix blend → hard ceiling clamp]
     → downsample → metering → Dither (Off/16/24-bit × Flat/Shaped, NEW axis) → out
```

- The 4x-oversampled detect→envelope→apply→clamp core, the lookahead sliding-window-minimum
  mechanism, the Clip Mix blend, and the final hard ceiling clamp are **unchanged** — this is
  still the architecture that makes the never-exceed-ceiling guarantee hold by construction
  (see `docs/architecture.md`, carried forward verbatim). v2 does not touch detection,
  oversampling, or the clamp.
- **Attack (NEW)** is not a gain-reduction ramp (this is not a compressor retrofit). It is a
  **classification window**: for each candidate gain-reduction event (a contiguous run of
  raw-gain samples below unity feeding the sliding-window minimum), if its duration is
  shorter than the current Attack setting, its *recovery* uses a fast, fixed near-instant
  release coefficient (mirroring the reference class's "transient stage... releases
  near-instantly regardless of the release setting" — research notes §2a) instead of the
  user's Release time; longer runs use the normal Release-governed path. At Attack = 0 ms
  (the default), every event is classified as "sustained" and the entire mechanism reduces
  to v1's existing single-path release exactly — **bit-identical regression at the default**.
- **Auto Release (NEW)** operates as a *modulator* on the effective Release time actually
  used by the (unchanged) Release Curve state machine: a slow-moving (order of seconds)
  running average of recent gain-reduction depth biases the effective release time longer
  when gain reduction has been deep/sustained recently (avoiding pumping on dense material)
  and shorter when it's been sparse/transient (recovering quickly between isolated peaks) —
  a reasoned, from-scratch implementation of the *documented qualitative principle*
  ("reacting quickly to transients while responding more slowly to... sustained content"),
  **not a reverse-engineered copy of any vendor's specific IRC/ARC algorithm**, whose exact
  DSP is proprietary and was not independently observed in this research pass. At Auto
  Release = 0% (the default), the modulator is a no-op and Release behaves exactly as in v1.
- **Stereo Link (NEW)** blends the true-peak detector's per-channel target between "shared,
  max-linked across channels" (100%, v1's only behavior, and the new default — bit-identical
  regression) and "each channel detects and limits independently" (0%). Implemented as a
  crossfade between the linked target and the per-channel target computed at every
  oversampled sample, still entirely inside the existing oversampled block — no new
  aliasing risk beyond what v1 already manages, since this changes *which* peak value feeds
  the existing detector, not the detector's own filtering.
- **Dither Shape (NEW)** is an orthogonal choice crossed with the existing bit-depth choice:
  Flat (v1's existing plain TPDF, default) or Shaped (a fixed noise-shaping feedback filter
  pushing quantization noise toward the top of the audible band, applied only when Dither
  bit-depth ≠ Off). No change to Off behavior or to Flat's existing bit-identical-when-off
  guarantee.
- All new controls stay inside the oversampled processing block where they touch the signal
  path (Attack classification, Stereo Link's per-channel detection); Auto Release only
  modulates a scalar release-time coefficient already computed per-block, and Dither Shape
  only touches the existing post-downsample dither stage — none of this changes v1's
  real-time-safety or allocation contract (`docs/architecture.md` §Real-time safety carries
  forward unmodified).

## Module specifications (authentic behaviors, generically named)

### `attack` (NEW) — transient/sustain classifier
- **0–50 ms, linear, default 0 ms.** At 0 ms (default), every gain-reduction event is
  treated as "sustained" and routed through the normal Release-governed path exactly as in
  v1 — **bit-identical to v1 at the default**, the core regression guarantee. Range chosen to
  bracket the documented usable region without adopting an unsourced competitor numeric
  default: the reference class's own docs treat "very short look-ahead times (less than
  0.1 ms)" as a real, usable operating point for a related but distinct control (Lookahead,
  unchanged in v2), and describe the attack/lookahead relationship qualitatively rather than
  numerically (research notes §2a/§2b access-gap note) — **50 ms upper bound is reasoned**
  (an order of magnitude above v1's fastest Release setting of 5 ms, giving headroom to
  route essentially all short-transient material through the fast path without swallowing
  genuinely sustained content) **not copied from a competitor spec sheet.**
- Linear rather than log-mapped, unlike Release/Lookahead: the control spans a narrower,
  more classifier-like range where the perceptually relevant region isn't log-distributed
  the way a multi-decade time constant is.

### `auto_release` (NEW) — program-dependent release modulation
- **0–100%, default 0%.** At 0% (default), Release behaves exactly as in v1 for every
  Release Curve choice — bit-identical regression. Above 0%, blends in the slow
  gain-reduction-history-biased modulation described in Topology above, scaled linearly by
  the control. Default 0% (not some nonzero "on by default" value) because the underlying
  modulation mechanism, while reasoned from the documented qualitative principle, has no
  sourced default intensity from any reference product (whose implementations are
  proprietary) — shipping it off-by-default is the honest choice pending user/listening
  feedback, consistent with this project's stated migration/honesty stance.
- Interacts with (does not replace) Release Curve: Auto Release changes the *effective
  target release time* fed into the existing Exponential/Linear/Smooth state machine, so all
  three curve shapes remain meaningful and available at any Auto Release setting.

### `stereo_link` (NEW) — detector linking amount
- **0–100%, default 100%.** 100% (default) reproduces v1's only behavior — max-linked
  detection across channels — exactly, bit-identical regression. 0% detects and limits each
  channel fully independently. Default kept at 100% (not, say, a more "modern" 50%) precisely
  because v1 shipped with hard-linked-only behavior and this is the value that reproduces it;
  changing the *default* character on an existing, tested guarantee is out of scope for a
  research-driven feature-gap fix. Mono and dual-mono host layouts are unaffected (the second
  channel doesn't exist to link against).

### `dither_shape` (NEW) — noise-shaping choice, crossed with existing `dither`
- **Choice: Flat / Shaped, default Flat.** Only has an audible effect when the existing
  `dither` (bit-depth) parameter is not Off — mirrors the reference class's precedent of
  making noise-shaping a secondary axis on top of an existing dither-amount choice rather
  than a replacement for it (research notes §5: "FabFilter Pro-L 2 allows you to choose a
  'Noise Shaping' setting: 'Off' (basic TPDF), 'Basic,' 'Optimized,' or 'Weighted'" — v2 ships
  a simpler two-way Flat/Shaped choice rather than the reference class's four-way one, a
  deliberately reduced scope: the underlying shaping-filter *coefficients* are this project's
  own fixed design, not a copy of any vendor's specific curve, and a four-way choice would
  imply more precisely-tuned distinct curves than a from-scratch v0.2.0 pass can honestly
  claim). Flat (default) is bit-identical to v1's existing dither behavior at every bit
  depth — regression guarantee.

### `input_gain`, `ceiling`, `release`, `lookahead`, `release_curve`, `dither` (bit depth), `clip_mix` — unchanged
- No sourced reason to change ranges, defaults, or behavior for any of these from v1.
  **Ceiling's existing -1.0 dBTP default is independently confirmed, not just retained**:
  research directly corroborates it as the mainstream, multi-platform convention ("-1 dBTP
  is the standard ceiling for most platforms. Spotify, Apple Music, Tidal, and Deezer all
  specify -1 dBTP for standard delivery" — research notes §4). The one adjacent finding
  (Amazon Music / high-loudness masters wanting -2 dBTP) is surfaced as a **factory preset**,
  not a default change (see below) — the existing default already sits in the best-supported
  position. Lookahead's 0.1–20 ms range is also independently corroborated (the reference
  class treats sub-0.1 ms lookahead as a real, meaningful operating point, and v1's range
  already reaches that region) — no change.

## Factory Presets (for the M2 preset system — proposed, not yet implemented)

Generic descriptors only, no names/brands. Settings are starting points, not exact renders.

| Preset | Intent | Rough settings |
|---|---|---|
| **Transparent Safety Net** | v1-compatible default: pure lookahead limiting, no new controls engaged, matches the shipped default exactly. | Input Gain 0 dB · Ceiling -1.0 dBTP · Release 50 ms · Lookahead 5 ms · Release Curve Exponential · Attack 0 ms · Auto Release 0% · Stereo Link 100% · Clip Mix 0% · Dither Off |
| **Punchy Master** | The reference-class "short lookahead + long attack + fast release" loudness/punch recipe, adapted to v2's classifier-style Attack. | Input Gain +3 dB · Ceiling -1.0 dBTP · Release 20 ms · Lookahead 1 ms · Release Curve Exponential · Attack 25 ms · Auto Release 30% · Stereo Link 100% · Clip Mix 0% |
| **Dense/Loud Modern** | Heavier gain reduction for dense, high-energy masters, biased toward the Clip Mix "clipper" character. | Input Gain +6 dB · Ceiling -1.0 dBTP · Release 30 ms · Lookahead 3 ms · Release Curve Smooth · Attack 10 ms · Auto Release 50% · Stereo Link 100% · Clip Mix 35% |
| **Wide Image Preserve** | Loosens stereo linking so hard-panned peaks in one channel don't pull the opposite channel's gain down with them. | Input Gain 0 dB · Ceiling -1.0 dBTP · Release 60 ms · Lookahead 5 ms · Release Curve Smooth · Attack 5 ms · Auto Release 20% · Stereo Link 40% · Clip Mix 0% |
| **Streaming Safe (High Loudness)** | The sourced Amazon Music / "louder than -14 LUFS" -2 dBTP guidance, as a discoverable named starting point. | Input Gain +4 dB · Ceiling -2.0 dBTP · Release 50 ms · Lookahead 5 ms · Release Curve Exponential · Attack 5 ms · Auto Release 30% · Stereo Link 100% · Clip Mix 0% |
| **Adaptive Riding** | Demonstrates Auto Release on dynamic programme material (mixed transient/sustained content) without changing anything else from the default. | Input Gain 0 dB · Ceiling -1.0 dBTP · Release 80 ms · Lookahead 5 ms · Release Curve Exponential · Attack 0 ms · Auto Release 100% · Stereo Link 100% · Clip Mix 0% |
| **Bright Clipper Blend** | Demonstrates the existing v1 Clip Mix character combined with the new fast-transient Attack path. | Input Gain +5 dB · Ceiling -1.0 dBTP · Release 40 ms · Lookahead 2 ms · Release Curve Exponential · Attack 30 ms · Auto Release 0% · Stereo Link 100% · Clip Mix 60% |
| **Clean Export (Dithered)** | The full, correctly-ordered final-stage bounce chain: dither on, shaped, at a conservative Ceiling. | Input Gain 0 dB · Ceiling -1.0 dBTP · Release 50 ms · Lookahead 5 ms · Release Curve Exponential · Attack 0 ms · Auto Release 0% · Stereo Link 100% · Clip Mix 0% · Dither 16-bit · Dither Shape Shaped |

## Guarantees & tests (Catch2; keep all still-valid v1 cases, extend for the new controls)

1. **Backward-compatible null cases at the "off" settings:** `attack = 0 ms`,
   `auto_release = 0%`, `stereo_link = 100%`, `dither_shape = Flat` together reproduce v1's
   exact output bit-for-bit (within float tolerance) across the existing v1 test-signal
   corpus (sine sweeps, near-Nyquist inter-sample-peak test signal, silence, full-scale) —
   the core regression guarantee that v2 doesn't silently change v1's sound for anyone who
   doesn't touch a new control.
2. **Attack classifier proof:** construct a synthetic test signal with (a) an isolated short
   transient (duration well below a test Attack value) and (b) a sustained loud passage
   (duration well above it); assert that with `attack` set above the transient's duration but
   below the sustained passage's duration, the transient's gain-reduction *recovers* measurably
   faster than the sustained passage's, and that this differential *disappears* (both recover
   at the plain Release rate) when `attack = 0`.
3. **Auto Release monotonicity proof:** feed a programme-like test signal alternating dense
   sustained gain reduction and sparse isolated peaks; sweep `auto_release` 0% → 100% and
   assert the measured effective release time during the dense passage increases
   monotonically with the control, while the isolated-peak release time changes markedly
   less — demonstrating the program-dependence, not just a uniform release-time shift.
4. **Stereo Link sweep:** feed a test signal with a peak in only the left channel; sweep
   `stereo_link` 100% → 0% and assert the *right* channel's measured gain reduction
   decreases monotonically toward zero (fully independent at 0%), while the *left* channel's
   gain reduction stays essentially constant across the sweep (it's always driven by its own
   peak) — proof the control changes cross-channel coupling, not each channel's own
   detection.
5. **Dither Shape spectral proof:** with `dither` fixed at 16-bit, compare the spectrum of
   the quantization-noise floor between `dither_shape = Flat` and `= Shaped` on a
   representative test signal; assert Shaped measurably redistributes energy away from a
   low/mid-frequency band toward a high-frequency band relative to Flat (a coarse spectral
   tilt assertion, not a claim of matching any specific vendor curve), and that `Flat`
   remains bit-identical to v1's existing dither output.
6. **Ceiling guarantee carried forward unconditionally:** the existing near-Nyquist
   inter-sample-peak "never exceed Ceiling" test from `tests/LimiterTests.cpp` is re-run
   across the full new-parameter space (every new control swept to its extremes, individually
   and combined) — the never-exceed guarantee must hold regardless of Attack, Auto Release,
   Stereo Link, or Dither Shape settings, since none of them touch the final hard ceiling
   clamp.
7. **State migration tolerance:** old (v1) saved state with only the seven original
   parameter IDs (no `attack`, `auto_release`, `stereo_link`, `dither_shape`) loads without
   crashing or throwing; all four new parameters fall back to their v2 defaults (which are
   also each individually the backward-compatible "off" value per guarantee 1) — this makes
   old-state migration for Apotheosis unusually simple: there is no lossy remapping needed at
   all, unlike a structural replacement (contrast with the Overture precedent, where an old
   `tone` control had no exact new equivalent). Conversely, v2 state with unknown-to-v1 IDs
   must not crash a hypothetical older build (forward-tolerant round-trip, matching the
   suite's existing unknown-ID-ignored pattern).
8. **NaN/Inf robustness on all new controls:** sweep `attack`, `auto_release`,
   `stereo_link`, `dither_shape` to their extremes combined with extreme Input
   Gain/Ceiling/Release/Lookahead/Clip Mix and confirm no NaN/Inf propagates through the
   engine (carries forward v1's existing robustness-test pattern, `tests/RobustnessTests.cpp`).
9. **Real-time-safety carry-forward:** no new allocation on the audio thread from any new
   control (Attack classification state is fixed-capacity, sized in `prepare()`; Auto
   Release's running average is a fixed-size accumulator; Stereo Link and Dither Shape are
   per-sample scalar computations) — the existing oversampling/latency/metering contracts
   from v1 (`docs/architecture.md`) remain unmodified and their existing tests remain green
   unchanged. Latency is unaffected: none of the four new controls change
   `getLatencySamples()`.
10. **Preset round-trip:** every factory preset in the table above loads, all parameter
    values land within tolerance of their specified settings, and produces no
    NaN/Inf/silence on a standard test signal, with the Ceiling guarantee (item 6) holding
    for every preset.

## Honesty & framing

- `apotheosis-research-notes.md` (this session) ships the sourced findings (quotes + URLs) —
  the Attack/Auto-Release/Stereo-Link/Dither-Shape additions in this brief are **research-
  derived from a competitor's own published help documentation (FabFilter Pro-L 2), a
  competitor's own published manual page (iZotope Ozone Maximizer), general audio-DSP
  dithering literature, and the ITU-R BS.1770 true-peak specification's documented behavior
  — not measured against, benchmarked against, or reverse-engineered from any competitor's
  actual binary/DSP.** Say so explicitly in the manual, next to each new control.
- **Auto Release's specific modulation mechanism (a slow gain-reduction-history running
  average biasing effective release time) is this project's own reasoned, from-scratch
  design**, built to satisfy the *documented qualitative principle* ("react fast to
  transients, respond more slowly to sustained/bass content") found in publicly available
  competitor documentation — it is explicitly **not** a copy or approximation of any vendor's
  specific proprietary IRC/ARC algorithm, whose internals were not observed in this research
  pass (no vendor publishes their actual DSP). This distinction must be stated in the manual,
  not just this brief.
- **Numeric defaults for the four new controls (`attack` 0–50 ms range, `auto_release` 0–100%
  default 0%, `stereo_link` 0–100% default 100%, `dither_shape` two-way default Flat) are
  reasoned engineering choices**, not numbers taken directly from a competitor spec sheet —
  the research pass's own access-gap note (research notes §6) explicitly found that exact
  competitor default ms/percentage values were not recoverable from the pages fetched. Each
  default was instead chosen to guarantee bit-identical regression to v1's existing, already-
  tested behavior (see Guarantee 1) — this is a stronger and more verifiable honesty
  commitment than matching an unverified competitor number would have been.
- Manual notes that "FabFilter Pro-L 2" and "iZotope Ozone Maximizer" are cited as documented
  public sources for the *design principles and terminology* (two-stage attack/release
  architecture; program-dependent release; noise-shaped dither), without implying
  endorsement, sponsorship, affiliation, or DSP equivalence — consistent with the existing
  README's non-affiliation language, and stronger than the Overture precedent's disclosure
  in that no numeric value in this brief is claimed to originate from those products.
- Out of scope for v2 (explicitly): a multi-algorithm "Style" selector matching the reference
  class's 8-way choice (research notes §2d) — tracked as an M3+ candidate, not a v2
  requirement, since it would require genuinely distinct limiting-character DSP per style
  rather than a parameterization of the existing engine. Custom GUI remains M3 as in v1's
  roadmap; this brief is DSP/parameter-layer only. A four-way Dither Shape choice matching
  Pro-L 2's exact option count is also explicitly descoped to a simpler two-way Flat/Shaped
  choice (see module spec above) rather than implying four distinctly-tuned curves this pass
  cannot honestly source.

## Versioning

Ships as **v0.2.0** (breaking parameter changes are acceptable pre-1.0, per suite
convention — though notably, every new parameter in this brief is purely *additive*: no v1
parameter ID, range, or default changes, and every new control's default reproduces v1's
exact prior behavior, so v0.2.0 is unusually low-risk as a migration). State migration =
tolerant import (old seven-parameter state loads with all four new IDs falling back to their
(backward-compatible) v2 defaults per Guarantee 7; unknown IDs in either direction are
ignored, not fatal). CHANGELOG documents the four new controls (Attack, Auto Release, Stereo
Link, Dither Shape) prominently as the headline v0.2.0 change, explicitly noting none of them
alter default behavior.
