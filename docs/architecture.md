# Architecture

## Signal flow

```mermaid
flowchart LR
    IN[Input] --> GAIN[Input Gain<br/>-12..+24 dB]
    GAIN --> UP[4x Oversample]
    UP --> DET[Per-channel true-peak<br/>detection, Stereo Link-weighted]
    DET --> MIN[Lookahead sliding-<br/>window minimum, per channel]
    MIN --> ATT[Attack classifier<br/>transient vs sustain]
    ATT --> REL[Release<br/>curve-shaped, Auto-Release-modulated]
    REL --> APPLY[Apply gain to<br/>lookahead-delayed signal]
    APPLY --> CLIP[Clip Mix blend<br/>tanh soft-clip path]
    CLIP --> CLAMP[Hard ceiling clamp]
    CLAMP --> DOWN[4x Downsample]
    DOWN --> METER[LUFS / true-peak<br/>metering]
    METER --> DITHER[Dither<br/>Off / 16-bit / 24-bit x Flat / Shaped]
    DITHER --> OUT[Output]
```

Everything from the 4x oversample through the hard ceiling clamp runs entirely inside the oversampled domain, owned by `TruePeakLimiterEngine` (`src/dsp/TruePeakLimiterEngine.{h,cpp}`). Detection and gain-reduction *application* happen on the same high-resolution samples - the engine never detects an inter-sample peak at 4x and then tries to correct it after downsampling back to the host's rate. Metering and Dither are the only stages that operate at the base (post-downsample) rate - see their dedicated sections below.

**v0.2.0** (`docs/design-brief.md`) adds four research-derived controls - Attack, Auto Release, Stereo Link, Dither Shape - each purely additive, each defaulting to the value that reproduces v1's exact prior behaviour bit-for-bit (see "v0.2.0 additions" below and `tests/RegressionTests.cpp`). This is also why the gain envelope and the sliding-window-minimum, described as a single shared instance throughout the rest of this document for v1, are **per channel** as of v0.2.0 - see "Stereo Link" below for why, and `TruePeakLimiterEngine.h`'s class-level docs for the exact mechanism that keeps them numerically identical to v1's single shared envelope at the (default) 100% Stereo Link setting.

## Module map

| Directory | Responsibility |
|---|---|
| `src/dsp` | All audio-thread DSP: `TruePeakLimiterEngine`, the complete signal chain (Input Gain, 4x-oversampled per-channel true-peak detection, lookahead gain-reduction envelope, Attack classifier, curve-shaped/Auto-Release-modulated release smoothing, Clip Mix blend, ceiling clamp, base-rate LUFS/true-peak metering, Dither Flat/Shaped). No allocation, locks, or I/O once `prepare()` has run. Independent of `juce::AudioProcessor` so it is directly unit-testable (see `tests/LimiterTests.cpp`, `tests/LatencyTests.cpp`, `tests/DspFeatureTests.cpp`, `tests/MeteringTests.cpp`, `tests/AttackAutoReleaseTests.cpp`, `tests/StereoLinkDitherShapeTests.cpp`, `tests/RegressionTests.cpp`). |
| `src/params` | Parameter layout and `AudioProcessorValueTreeState` definitions - parameter IDs, ranges, defaults. Single source of truth for what a preset captures. |
| `src/presets` | The M2 suite-wide preset system (`PresetManager`, `PresetBar`, `Localisation`) - copied verbatim from the pilot implementation (`basilica-audio/nave`, see `docs/preset-system-notes.md`'s replication recipe), with only the small per-plugin config surface (`PresetManagerConfig`, factory `BinaryData::` asset list) written for Apotheosis. See "M2 preset system" and "i18n frame" below. |
| `src/PluginProcessor.*` | Host plumbing: APVTS construction, `prepareToPlay`/`processBlock`/`reset`, latency reporting, state save/load, `PresetManager` construction/startup-default resolution, and thin metering-getter delegation to `TruePeakLimiterEngine`. Reads APVTS values and pushes them into `TruePeakLimiterEngine` every block; does not implement any DSP itself. |
| `src/PluginEditor.*` | A simple, functional v0.1/v0.2 GUI: one rotary slider per continuous parameter (bound via `SliderAttachment`) plus three combo boxes for the discrete Release Curve/Dither/Dither Shape choices (`ComboBoxAttachment`), with the M2 `PresetBar` docked at the top. Every automatable parameter has a working control. A custom vector-drawn GUI (including a visual metering display) is a later milestone (M3). |

Dependency direction is one-way: `PluginEditor` -> `params` (via attachments) and `PluginProcessor` -> `params` + `dsp`. `src/dsp` has no upward dependency on the processor or UI, which is what keeps `TruePeakLimiterEngine` testable in isolation.

## Detection and gain reduction: why both happen in the oversampled domain

A true-peak limiter has to guarantee the *reconstructed, continuous-time* signal never exceeds the ceiling - not just its sample values. Measuring the true peak at 4x oversampling but only *correcting* it afterwards, at the base rate, leaves a gap: the correction can't act on the very inter-sample energy that was detected, because by the time you're back at the base rate that information no longer exists as separate samples.

`TruePeakLimiterEngine` instead upsamples once (`juce::dsp::Oversampling`, half-band polyphase IIR, `useIntegerLatency = true`), then does **everything** - peak detection, the lookahead minimum-gain envelope, release smoothing, gain multiply, and a final hard ceiling clamp - directly on the 4x-rate samples, before downsampling back. The gain reduction therefore acts on exactly the same high-resolution representation that produced the true-peak reading, so the guarantee holds by construction rather than by inference.

## Lookahead: instantaneous attack without clipping

There is no separate "attack time" control. Instead:

1. For every oversampled sample, `TruePeakLimiterEngine` computes the **raw gain** needed right now to hit the (headroom-adjusted) ceiling: `min(1, ceiling / peak)`, where `peak` is the greater of the two (linked) channels' absolute values.
2. This raw-gain stream feeds a **sliding-window minimum** (a monotonic deque, `pushSlidingMin()` in `TruePeakLimiterEngine.cpp`) with a window covering "now" through `lookaheadSamplesOS` samples into the future. Because a monotonic deque naturally reports "the minimum value seen so far in the retained window" at every push, associating that minimum with the sample sitting `lookaheadSamplesOS` positions *behind* the newest arrival is exactly a lookahead operation - no separate attack time constant is needed, because the future peak is already known.
3. The **audio signal itself** is delayed by the same `lookaheadSamplesOS` (a small ring buffer per channel, `delayPushAndRead()`), so the gain value computed for "now" lines up sample-for-sample with the (delayed) audio it multiplies.
4. **Release** is the only smoothed phase, evaluated at the oversampled rate: when the required gain is *increasing* (releasing) rather than decreasing (attacking), `currentGain` moves towards the lookahead-minimum gain, shaped by the selected **Release Curve** (see below). Decreasing gain (attack) is applied immediately, *regardless of Release Curve* - it is already "in the past" as far as the raw detection stream is concerned, so there is nothing to smooth without reintroducing overshoot.

This is O(1) amortised per sample (the deque's total push/pop work is bounded by the number of samples processed) and uses only fixed-capacity buffers allocated in `prepare()` - no allocation on the audio thread.

## Release Curve: shaping the release phase

`ParamIDs::releaseCurve` (`TruePeakLimiterEngine::ReleaseCurve`) selects the shape of step 4 above - only the release (increasing-gain) phase, never attack:

- **Exponential** (index 0, default) - the original v0.1 one-pole ramp: `currentGain = target + (currentGain - target) * releaseCoeff`, where `releaseCoeff = exp(-1 / (releaseSeconds * oversampledRate))`. Fast initial recovery that tapers off logarithmically.
- **Linear** (index 1) - `currentGain` moves towards the target at a constant per-sample rate (`linearStepPerSample`, derived from Release so the full 0..1 gain range recovers in one Release-time-worth of samples), rather than tapering. More mechanical-sounding but very predictable.
- **Smooth** (index 2) - a two-stage cascade of the *same* one-pole coefficient (`smoothReleaseStage` follows the target, then `currentGain` follows `smoothReleaseStage`), i.e. a critically-damped second-order response. This gives a softer, overshoot-free onset to the release than a single pole, at the cost of an overall slower perceived release for the same Release time - the classic trade-off of adding a pole to a smoother.

The two curve-internal state variables (`currentGain`, `smoothReleaseStage`) are kept in lock-step on every **attack** sample (both snap to the lookahead-minimum gain), so switching Release Curve - or switching between attack and release - never resumes from a stale, lagging value. `tests/DspFeatureTests.cpp` verifies Exponential's default behaviour is unchanged from the pre-M1 engine, that the three curves produce materially different release trajectories, and that none of them ever overshoots unity gain or produces non-finite output.

## Clip Mix: an alternate soft-clip "clipper" path

`ParamIDs::clipMix` (0-100%, default 0%, smoothed like Ceiling) blends the transparent gain-reduction limiter path with an alternate waveshaping path, both evaluated per oversampled sample:

```
limiterSample = delayedSample * currentGain                                  // the existing gain-reduction path, unchanged
clipped       = clipTargetLinear * tanh(delayedSample / clipTargetLinear)     // independent tanh soft-clip, no gain envelope at all
outSample     = limiterSample + (clipped - limiterSample) * clipMixAmount
```

At 0% (default), the `if (clipMixAmount > 0.0f)` branch never executes, so the engine's output is bit-identical to the pure limiter path (regression-tested in `tests/DspFeatureTests.cpp`). At 100%, `outSample` is driven entirely by the tanh curve, independent of the lookahead gain envelope - a common modern loudness-maximiser technique.

Because the tanh curve generates new high-frequency harmonic content (unlike the linear gain-reduction path), its target uses extra headroom on top of `headroomMarginDb`, scaled by `clipMixAmount` (`TruePeakLimiterEngine::clipExtraHeadroomDb`, 1.0 dB at 100% mix, zero effect at 0%) to absorb the correspondingly larger downsample reconstruction-filter ripple. Regardless of the blend, the same unconditional final hard ceiling clamp (see below) still applies to `outSample` - the never-exceed-ceiling guarantee holds at every Clip Mix setting, verified directly in `tests/DspFeatureTests.cpp` at 100% mix using the same near-Nyquist inter-sample-peak test signal as `tests/LimiterTests.cpp`.

## v0.2.0 additions (Attack, Auto Release, Stereo Link, Dither Shape)

Research-derived from a competitor's own published help documentation (FabFilter Pro-L 2), a competitor's own published manual page (iZotope Ozone Maximizer), general audio-DSP dithering literature, and the ITU-R BS.1770 true-peak specification's documented behaviour - **not measured against, benchmarked against, or reverse-engineered from any competitor's actual binary/DSP**. See `docs/research-notes.md` for the full sourcing and `docs/design-brief.md` for the reasoning behind every new default. None of the four controls below change v1's default behaviour: each one's default is, individually, the value that reproduces v1's exact prior output - `tests/RegressionTests.cpp` verifies this directly with a bit-for-bit comparison across a v1-style test-signal corpus.

### Attack: transient/sustain classification

`ParamIDs::attack` (0-50 ms, linear, default 0 ms) is **not a gain-reduction ramp** (this is not a compressor retrofit) - it is a *classification window* layered on top of the existing lookahead sliding-window-minimum mechanism, mirroring the reference class's documented two-stage transient/sustain architecture (a fast transient stage that "releases near-instantly regardless of the Release knob", and a slower sustain stage the Release control actually shapes - `docs/research-notes.md` S2a).

For each channel, `TruePeakLimiterEngine::processChunk()` tracks how many consecutive oversampled samples the **windowed** (lookahead-min) gain has stayed below unity - a candidate gain-reduction "event". This is deliberately measured on the windowed gain, not the raw pre-window gain: raw per-sample gain dips below unity only within each audio cycle's brief peak excursion for any oscillating programme material and returns to unity near every zero-crossing, so a raw-gain-based duration would misclassify essentially all continuous, genuinely-sustained loud material as a string of ultra-short "transients" - the windowed gain (the same signal that already drives the attack/release envelope) persists below unity for the actual perceptually-relevant duration a loud passage keeps the limiter engaged, which is the duration this classifier needs.

When a channel enters its release (recovering-gain) phase, if the most recently completed event's duration was shorter than the current Attack setting, its recovery uses a **fixed, near-instant** release coefficient (`TruePeakLimiterEngine::fastAttackReleaseMs`, 1 ms - independent of Release, Release Curve, or Auto Release entirely, mirroring the reference class's "transient stage releases near-instantly regardless of the release setting"). Longer events use the normal Release-governed path (Release Curve, optionally Auto-Release-modulated - see below). At Attack = 0 ms (the default), no event's duration can ever be shorter than 0, so every event is classified "sustained" and the engine always takes the normal path - **bit-identical to v1** at the default.

### Auto Release: program-dependent release-time modulation

`ParamIDs::autoRelease` (0-100%, default 0%) modulates the *effective* Release time fed into the existing Exponential/Linear/Smooth curve state machine, based on a slow-moving (2 second time constant, `TruePeakLimiterEngine::autoReleaseTimeConstantSeconds`) running average of recent gain-reduction depth (`autoReleaseDepthAvgDb`, updated once per processed chunk from that chunk's own measured depth - a one-chunk-delayed, causal update, the same "smoothed value computed once per chunk" granularity `ceilingSmoothed`/`clipMixSmoothed` already use). This is a reasoned, from-scratch implementation of the *documented qualitative principle* found in the reference class's IRC/ARC lineage ("reacting quickly to transients while responding more slowly to... sustained content", `docs/research-notes.md` S3) - **not a reverse-engineered copy of any vendor's specific proprietary algorithm**, whose internals were not observed in this research pass.

The modulation is deliberately **asymmetric** around the idle baseline (`depthNorm == 0`, i.e. "nothing has needed gain reduction recently"): depth approaching the reference maximum (`autoReleaseModDepthReferenceDb`, 4 dB - a genuinely sustained passage saturates the average toward this) can lengthen the effective Release time up to 2.5x at 100% Auto Release (`autoReleaseLengthenRangeFraction`), while an idle/near-zero average only shortens it modestly, down to 0.7x (`autoReleaseShortenRangeFraction`). This asymmetry is what makes an isolated, brief peak's own post-peak recovery change only mildly across the full Auto Release range (its own averaged depth barely moves off the idle baseline, since one brief peak against a multi-second averager stays close to it) while a sustained, dense passage's recovery changes dramatically (its averaged depth saturates near the reference) - `tests/AttackAutoReleaseTests.cpp` verifies both the monotonic lengthening on a dense passage and the comparatively small swing on an isolated peak directly. At Auto Release = 0% the modulation multiplier is exactly `1.0f` regardless of the averaged depth (`0.0f * anything == 0.0f` in both branches of the asymmetric formula), so the effective Release time is bit-identical to the plain Release value v1 used - **bit-identical to v1** at the default.

Auto Release never affects the Attack classifier's fixed fast-path coefficient (above) - only the normal Release-curve-governed path.

### Stereo Link: per-channel detection crossfade

`ParamIDs::stereoLink` (0-100%, default 100%) blends each channel's true-peak detector input between "shared, max-linked across channels" (100%, v1's only behaviour) and "each channel detects and limits fully independently" (0%). Implementing this requires each channel to have its own gain envelope and sliding-window-minimum state - v1's single shared `currentGain`/`smoothReleaseStage`/sliding-window are therefore **per channel** as of v0.2.0 (`TruePeakLimiterEngine::maxChannels == 2`, matching the mono/stereo-only bus-layout contract).

At every oversampled sample, each channel's detector input is a crossfade: `peakForChannel = linkedPeak * stereoLinkAmount + perChannelPeak[channel] * (1 - stereoLinkAmount)`, where `linkedPeak` is the same max-across-channels value v1 always used. At Stereo Link = 100% (default), `peakForChannel` equals `linkedPeak` for *every* channel - each channel's independent envelope receives an identical input sequence to every other channel's, so all channels converge on the exact same `currentGain` trajectory v1's single shared envelope produced (same arithmetic, same inputs, same result) - **bit-identical to v1** at the default. At 0%, each channel's detector sees only its own peak, fully decoupling cross-channel triggering (useful when hard-panned content would otherwise pull the whole stereo image toward mono-triggering behaviour on a fully linked detector). `tests/StereoLinkDitherShapeTests.cpp` verifies the sweep directly: a peak in only the left channel produces a monotonically-decreasing (toward zero) pull on the right channel's gain reduction as Stereo Link decreases, while the left channel's own gain reduction stays essentially constant (it's always driven by its own peak, never by the crossfade).

This all happens entirely inside the existing oversampled block - no new aliasing risk beyond what v1 already manages, since Stereo Link changes *which peak value feeds* each channel's detector, not the detector's own filtering.

## Latency model

Reported latency (`TruePeakLimiterEngine::getLatencySamples()`, surfaced via `AudioProcessor::setLatencySamples()` in `prepareToPlay()`) is the sum of two independent, well-defined quantities:

```
totalLatencySamples = lookaheadSamplesBase + detectionLatencySamplesBase
```

- `lookaheadSamplesBase = round(LookaheadMs / 1000 * sampleRate)` - the Lookahead parameter, converted to base-rate samples.
- `detectionLatencySamplesBase = round(oversampler.getLatencyInSamples())` - the 4x oversampler's own round-trip (up + down) latency, which JUCE reports directly in base-rate samples when `useIntegerLatency = true` (`juce::dsp::Oversampling::getLatencyInSamples()`, JUCE 8.0.14).

**Lookahead is a "setup" parameter, not a live-automatable one.** `TruePeakLimiterEngine::setLookaheadMs()` only *latches* a new value; it is applied - resizing the lookahead delay buffer and the sliding-window-minimum's ring buffers - only inside the next `prepare()` call. `PluginProcessor::prepareToPlay()` seeds it from the APVTS before calling `engine.prepare()`, matching the sibling plugins' "seed before prepare" idiom. This is a deliberate scope decision: Lookahead directly changes the plugin's reported latency and the size of real-time buffers, neither of which should change mid-block on the audio thread. A host-side parameter change to Lookahead therefore only takes effect the next time the host re-prepares the plugin (sample-rate change, bypass toggle in most hosts, etc.), not instantaneously. InputGain, Ceiling, Release, and Clip Mix remain fully live-automatable, smoothed per block; Release Curve and Dither are cheap discrete-mode switches applied every block (no allocation, no buffer resize), so they can change instantly but are not zipper-smoothed - the same "discrete choice, not a continuous control" treatment the sibling plugins give their own voicing/mode selectors.

## Internal headroom margin

The gain-reduction *target* used internally is not the user-facing Ceiling directly, but `ceiling - headroomMarginDb` (0.3 dB, `TruePeakLimiterEngine::headroomMarginDb`). This absorbs the small amount of passband ripple/overshoot the oversampler's own downsampling (anti-imaging) filter can introduce when reconstructing an already-limited oversampled signal back to the base rate. Regardless of whether this margin is exactly right for a given input, a **final hard clamp** to the exact nominal ceiling is applied to every oversampled sample right before downsampling (see `process()` in `TruePeakLimiterEngine.cpp`) - the never-exceed guarantee does not depend on the margin's precision, only on this backstop.

## Dither

`ParamIDs::dither` (Off/16-bit/24-bit, default Off) adds TPDF (triangular-probability-density-function) noise, computed as `(rng.nextFloat() - rng.nextFloat()) * lsb` per sample per channel (two independent `juce::Random::nextFloat()` draws subtracted, giving a triangular distribution in `(-lsb, +lsb)`), where `lsb = 2^(1-16)` or `2^(1-24)`. This is applied **after** `oversampler->processSamplesDown()`, i.e. at the base rate, at the output word length - the conventional placement for dither, and deliberately *not* inside the oversampled loop (dithering before downsampling would have the downsample anti-imaging filter partially filter the dither itself, defeating its flat/triangular spectral intent at the base rate).

Off (the default) performs no computation and is bit-identical to the pre-M1 signal path (regression-tested in `tests/DspFeatureTests.cpp`). Dither's amplitude is tiny relative to the Ceiling (at most 1 LSB, roughly -90 dBFS for 16-bit and -138 dBFS for 24-bit), and no further clamp is applied after it - unlike the oversampled-domain hard clamp (see above), there is no equivalent backstop at the base rate. In practice this means Dither can push a discrete output sample up to ~1 LSB past the nominal ceiling, which is standard, expected mastering-limiter/dither behaviour and is far below the 0.5 dB tolerance this project's own true-peak tests already use (`tests/DspFeatureTests.cpp` verifies this directly with 16-bit dither, the larger of the two amplitudes, enabled).

### Dither Shape (v0.2.0): Flat vs Shaped noise

`ParamIDs::ditherShape` (Flat/Shaped, default Flat) crosses an orthogonal axis onto the existing `dither` bit-depth choice - it only has an audible effect when `dither` is not Off, mirroring the reference class's own precedent of making noise-shaping a secondary axis on top of an existing dither-amount choice ("FabFilter Pro-L 2 allows you to choose a 'Noise Shaping' setting: 'Off' (basic TPDF), 'Basic,' 'Optimized,' or 'Weighted'" - `docs/research-notes.md` S5). This project ships a simpler two-way Flat/Shaped choice rather than the reference class's four-way one - a deliberately reduced scope, since the underlying shaping-filter coefficients below are this project's own fixed design, not a copy of any vendor's specific curve, and a four-way choice would imply more precisely-tuned distinct curves than this pass can honestly claim.

At Shaped, `TruePeakLimiterEngine::processChunk()` runs each channel's raw per-sample TPDF draw through a simple, fixed first-order differencing filter (`shapingSample = (tpdf - previousDitherTpdf[channel]) * 0.5f`, per channel) before scaling by `ditherLsb` - a from-scratch, project-owned noise-shaping design (a basic 1st-order high-pass on the injected dither sequence itself, not a copy of any vendor's shaping curve, and not a claim of matching any specific published curve). This measurably redistributes quantisation-noise energy toward the top of the audible band relative to Flat (`tests/StereoLinkDitherShapeTests.cpp` verifies this with a coarse spectral-tilt comparison: a high-frequency-band-to-low-frequency-band energy ratio, Shaped vs Flat, on a silent-input dither-only render). At Flat (the default), `shapingSample` is always the raw `tpdf` draw unconditionally - the exact same formula v1's dither used, with the exact same sequence of `juce::Random::nextFloat()` draws consumed per sample - **bit-identical to v1** at the default. Both Flat and Shaped remain subject to the same post-dither re-clamp to `ceilingLinear` described above.

## Metering (LUFS and true peak)

`TruePeakLimiterEngine` also computes and publishes (via relaxed `std::atomic<float>` members, safe for a GUI or test harness to poll from any thread - see `getGainReductionDb()`, `getOutputTruePeakDb()`, `getMomentaryLufs()`, `getShortTermLufs()`, `getIntegratedLufs()`) the following, updated once per processed (non-empty) block:

- **Gain reduction** - the minimum `currentGain` observed during the block, in dB. 0 dB idle, negative while limiting.
- **Output true peak** - the maximum `|outSample|` observed in the oversampled domain during the block (i.e. after the gain/Clip Mix/clamp stages, before downsampling), in dB. -100 dB idle floor.
- **Momentary / Short-Term / Integrated LUFS** - K-weighted loudness per ITU-R BS.1770-4, computed at the **base rate** (post-downsample, on the actual output signal): two cascaded biquads per channel (`kWeightShelf`/`kWeightHighPass`, `juce::dsp::IIR::Filter<float>`) implement the standard high-shelf + high-pass K-weighting pre-filter, re-derived per sample rate from the spec's analog-prototype parameters (`f0`/`Q`/gain for each stage) via `juce::dsp::IIR::Coefficients<float>::makeHighShelf`/`makeHighPass` rather than the spec's 48kHz-only published digital coefficients, so it is correct at every supported sample rate (44.1-192 kHz), not just 48 kHz. Momentary (400 ms) and Short-Term (3 s) are true sliding windows over the K-weighted mean power (`TruePeakLimiterEngine::LoudnessWindow`, a fixed-capacity ring buffer with an O(1) running sum, sized in `prepare()`); Integrated is a session-running accumulator, gated by the ITU absolute gate (-70 LUFS) evaluated once per processed block against that block's Momentary reading.

**Documented deviations from the full ITU-R BS.1770-4 algorithm**, both chosen to keep the implementation O(1) per sample/block and free of unbounded-duration allocation:
- Only the **absolute gate** (-70 LUFS) is implemented for Integrated Loudness; the spec's second-pass **relative gate** (-10 LU below the absolute-gated mean) is not. This means Integrated Loudness here will read slightly louder than a fully spec-compliant meter on programme material with significant quiet passages.
- The absolute gate is evaluated **once per processed block** (using that block's end-of-block Momentary reading), not continuously per the spec's overlapping 400 ms gating blocks. For typical host block sizes this is a finer granularity than the spec's blocks anyway, but it is not bit-identical to a reference implementation.
- All of this is a genuine, real-time-safe **estimate** suitable for a plugin's own reference metering, not a certified-accurate loudness-compliance meter; `tests/MeteringTests.cpp` accordingly uses broad, comparative assertions (louder signal -> higher LUFS reading; full-scale sine within a generous sane range) rather than exact reference values.

Displaying these values in the plugin's own UI (a visual meter) is GUI work, tracked for the custom-GUI milestone (M3) - this M1 work is the DSP-side computation and readout API only.

## NaN/Inf handling

`process()` sanitises non-finite (NaN/Inf) input samples to `0.0f` at the very start of every block, before they reach the oversampler. This matters specifically because the oversampler's internal IIR filter state is persistent across blocks: a single NaN sample that isn't caught would otherwise poison that filter state indefinitely, corrupting every subsequent block regardless of how "clean" the input becomes afterwards (see `tests/RobustnessTests.cpp`'s NaN/Inf sweep test).

## Real-time safety

- `ApotheosisAudioProcessor::processBlock()` starts with `juce::ScopedNoDenormals`.
- All DSP state (the oversampler, the lookahead delay buffer, the sliding-window-minimum ring buffers, the K-weighting filters, the Momentary/Short-Term loudness ring buffers) is allocated in `prepare()`/`prepareToPlay()` and never reallocated on the audio thread.
- `reset()` clears all oversampler/delay/envelope/metering state without deallocating (`TruePeakLimiterEngine::reset()`, called from both `AudioProcessor::reset()` and internally from `prepare()`). This includes zeroing Integrated LUFS's running accumulator - i.e. Integrated Loudness has transport-restart semantics, not indefinite cross-session accumulation.
- Parameter values are read via `apvts.getRawParameterValue()` atomics in `processBlock()`, never via `apvts.getParameter()->getValue()` or `String`-keyed lookups.
- Metering readouts (`getGainReductionDb()` etc.) are `std::atomic<float>`, written with `memory_order_relaxed` from the audio thread and readable from any thread (message-thread GUI polling in particular) without a lock.
- `TruePeakLimiterEngine::process()` treats a zero-sample block as a safe no-op before touching any oversampler/buffer/metering state (meters simply retain their last value).
- The sliding-window-minimum, lookahead delay buffer, and the Momentary/Short-Term loudness ring buffers are all fixed-capacity, sized once in `prepare()` from the (latched) Lookahead value and the prepared sample rate respectively; `process()` never grows them.
- Dither's `juce::Random` instance is a plain member (constructed once, not on the audio thread); `nextFloat()` itself performs no allocation.
- The v0.2.0 additions carry the same guarantees forward unmodified: Attack's per-channel event counters are plain `int`s, Auto Release's running average is a single `double`, Stereo Link/Dither Shape are per-sample scalar computations over buffers already sized in `prepare()` - the per-channel sliding-window-minimum and gain-envelope arrays this pass introduces are fixed at `TruePeakLimiterEngine::maxChannels == 2` and resized only inside `prepare()`. None of the four new setters (`setAttackMs`/`setAutoReleasePercent`/`setStereoLinkPercent`/`setDitherShape`) allocate, lock, or perform I/O. `PresetManager`'s only audio-thread-adjacent code is its `AudioProcessorValueTreeState::Listener::parameterChanged()` override (dirty-flag tracking via a single lock-free `std::atomic<bool>` store) - every other `PresetManager`/`PresetBar` method is message-thread-only (file I/O, JSON parsing, `juce::String`/`juce::var` allocation) and is never called from `processBlock()` - see `docs/preset-system-notes.md`.

## M2 preset system

`src/presets/` implements the Basilica Audio suite-wide M2 preset system (`.scaffold/specs/preset-system-m2.md`), copied verbatim from the pilot implementation (`basilica-audio/nave`) per `docs/preset-system-notes.md`'s replication recipe - `PresetManager` (factory/user preset discovery, load/save/rename/delete, default resolution, import/export including zip banks, dirty-state tracking) and `PresetBar` (the horizontal strip UI docked at the top of the editor) carry zero Apotheosis-specific code; the only plugin-specific glue is `PluginProcessor.cpp`'s `makePresetManagerConfig()`/`makeFactoryPresetAssets()` helpers and the eight `presets/factory/*.json` files themselves (`docs/presets.md`). Factory presets are embedded via `juce_add_binary_data` (`ApotheosisBinaryData`, see `CMakeLists.txt`); user presets live at `~/Library/Audio/Presets/Yves Vogl/Apotheosis/` (macOS) / `%APPDATA%/Yves Vogl/Apotheosis/Presets/` (Windows). `AudioProcessorValueTreeState` remains the single source of truth for parameter values - `PresetManager` owns no parallel copy of parameter state, only file I/O and JSON (de)serialisation. See `tests/PresetManagerTests.cpp` for the full test coverage (save/load round-trip, forward/backward-tolerant import, factory-preset validation, default resolution order, dirty-flag lifecycle, prev/next traversal, rename/delete guards, single-file and bank import/export).

## i18n frame

All user-facing **frame** strings (PresetBar's button/menu/dialog text - "Save", "Import...", "This preset file belongs to a different plugin.", etc.) are wrapped in `TRANS()`/`juce::translate()` and shipped with a German translation (`resources/i18n/de.txt`, embedded via BinaryData, selected automatically at editor construction via `juce::SystemStats::getUserLanguage()` - `de*` locales get German, everything else falls through to English; there is no user-facing language override yet). `src/presets/Localisation.{h,cpp}` (also copied verbatim from the pilot) implements the selection.

**Core/DSP terminology - parameter names, units, and technical terms (Input Gain, Ceiling, Release, Attack, Lookahead, Auto Release, Stereo Link, Clip Mix, Dither, Dither Shape, dB, dBTP, ms, %, ...) is never translated anywhere in this plugin** - `PluginEditor.cpp`'s knob/combo-box labels intentionally do not call `TRANS()` on parameter names. `tests/LocalisationTests.cpp` verifies this directly: every PresetBar frame string resolves to its exact expected German translation, and every parameter/DSP term is verifiably absent from the German mapping (i.e. `translate()` on a parameter name returns it unchanged, proving it was never mapped).

## Known limitations (as of v0.2.0)

- **Release Curve remains a fixed, user-selected shape** (Exponential/Linear/Smooth); Auto Release (v0.2.0) adds *program-dependent modulation of the effective Release time* on top of whichever curve is selected, but does not replace the curve mechanism itself with a fully adaptive multi-band/psychoacoustic model - see "v0.2.0 additions" above and `docs/design-brief.md`'s Honesty section for what Auto Release's own mechanism is (a from-scratch, reasoned design) and isn't (a copy of any vendor's proprietary IRC/ARC algorithm).
- **True-peak verification methodology**: both the engine's internal detector and this project's own test suite (`TestHelpers::measureTruePeakLinear`) use the same `juce::dsp::Oversampling` half-band polyphase IIR technique to estimate true peak. This is internally consistent and matches the DSP spec's literal instruction ("oversampled true-peak of OUTPUT"), but it is not cross-checked against a fully independent measurement algorithm (e.g. a windowed-sinc or ITU-R BS.1770-style true-peak meter). The `+ small tolerance` (0.5 dB) used in `tests/LimiterTests.cpp` and `tests/DspFeatureTests.cpp` reflects this along with the internal headroom margin's imprecision, not a hard theoretical bound.
- **Lookahead is prepare-time latched**, as described above - not a click-free, live-automatable control. This is a deliberate, documented scope decision, not an oversight.
- **LUFS metering is a real-time-safe estimate**, not a certified-accurate ITU-R BS.1770-4 loudness meter - see the "Metering" section above for the specific documented deviations (absolute-gate-only, block-granularity gating).
- **Dither is not clamp-backstopped at the base rate** - see the "Dither" section above; its amplitude is small enough that this is standard, expected behaviour, not a guarantee violation in practice.
- **Dither Shape is a fixed, two-way choice (Flat/Shaped)**, not the reference class's four-way Noise Shaping selector - a deliberately reduced scope, since the underlying shaping-filter coefficients are this project's own fixed design, not a copy of any vendor's specific curve (see the "Dither Shape" section above).
- **No multi-algorithm "Style" selector** matching the reference class's 8-way choice - out of scope for v0.2.0 (tracked as an M3+ candidate, `docs/design-brief.md`'s Honesty section), since it would require genuinely distinct limiting-character DSP per style rather than a parameterisation of the existing engine.
- **No visual metering in the GUI yet, and `PresetBar` is deliberately plain** (stock `juce::TextButton`/`PopupMenu`/`AlertWindow`/`FileChooser`, no bespoke preset-browser UI). Both the metering DSP/readout API and the M2 preset system's full functionality (this milestone) are complete and tested; visual polish for both is GUI work tracked for the custom-GUI milestone (M3), matching the M2 spec's explicit "M3 restyles it, do not gold-plate" instruction.
