<p align="center"><img src="assets/icon.png" alt="Apotheosis icon" width="120"/></p>

# Apotheosis — user manual

*The final ascension — a lookahead true-peak brickwall limiter for the master.*

## What it is

Apotheosis is a lookahead, oversampled **true-peak brickwall limiter** for the master bus. It is the last processor before export/streaming, not a general-purpose compressor: its one job is to guarantee the output's true (inter-sample, reconstructed continuous-time) peak never exceeds a Ceiling you set, while doing as little else to the sound as possible - or, if you dial in some Clip Mix, giving you a second, more aggressive "clipper" character on top of that guarantee.

## Where it sits in a heavy production chain

Apotheosis belongs at the very end of the **master bus**, after every other processor has done its job:

```
Mix bus -> EQ / bus compression / saturation -> Apotheosis (true-peak limiter) -> export / streaming platform
```

It is not meant for individual tracks (guitars, drums, vocals) - use it once, on the master, as the final safety net and loudness stage before bouncing. Because it works in the true-peak (oversampled) domain rather than just the sample domain, it also protects you against inter-sample overshoot that a plain sample-peak limiter would miss - the kind of overshoot that shows up as clipping/distortion after a track is transcoded to a lossy format (MP3/AAC) for streaming, even though the original file looked "safe" on a normal peak meter.

## Signal flow

```
Input -> Input Gain -> [4x/8x/16x oversampled] true-peak detect (Stereo Link-weighted) -> lookahead min-gain envelope
                                          -> Style: FIR attack smoother -> dual-stage Release (Auto Release-modulated)
                                             (Classic: Attack classifier -> single Release, as in v0.2.0)
                                                                          |
                       ceiling clamp <-- Clip Mix blend <-- apply gain to lookahead-delayed signal <-+
                                |
     Output <-- Delta / Unity Gain <-- Dither (Legacy/Weighted) <-- True Peak Guard <-- [downsample]
```

Input Gain, true-peak detection, the lookahead gain-reduction envelope, the Style attack smoother and release stages, the Clip Mix blend, and the final ceiling clamp all happen **inside the same oversampled domain** (4x by default; 8x or 16x if you choose), before downsampling back to your project's sample rate. The True Peak Guard, dither and the two audition modes run *after* downsampling, at your project's sample rate, because that is the signal a delivery meter actually sees. This is what makes the never-exceed-ceiling guarantee hold structurally rather than by inference: the limiter never detects a peak at high resolution and then tries to fix it after throwing that resolution away. See [`docs/architecture.md`](architecture.md) for the full engineering breakdown, including the latency model and the internal headroom-margin rationale.

**v0.4.0** adds seven more - Style, Oversampling, OS Filter, True Peak Guard, Noise Shaping, Delta and Unity Gain. The headline is **Style**: the four non-Classic styles replace the rectangular gain envelope with an FIR-smoothed one, which makes zero overshoot a structural property of the algorithm rather than something a clamp catches afterwards, and smooths the envelope's sharp corners on transient-dense material. (On sustained low-frequency content at generous Lookahead settings, Classic was already clean - see the measurement note in `docs/architecture.md`.) **True Peak Guard** turns the Ceiling into a measured guarantee instead of a margin, and the loudness metering became compliant enough to replace a standalone meter. As with v0.2.0, **every one of the seven defaults reproduces v0.2.0's output bit-for-bit** - loading an existing session changes nothing about how it sounds. The one exception is a pure, constant 6-sample delay below 176.4 kHz (see the latency table), which shifts the output in time without altering a single sample value.

**v0.2.0** adds four new controls - Attack, Auto Release, Stereo Link, Dither Shape - described in the parameter table below. All four are research-derived: sourced from a competitor's own published help documentation (FabFilter Pro-L 2), a competitor's own published manual page (iZotope Ozone Maximizer), general audio-DSP dithering literature, and the ITU-R BS.1770 true-peak specification's documented behaviour, cited here for the underlying *design principles and terminology* only (the two-stage attack/release architecture, program-dependent release, noise-shaped dither) - **not implying endorsement, sponsorship, affiliation, or DSP equivalence, and not measured against, benchmarked against, or reverse-engineered from any competitor's actual binary/DSP**. See [`docs/design-brief.md`](design-brief.md) and [`docs/research-notes.md`](research-notes.md) for the full sourcing. Every new control's default reproduces v1's exact prior behaviour bit-for-bit - none of them change what Apotheosis sounds like unless you deliberately move them.

## Parameter reference

| Parameter | Range | Default | Unit | What it does |
|---|---|---|---|---|
| **Input Gain** | -12 – +24 | 0 | dB | Trim applied before true-peak detection - "how hard are you hitting the ceiling". Raise it to drive the limiter harder (more gain reduction, louder/more compressed result); lower it if the incoming mix is already close to the Ceiling and you only want Apotheosis as a transparent safety net. |
| **Ceiling** | -12 – 0 | -1.0 | dBTP | The never-exceed true-peak target. The output's true (inter-sample) peak will not exceed this value, regardless of any other setting. -1.0 dBTP is a conventional mastering safety margin that leaves room for downstream lossy-encoding overshoot; use -1.0 to -2.0 dBTP for most streaming-platform targets, or push closer to 0 dBTP only if you control the final delivery format and know it won't re-encode. |
| **Release** | 5 – 1000 | 50 | ms | How quickly gain reduction relaxes back towards unity once the programme material no longer requires it. There is no separate Attack control - the attack is always instantaneous and click-free, made possible by the Lookahead delay (see below), not by a time constant. Faster Release (short values) tracks transients more closely and can sound punchier but riskier (more audible pumping on sustained material); slower Release smooths gain reduction out at the cost of holding it longer after a peak. |
| **Lookahead** | 0.1 – 20 | 5 | ms | How far into the future the limiter can "see" an oncoming true peak before it reaches the output - this is the mechanism that makes the instantaneous, non-clipping attack possible at all. **This is a "setup" parameter, not a live-automatable one**: it directly sizes real-time buffers and changes the plugin's reported latency, so a change only takes effect the next time your host re-initialises the plugin (sample-rate change, transport stop/start in most hosts, or reopening the project) - not instantly while audio is running. Larger values catch faster/steeper transients more reliably at the cost of more reported latency (which your DAW compensates for automatically on every track, so it is not something you need to manually correct for). |
| **Release Curve** | Exponential / Linear / Smooth | Exponential | – | Shapes the *release* (recovering) phase only - attack is always instantaneous regardless of this choice. **Exponential** is the classic one-pole ramp: fast initial recovery that tapers off, generally the most transparent and "musical" default. **Linear** recovers at a constant rate instead of tapering, which can sound more mechanical/obvious but is very predictable. **Smooth** is a two-stage cascade that gives a softer, overshoot-free onset to the release - useful if Exponential's initial recovery speed is causing audible pumping on sustained material - at the cost of an overall slower perceived release for the same Release time. Switching Release Curve is a discrete change (like a stompbox toggle), not a smoothly-automatable control. |
| **Clip Mix** | 0 – 100 | 0 | % | Blends the transparent gain-reduction limiter path (0%, default) with an alternate tanh soft-clip "clipper" character (100%) applied directly to the signal rather than via gain reduction. At 0% Apotheosis behaves exactly like a pure lookahead limiter. Raising Clip Mix adds an increasingly present, more aggressive/saturated top end to anything hitting the ceiling - a common modern loudness-maximiser technique that trades some transparency for extra perceived loudness and a more "glued"/aggressive character, appropriate to heavy music's dense, high-energy masters. Every blend still passes through the same final hard ceiling clamp, so the never-exceed-ceiling guarantee holds at any Clip Mix setting. |
| **Dither** | Off / 16-bit / 24-bit | Off | – | Adds TPDF (triangular-probability-density-function) dither noise at the very end of the chain, at the output word length - standard practice when your final export/bit-depth-reduction target is 16-bit or 24-bit, since it decorrelates quantization error into noise rather than harmonic distortion. Leave it **Off** if a downstream stage (e.g. your DAW's own dithered bounce, or a 32-bit float delivery pipeline) already handles dithering - stacking dither doesn't help and unnecessarily raises the noise floor twice. Its amplitude is tiny (at most 1 LSB at the chosen bit depth, roughly -90 dBFS for 16-bit and -138 dBFS for 24-bit) and does not meaningfully affect the true-peak ceiling guarantee. |
| **Attack** *(v0.2.0)* | 0 – 50 | 0 | ms | A transient/sustain **classifier**, not a gain-reduction ramp - the attack itself is always instantaneous regardless of this setting (see Release, above). Attack sets the duration below which a gain-reduction event is treated as a short transient, whose recovery then happens near-instantly (independent of Release/Release Curve/Auto Release) instead of following the normal Release-governed path. At 0 ms (the default) every event is treated as sustained and Apotheosis behaves exactly as v1 did. Raise Attack to let brief transient peaks recover their gain faster than sustained, dense material - the reference class's documented "short lookahead + long attack + fast release" recipe for maximising perceived loudness while preserving punch (see the Punchy Master factory preset). Research-derived from a competitor's documented two-stage transient/sustain architecture - the 0-50 ms range and 0 ms default are this project's own reasoned choices (no competitor default numeric value was recoverable from the sources fetched), not copied from any spec sheet. |
| **Auto Release** *(v0.2.0)* | 0 – 100 | 0 | % | Blends in a program-dependent modulation of the *effective* Release time: a slow (multi-second), running average of how deep/sustained recent gain reduction has been biases the effective Release time longer on dense, sustained material (avoiding pumping) and shorter on sparse, isolated peaks (recovering quickly between them) - layered on top of (not replacing) whichever Release Curve you have selected. At 0% (the default) this is a complete no-op and Release behaves exactly as v1's. This is a from-scratch, reasoned implementation of the *qualitative principle* documented in the reference class's program-dependent-release lineage - **it is explicitly not a reverse-engineered copy of any vendor's specific proprietary algorithm**, whose internals are not publicly documented. Shipped off by default because no sourced default intensity exists for this control in any reference product; try it on dense, high-energy masters (see the Dense/Loud Modern and Adaptive Riding factory presets) if you want Apotheosis to ride sustained passages more gently without also slowing down its response to isolated peaks. |
| **Stereo Link** *(v0.2.0)* | 0 – 100 | 100 | % | How tightly the two channels' true-peak detection is linked. At 100% (the default) both channels share a single, max-linked detector - v1's only behaviour, and the safest choice for most mastering work, since it never lets the stereo image shift under gain reduction. Lowering it lets each channel detect and limit progressively more independently, down to fully independent detection at 0% - useful when a hard-panned peak in one channel would otherwise pull the *opposite* channel's gain down with it (see the Wide Image Preserve factory preset). Mono and dual-mono host layouts are unaffected (there is no second channel to link against). |
| **Dither Shape** *(v0.2.0)* | Flat / Shaped | Flat | – | Only has an audible effect when **Dither** (above) is not Off. **Flat** (the default) is v1's existing plain TPDF dither, unchanged. **Shaped** runs the dither noise through a simple, fixed noise-shaping filter that pushes quantisation-noise energy toward the top of the audible band, where hearing is less sensitive - a general dithering-literature technique, not a copy of any vendor's specific shaping curve (the reference class documents a four-way Noise Shaping choice; this is a deliberately simpler two-way one). Dither's own guidance applies here too: this is genuinely one of the least consequential decisions in mastering, bounded to the least-significant bits either way - reach for **Clean Export (Dithered)** as a starting point when Shaped is worth trying (a final, fixed-bit-depth bounce). **Superseded when Noise Shaping is set to Weighted** - see that control below. |
| **Style** *(v0.4.0)* | Classic / Transparent / Punchy / Bus / Safe | Classic | – | The single most consequential v0.4.0 control: it selects how the gain envelope is *shaped*. **Classic** (the default) is the literal v0.2.0 behaviour, unchanged - a rectangular sliding-minimum attack plus the Attack transient classifier. The four new styles replace both: the attack becomes a **cascaded-box FIR smoother**, which removes the sharp corners in the gain envelope that cause low-frequency intermodulation distortion on bass-heavy material, and the transient classifier becomes **two concurrent release followers** (a fast one, depth-capped to the top few dB of gain reduction, handling transient tops; a slow one carrying the sustained programme gain reduction). The FIR smoother is why the styles are transparent at depth: because a mean of values that are each at least the window minimum is itself at least that minimum, a smoothed envelope **cannot overshoot** - the guarantee is structural, not a clamp. See the per-style table below. |
| **Oversampling** *(v0.4.0)* | 4x / 8x / 16x | 4x | – | How far the detection-and-limiting domain is oversampled. **4x** (the default) is the exact v0.2.0 chain. **8x** and **16x** push aliasing from the clipper path and from the gain envelope's own sharp edges further down (measurably: see the alias floors under "Oversampling: what the factors buy you" below), at a real CPU and latency cost. **This is a "setup" parameter exactly like Lookahead** - see the prepare-latch note under the latency table. At high project sample rates the engine internally caps the effective factor (8x maximum at 96 kHz and above, 4x maximum at 176.4 kHz and above); the control keeps whatever you set it to, so the setting travels correctly back to a 48 kHz session. |
| **OS Filter** *(v0.4.0)* | Minimum Phase / Linear Phase | Minimum Phase | – | The filter class used by the oversampler. **Minimum Phase** (the default) is v0.2.0's polyphase allpass IIR halfband: the lowest latency, no pre-ringing, but non-linear phase. **Linear Phase** uses FIR equiripple halfbands: perfectly preserved phase relationships and better null-test behaviour in a mastering stack, at the cost of roughly 50-70 samples of extra latency and some pre-ringing on hard transients. Same prepare-latch contract as Oversampling. |
| **True Peak Guard** *(v0.4.0)* | Off / On | Off | – | Turns the Ceiling from a *margin-based* promise into a **measured** one. With the guard on, a BS.1770-4-compliant 4x interpolating true-peak detector runs on the final output, after downsampling - the same measurement a compliant delivery meter performs - and whenever it reads above your Ceiling, the output is ducked by exactly the excess, for exactly the duration of the excess (attack under 0.1 ms, release 5 ms). It is a micro-correction, not a second limiter: on material that never exceeds the Ceiling it does nothing at all. Turn it on for anything you are delivering to a platform with a published true-peak spec. Its alignment delay is **always** in the signal path whether the guard is on or off, so switching or automating it can never change the plugin's reported latency mid-session. |
| **Noise Shaping** *(v0.4.0)* | Legacy / Weighted | Legacy | – | Only has an audible effect when **Dither** is not Off. **Legacy** (the default) routes through the untouched v0.2.0 dither code, including the Dither Shape choice - bit-identical to v0.2.0. **Weighted** replaces it with a psychoacoustically shaped requantiser: per-channel TPDF plus a 9th-order error-feedback noise shaper fitted to an inverse audibility curve, which pushes quantisation noise into the frequency regions where hearing is least sensitive. Measured against flat TPDF, this buys at least 15 dB of F-weighted perceived noise-floor improvement at 16-bit (the asserted floor; the shipped coefficient table measures 20.05 dB at 48 kHz). **Precedence: when Noise Shaping is Weighted, it supersedes Dither Shape entirely** - the Flat/Shaped choice no longer does anything. The curve is optimised for 44.1/48 kHz delivery; it is still used unchanged at higher rates. |
| **Delta** *(v0.4.0)* | Off / On | Off | – | A monitor mode: the output becomes *what the limiter removed* (the processed signal minus the sample-aligned dry signal) instead of the processed signal. It is the fastest way to hear whether your limiting is transparent - if Delta is close to silent, almost nothing is being taken away; if you can identify instruments in it, you are limiting harder than you think. Toggling is crossfaded over 10 ms, so it is safe to automate. Delta bypasses dither (you are auditioning, not rendering) but still passes the final safety clamp. **Not a render mode - turn it off before you bounce.** |
| **Unity Gain** *(v0.4.0)* | Off / On | Off | – | A monitor mode: trims the output by exactly minus your Input Gain, so you can A/B different drive amounts at **matched loudness** rather than being fooled by the fact that louder always sounds better. Move Input Gain with Unity Gain on and you hear the *character* of more limiting without the level difference. When Delta and Unity Gain are both on, **Delta wins** (it is already a monitor mode). |

### Style: what each one is for

All four non-Classic styles use the FIR attack smoother and the dual release followers; they differ in how wide the smoother is and how the two followers are tuned. The **Attack** control gains a second meaning in these styles: it sets the smoother's span (`Attack ms`, capped at the Lookahead), and **Attack at 0 ms means "use the full Lookahead"** - which is the recommended setting for all of them.

| Style | Attack smoother | Fast / slow release | Fast-stage depth cap | Use it for |
|---|---|---|---|---|
| **Classic** | none - v0.2.0's rectangular sliding minimum | v0.2.0's single classifier-driven release | – | Existing sessions, and anyone who wants v0.2.0's exact sound. This is the default and it is bit-identical to v0.2.0. |
| **Transparent** | 2 boxes, full lookahead | 50 ms / 800 ms | 2 dB | **The recommended mastering default.** The best transparency-per-dB of the set; this is the style the low-frequency intermodulation work was aimed at. |
| **Punchy** | 1 box, 0.4x lookahead | 30 ms / 400 ms | 4 dB | Loudness-first work. The narrower smoother and higher fast cap deliberately let transient tops through harder, at some cost in transparency. |
| **Bus** | 2 boxes, full lookahead | 80 ms / 1200 ms | 2 dB | Slow, glue-like behaviour on a mix bus rather than a delivery master. |
| **Safe** | 3 boxes, N/3 each, full lookahead | 60 ms / 1500 ms | 1 dB | Maximum smoothness: classical, acoustic and archival material, where any audible gain movement is worse than a little less loudness. |

### Oversampling: what the factors buy you

Raising the factor does one measurable thing: it pushes the alias products
generated inside the oversampled domain - by the Clip Mix tanh path, and by
the gain envelope's own sharp edges - further below the signal. The figures
below are the bounds `tests/TruePeakVerificationTests.cpp` asserts on every
CI push, measured on a 10 kHz sine at 100 % Clip Mix (the worst case this
plugin can produce), expressed relative to the fundamental.

| Configuration | Alias floor (asserted) | Pass-through flatness to 20 kHz |
|---|---|---|
| **4x Minimum Phase** (default) | -70 dBc | flat within +-0.1 dB |
| **8x Minimum Phase** | -90 dBc | flat within +-0.1 dB |
| **16x Linear Phase** | -100 dBc | flat within +-0.1 dB |

The flatness bound holds for **all six** factor/phase combinations, not just
the three tiers above; the measured worst-case deviation across all six is
0.0004 dB, three orders of magnitude inside the bound. In other words, none
of these settings changes your tone - they change how much alias energy
survives when the limiter is working hard.

Two practical consequences:

- **At 0 % Clip Mix on material that barely touches the ceiling, the factor
  is close to inaudible.** The alias energy the higher factors remove is
  generated by hard nonlinearity; if you are not generating much, there is
  not much to remove. 4x Minimum Phase is a genuinely good default.
- **The cost is CPU and (for Linear Phase) latency**, not tone. Reach for
  16x Linear Phase for archival and mastering-for-transcode work where the
  clipper is engaged, rather than routinely.

Note also that **4x Minimum Phase deliberately uses different filter
coefficients from the other five combinations**: it keeps the exact stock
filters v0.2.0 shipped, because that is what makes the default bit-identical
to existing sessions. The custom, steeper filter specs (which spend their
attenuation budget on the *decimator*, where audible aliases fold back)
apply to 8x, 16x and every Linear Phase configuration - settings no
pre-v0.4.0 session can already be using. See
[`docs/architecture.md`](architecture.md) for the stage-spec detail.

### Latency

Apotheosis reports its latency to your host, which compensates for it automatically on every track - you never need to correct for it manually. The table is the honest total at the **default 5 ms Lookahead**: lookahead + the oversampler's round trip + the True Peak Guard's constant 6-sample alignment delay. Changing Lookahead moves every number by the same amount.

| Project rate | 4x Min | 8x Min | 16x Min | 4x Linear | 8x Linear | 16x Linear |
|---|---|---|---|---|---|---|
| 44.1 kHz | 232 | 232 | 232 | 283 | 286 | 286 |
| 48 kHz | 252 | 252 | 252 | 303 | 306 | 306 |
| 96 kHz | 492 | 492 | 492 | 543 | 546 | 546 |
| 192 kHz | 966 | 966 | 966 | 1017 | 1017 | 1017 |

All values in samples at the project rate. Three things worth knowing:

- **Minimum Phase costs nothing extra as the factor rises.** The IIR allpass halfband chain's latency is effectively zero at every factor, so the whole minimum-phase column is just lookahead + 6. Linear Phase is where the factor shows up, and even there the step from 4x to 8x is 3 samples.
- **The 6-sample True Peak Guard delay is always present** below 176.4 kHz, whether the guard is on or off, and it is 0 at 176.4 kHz and above. That is deliberate: a delay that appeared and disappeared with the toggle would glitch your host's delay compensation every time the parameter was automated. It is a pure delay, so it changes nothing about the audio content.
- **At 192 kHz the 8x and 16x rows equal the 4x row** because the engine derates the effective factor at high project rates (see Oversampling above).

Two more things worth knowing if you null-test or measure latency yourself:

- **Comparing a v0.4.0 render against a v0.2.0 one? Shift by exactly
  6 samples** (below 176.4 kHz; 0 at and above). The guard's alignment delay
  is a pure integer delay - it changes *when* the output arrives, never a
  single sample *value* - so with that shift applied, a v0.4.0 render at the
  default settings nulls against a v0.2.0 render completely. This is
  verified rather than claimed: the project's regression tests compare
  against committed v0.2.0 renders with exactly this compensation and assert
  bit-exactness.
- **On 8x and 16x Minimum Phase, an impulse's energy maximum lands one
  sample after the reported latency.** JUCE derives an IIR chain's latency
  from its phase delay near DC - which is precisely the convention hosts
  compensate against - while the custom allpass cascade's phase dispersion
  pushes the impulse peak fractionally later. **The reported number is the
  correct one for your host's delay compensation**, and the other four
  factor/phase combinations land exactly on it. This only matters if you are
  measuring alignment by locating an impulse peak yourself; it is measured
  and bounded in `tests/TruePeakVerificationTests.cpp`, not an unknown.

**Prepare-latch: Oversampling and OS Filter behave exactly like Lookahead.** All three are read only when the host initialises the plugin, so a change takes effect at the next `prepareToPlay()` - reopening the project, changing the sample rate or buffer size, or in most hosts stopping and restarting the transport - not instantly while audio is running. Set them up front rather than reaching for them mid-mix, and don't automate them. (Live, click-free switching is planned, and needs machinery that is not safe to bolt on: re-allocating the oversampler while audio runs is a real-time hazard, so it is being done properly rather than quickly.)

## Metering

Apotheosis's DSP engine continuously computes and exposes the current gain reduction, the output's true peak, and Momentary (400 ms)/Short-Term (3 s)/Integrated LUFS loudness readings. A visual meter surfacing these values in the plugin's UI is planned for the custom-GUI milestone (M3); until then, this data is available to any host or test harness that queries the processor directly.

**v0.4.0 makes the metering delivery-grade**, which is a change in what the numbers say (it does not change the audio in any way):

- **Integrated LUFS is now fully ITU-R BS.1770-4 gated.** 400 ms blocks at 75% overlap, the -70 LUFS absolute gate and the -10 LU relative gate, computed the specified two-pass way. v0.2.0 shipped a documented approximation of this; the reading you get now is the reading a compliant standalone loudness meter gives, verified against the EBU Tech 3341 test cases to within 0.1 LU. **Expect Integrated readings to differ from v0.2.0's on the same material** - the new ones are the correct ones.
- **Loudness Range (LRA)** is new, per EBU Tech 3342, verified against the Tech 3342 vectors to within 1 LU (`getLoudnessRangeLu()`).
- **The output true-peak readout is now a measurement, not an estimate.** It is produced by the same BS.1770-4 48-tap 4-phase interpolating detector the True Peak Guard uses, applied at the project sample rate after downsampling - so the dBTP figure Apotheosis shows is what a compliant external meter will show, rather than an oversampled-domain approximation of it.
- **True-peak max-hold** (`getTruePeakMaxHoldDb()`, resettable) and **maximum gain reduction** (`getMaxGainReductionDb()`) are new session-running readouts.

See [`docs/architecture.md`](architecture.md) for the K-weighting, histogram-gating and interpolator implementation notes.

## What is verified, and to what bound

Every headline claim in this manual has a test behind it that runs on every
CI push (macOS and Windows). This table is the short version - the bound is
what the committed test asserts, the measured column is what it actually
produced on the release measurement run.

| Claim | Asserted bound | Measured |
|---|---|---|
| The non-Classic styles' gain envelope never overshoots | Zero violations across 10 080 randomised signals (4 styles x 3 lookahead settings), **with the final safety clamp bypassed** | 0 violations |
| With True Peak Guard on, a compliant BS.1770-4 4x meter reads at or below your Ceiling | <= Ceiling + 0.05 dB on every corpus item, including a near-Nyquist inter-sample-peak case | At a -1.0 dBTP Ceiling: -1.128 / -1.287 / -1.293 / -1.733 dBTP |
| The True Peak Guard's alignment delay is a constant | Exactly 6 samples at 44.1 / 48 / 96 kHz, 0 at 192 kHz - never scaled by sample rate | 6 / 6 / 6 / 0 |
| Weighted noise shaping beats flat TPDF | >= 15 dB F-weighted improvement at 16-bit | 20.05 dB |
| Alias products stay below the per-factor floor | -70 dBc at 4x, -90 dBc at 8x, -100 dBc at 16x Linear | within bounds |
| Oversampling is tonally transparent | Pass-through flat within +-0.1 dB to 20 kHz, all six factor/phase combinations | 0.0004 dB worst case |
| Auto Release does not depend on your buffer size (non-Classic styles) | A 64-sample render nulls against a 2048-sample render to at least 80 dB | Zero difference - identical renders |
| Delta really is what the limiter removed | A signal 6 dB under the Ceiling gives a delta below -100 dBFS RMS | within bound |
| Unity Gain holds level | Output within 0.05 dB of the 0 dB-drive reference | within bound |
| Integrated LUFS is spec-correct | EBU Tech 3341 test set within 0.1 LU | within bound |
| Loudness Range is spec-correct | EBU Tech 3342 vectors within 1 LU | within bound |
| Every v0.4.0 default reproduces v0.2.0 | Bit-exact against committed v0.2.0 golden renders (after the 6-sample guard shift) | 0 mismatches |
| Nothing allocates on the audio thread | 0 allocations under a replaced global allocator, with 16x Linear Phase, True Peak Guard, Weighted dither, the Safe style, Delta and Unity Gain all engaged | 0 |

Two honest caveats belong next to that table:

- **The measured ceiling guarantee is a 4x-meter guarantee.** A compliant
  BS.1770-4 4x meter can under-read the true peak by up to 0.554 dB on
  content near 0.45 x Nyquist, and the guard corrects until *its own*
  compliant detector reads at or below your Ceiling. A higher-resolution
  reference meter can therefore legitimately read up to that much above the
  Ceiling on such content. That is a property of the measurement standard,
  not of this plugin, and the tests state it explicitly instead of hiding
  it.
- **The loudness metering is verified against the published EBU test
  vectors, not certified by a compliance body.** Verified is a strong claim;
  certified is a different one, and this plugin only makes the first.

There is also a measured result that came out *smaller* than intended, and
it is documented rather than quietly dropped: the FIR attack smoother was
aimed partly at low-frequency intermodulation distortion, and on sustained
50 Hz driven 3 dB over the Ceiling at a 10 ms Lookahead, **Classic measures
just as clean as Transparent** (both far under the 1 % bound). The reason is
that this engine's sliding-minimum window has always been sized by
*Lookahead* rather than by an attack time, so at 10 ms it already spans the
50 Hz half-period. Shorten the Lookahead to 3 ms - below that half-period -
and the same render distorts measurably, which is what shows the window is
doing the work. The smoother's real contribution is the structural
zero-overshoot guarantee and a smoother envelope on transient-dense
material, not a low-frequency distortion win over Classic at generous
Lookahead settings. See `docs/architecture.md` for the full note.

Three photoreal needle meters sit across the top of the editor, left to right: **Gain Reduction**, **True Peak**, and **LUFS** (Momentary, 400 ms integration). All three poll the DSP engine's existing metering accessors (`getGainReductionDb()`, `getOutputTruePeakDb()`, `getMomentaryLufs()`) on a 30 Hz timer and apply ~300 ms of ballistic smoothing, reading like a physical VU meter rather than jumping instantaneously. The Gain Reduction needle sits at rest when there is no reduction and sweeps away from rest as reduction increases; True Peak and LUFS both read directly against the same dB scale (a well-behaved master sits at/below Ceiling on the True Peak meter, and typical streaming/loud masters land comfortably on the LUFS meter's scale - very quiet material pins toward the scale's floor). Short-Term (3 s) and Integrated (session-running, absolute-gated) LUFS readings remain available to any host or test harness via `getShortTermLufs()`/`getIntegratedLufs()`, just not shown on their own dedicated needle. See [`docs/architecture.md`](architecture.md) for the K-weighting/gating implementation notes and its documented deviations from the full ITU-R BS.1770-4 two-pass algorithm, and [`docs/gui-components.md`](gui-components.md) for the exact per-meter dB-mapping rationale.

## Window scale

A button next to the preset bar cycles the editor between 100%, 150%, and 200% window scale (no free/continuous resize - the artwork is pre-rendered at fixed density tiers). The chosen scale is remembered per plugin instance and persists through session save/reload.

## The Lookahead knob's dashed frame

Lookahead is the one control on the Release panel enclosed in a dashed amber frame and labelled "Lookahead (Setup)". This is a **setup** parameter: it sizes the limiter's internal buffers and determines the plugin's reported latency, so changing it only takes effect the next time the audio engine restarts (e.g. reopening the plugin, or a host sample-rate/block-size change) rather than live while audio is playing - unlike every other knob on the faceplate, which responds immediately. It remains a normal, host-automatable parameter; the frame is purely a visual/accessibility cue about *when* a change takes effect.

## Presets

A preset bar sits at the top of the editor (`[<] [PresetName] [>] [Save] [Save As...] [Delete] [Import...] [Export...]`). Apotheosis ships with eleven factory presets covering the v1-compatible default plus starting points for each new v0.2.0 and v0.4.0 control - see [`docs/presets.md`](presets.md) for the full list and intent of each. Your own presets save to `~/Library/Audio/Presets/Basilica Audio/Apotheosis/` on macOS (`%APPDATA%/Basilica Audio/Apotheosis/Presets/` on Windows); the preset menu's "Set current as default" makes any preset (factory or your own) the one that loads automatically the next time you open a fresh instance. Import/export works with single `.basilicapreset` files and `.zip` preset banks.

## Language

The preset bar's own interface text (button labels, menu items, dialog messages) automatically appears in German if your system language is German; otherwise it stays in English. There is no manual language switch yet. Parameter names and units (Attack, Release, dB, %, ...) always stay in English regardless of system language, since they are technical terms, not translatable UI copy.

## Tips

- **Use Input Gain to choose how hard you're driving the limiter, not the fader on your mix bus.** Keeping your pre-limiter mix at a sensible level and using Input Gain to dial in the amount of limiting keeps your gain-staging intentional and repeatable across sessions.
- **-1.0 dBTP is a safe general-purpose Ceiling** for most streaming targets. If you know your exact delivery pipeline (e.g. a platform with a published loudness/true-peak spec), match Ceiling to that spec instead of guessing.
- **Start with Release Curve at Exponential.** Only reach for Linear or Smooth if you're hearing something specific you want to change (mechanical-sounding recovery, or audible pumping) - they are alternatives, not upgrades.
- **Keep Clip Mix at 0% for mastering-grade transparency**, and only raise it deliberately when you want the harder, more saturated "modern loudness" character - it is an intentional trade-off, not a free loudness boost.
- **Leave Dither Off unless Apotheosis is the very last stage before a fixed-bit-depth bounce.** If your DAW's own bounce/export step already dithers, enabling it here too just adds unnecessary extra noise.
- **Don't chase 0 dBTP.** Pushing Ceiling all the way to 0 dBTP removes your safety margin against downstream lossy-encoding overshoot; -1.0 to -2.0 dBTP is the conventional range for a reason.
- **Watch your Lookahead/latency budget if you're comparing takes across plugins.** Since Lookahead only takes effect on the next `prepareToPlay()`, changing it mid-session and expecting an instant audible difference will be misleading - stop and restart playback (or reopen the plugin) after changing it.
- **Leave Attack, Auto Release, and Stereo Link at their defaults (0 ms, 0%, 100%) until you have a specific reason to change one.** Each is a genuine character control, not a "better" setting - Apotheosis at its v0.2.0 defaults sounds and behaves exactly like v1.
- **Try the "short lookahead + long attack + fast release" combination for extra perceived loudness with preserved punch** (see the Punchy Master factory preset) - this is the reference class's own documented trade-off for maximising loudness while keeping transients intact, now reachable in Apotheosis via Attack.
- **If you are starting a new master, start on Style: Transparent rather than Classic.** Classic is the default only because it has to be - it is what protects existing sessions. Transparent is the better limiter.
- **Use Delta to decide how hard to limit.** Push Input Gain until you can start to identify instruments in the Delta signal, then back off. It is far more reliable than deciding by ear at two different loudnesses.
- **Use Unity Gain whenever you are comparing two Input Gain settings.** Louder always sounds better; matched loudness is the only way to hear whether it actually is better.
- **Turn True Peak Guard on for anything with a delivery spec**, and leave the Ceiling where the spec says. The guard is what makes the number true rather than probable.
- **Set Oversampling and OS Filter at the start of a session, not during it.** They only take effect when the host re-initialises the plugin, so changing one mid-mix and listening for a difference will mislead you. 4x Minimum Phase is a perfectly good default; reach for 16x Linear Phase for archival work, not routinely.
- **Reach for Stereo Link below 100% only when a hard-panned element is visibly pulling the opposite channel's gain reduction down with it** (check the Wide Image Preserve factory preset as a starting point) - full linking is the safer, more conventional choice for most mastering work.

## Known limitations

- **The seven v0.4.0 controls have no bespoke editor controls yet.** Style, Oversampling, OS Filter, True Peak Guard, Noise Shaping, Delta and Unity Gain are all host-visible and automatable through the plugin's parameter layout, so every host surfaces them in its generic parameter view - but wiring them into a dedicated editor, and the visual meter for the gain-reduction/true-peak/loudness readouts described above, are both later-milestone (M3) GUI work. The underlying DSP and readout API are already complete.
- **Dither is not re-clamped to the Ceiling at the base rate.** A discrete output sample can sit up to about 1 LSB past the nominal Ceiling (roughly -90 dBFS at 16-bit, -138 dBFS at 24-bit) - standard, expected behaviour for dithered output, and far inside the tolerance the true-peak tests use.
- **The Weighted noise-shaping curve is fitted for 44.1/48 kHz delivery** and is used unchanged at higher project rates, where its fit to the audibility curve no longer holds as precisely.
- **Five styles, one architecture, honestly described.** Classic keeps v0.2.0's exact behaviour; the four non-Classic styles share one FIR-smoother/dual-release architecture with different per-style constants rather than being four independently engineered algorithms.
- **Release Curve stays a single, user-selected shape in Classic.** The non-Classic styles' fast/slow release time constants are fixed per style (see the style table above), not separately user-exposed.
- **The measured ceiling guarantee is a 4x-meter guarantee** (see the caveats under "What is verified, and to what bound" above) - a higher-resolution reference meter can legitimately read a little above the Ceiling on certain content near 0.45x Nyquist. That is a property of the BS.1770-4 standard itself, not of this plugin.
- **Lookahead, Oversampling and OS Filter are prepare-latched "setup" parameters**, not live-automatable (see Latency above). Live, click-free switching needs real-time-safe re-prepare machinery that is not yet built - re-allocating the oversampler while audio runs is a real-time hazard.
- **The voicing throughout is research-derived**, sourced from published documentation and general DSP/dithering literature for this reference class - not measured against, benchmarked against, or reverse-engineered from any competitor's binary. See [`docs/research-notes.md`](research-notes.md) for the sourced findings.
