<p align="center"><img src="assets/icon.png" alt="Apotheosis icon" width="120"/></p>

# Apotheosis — user manual

*The final ascension — a lookahead true-peak brickwall limiter for the master.*

## What it is

Apotheosis is a lookahead, oversampled **true-peak brickwall limiter** for the master bus. It is the last processor before export/streaming, not a general-purpose compressor: its one job is to guarantee the output's true (inter-sample, reconstructed continuous-time) peak never exceeds a Ceiling you set, while doing as little else to the sound as possible - or, if you dial in some Clip Mix, giving you a second, more aggressive "clipper" character on top of that guarantee.

## Where it sits in a symphonic-metal chain

Apotheosis belongs at the very end of the **master bus**, after every other processor has done its job:

```
Mix bus -> EQ / bus compression / saturation -> Apotheosis (true-peak limiter) -> export / streaming platform
```

It is not meant for individual tracks (guitars, drums, vocals) - use it once, on the master, as the final safety net and loudness stage before bouncing. Because it works in the true-peak (oversampled) domain rather than just the sample domain, it also protects you against inter-sample overshoot that a plain sample-peak limiter would miss - the kind of overshoot that shows up as clipping/distortion after a track is transcoded to a lossy format (MP3/AAC) for streaming, even though the original file looked "safe" on a normal peak meter.

## Signal flow

```
Input -> Input Gain -> [4x oversampled] true-peak detect -> lookahead min-gain envelope -> Release (curve-shaped)
                                                                          |
                    Output <-- Dither <-- ceiling clamp <-- Clip Mix blend <-- apply gain to lookahead-delayed signal <-+
```

Input Gain, true-peak detection, the lookahead gain-reduction envelope, the Release curve, the Clip Mix blend, and the final ceiling clamp all happen **inside the same 4x-oversampled domain**, before downsampling back to your project's sample rate. This is what makes the never-exceed-ceiling guarantee hold structurally rather than by inference: the limiter never detects a peak at high resolution and then tries to fix it after throwing that resolution away. See [`docs/architecture.md`](architecture.md) for the full engineering breakdown, including the latency model and the internal headroom-margin rationale.

## Parameter reference

| Parameter | Range | Default | Unit | What it does |
|---|---|---|---|---|
| **Input Gain** | -12 – +24 | 0 | dB | Trim applied before true-peak detection - "how hard are you hitting the ceiling". Raise it to drive the limiter harder (more gain reduction, louder/more compressed result); lower it if the incoming mix is already close to the Ceiling and you only want Apotheosis as a transparent safety net. |
| **Ceiling** | -12 – 0 | -1.0 | dBTP | The never-exceed true-peak target. The output's true (inter-sample) peak will not exceed this value, regardless of any other setting. -1.0 dBTP is a conventional mastering safety margin that leaves room for downstream lossy-encoding overshoot; use -1.0 to -2.0 dBTP for most streaming-platform targets, or push closer to 0 dBTP only if you control the final delivery format and know it won't re-encode. |
| **Release** | 5 – 1000 | 50 | ms | How quickly gain reduction relaxes back towards unity once the programme material no longer requires it. There is no separate Attack control - the attack is always instantaneous and click-free, made possible by the Lookahead delay (see below), not by a time constant. Faster Release (short values) tracks transients more closely and can sound punchier but riskier (more audible pumping on sustained material); slower Release smooths gain reduction out at the cost of holding it longer after a peak. |
| **Lookahead** | 0.1 – 20 | 5 | ms | How far into the future the limiter can "see" an oncoming true peak before it reaches the output - this is the mechanism that makes the instantaneous, non-clipping attack possible at all. **This is a "setup" parameter, not a live-automatable one**: it directly sizes real-time buffers and changes the plugin's reported latency, so a change only takes effect the next time your host re-initialises the plugin (sample-rate change, transport stop/start in most hosts, or reopening the project) - not instantly while audio is running. Larger values catch faster/steeper transients more reliably at the cost of more reported latency (which your DAW compensates for automatically on every track, so it is not something you need to manually correct for). |
| **Release Curve** | Exponential / Linear / Smooth | Exponential | – | Shapes the *release* (recovering) phase only - attack is always instantaneous regardless of this choice. **Exponential** is the classic one-pole ramp: fast initial recovery that tapers off, generally the most transparent and "musical" default. **Linear** recovers at a constant rate instead of tapering, which can sound more mechanical/obvious but is very predictable. **Smooth** is a two-stage cascade that gives a softer, overshoot-free onset to the release - useful if Exponential's initial recovery speed is causing audible pumping on sustained material - at the cost of an overall slower perceived release for the same Release time. Switching Release Curve is a discrete change (like a stompbox toggle), not a smoothly-automatable control. |
| **Clip Mix** | 0 – 100 | 0 | % | Blends the transparent gain-reduction limiter path (0%, default) with an alternate tanh soft-clip "clipper" character (100%) applied directly to the signal rather than via gain reduction. At 0% Apotheosis behaves exactly like a pure lookahead limiter. Raising Clip Mix adds an increasingly present, more aggressive/saturated top end to anything hitting the ceiling - a common modern loudness-maximiser technique that trades some transparency for extra perceived loudness and a more "glued"/aggressive character, appropriate to symphonic metal's dense, high-energy masters. Every blend still passes through the same final hard ceiling clamp, so the never-exceed-ceiling guarantee holds at any Clip Mix setting. |
| **Dither** | Off / 16-bit / 24-bit | Off | – | Adds TPDF (triangular-probability-density-function) dither noise at the very end of the chain, at the output word length - standard practice when your final export/bit-depth-reduction target is 16-bit or 24-bit, since it decorrelates quantization error into noise rather than harmonic distortion. Leave it **Off** if a downstream stage (e.g. your DAW's own dithered bounce, or a 32-bit float delivery pipeline) already handles dithering - stacking dither doesn't help and unnecessarily raises the noise floor twice. Its amplitude is tiny (at most 1 LSB at the chosen bit depth, roughly -90 dBFS for 16-bit and -138 dBFS for 24-bit) and does not meaningfully affect the true-peak ceiling guarantee. |

## Metering (engine-side; GUI display is a later milestone)

Apotheosis's DSP engine continuously computes and exposes (via the processor's `getGainReductionDb()`, `getOutputTruePeakDb()`, `getMomentaryLufs()`, `getShortTermLufs()`, and `getIntegratedLufs()` accessors) the current gain reduction, the output's true peak, and Momentary (400 ms)/Short-Term (3 s)/Integrated (session-running, absolute-gated) LUFS loudness readings. A visual meter surfacing these values in the plugin's UI is planned for the custom-GUI milestone (M3); until then, this data is available to any host or test harness that queries the processor directly. See [`docs/architecture.md`](architecture.md) for the K-weighting/gating implementation notes and its documented deviations from the full ITU-R BS.1770-4 two-pass algorithm.

## Tips

- **Use Input Gain to choose how hard you're driving the limiter, not the fader on your mix bus.** Keeping your pre-limiter mix at a sensible level and using Input Gain to dial in the amount of limiting keeps your gain-staging intentional and repeatable across sessions.
- **-1.0 dBTP is a safe general-purpose Ceiling** for most streaming targets. If you know your exact delivery pipeline (e.g. a platform with a published loudness/true-peak spec), match Ceiling to that spec instead of guessing.
- **Start with Release Curve at Exponential.** Only reach for Linear or Smooth if you're hearing something specific you want to change (mechanical-sounding recovery, or audible pumping) - they are alternatives, not upgrades.
- **Keep Clip Mix at 0% for mastering-grade transparency**, and only raise it deliberately when you want the harder, more saturated "modern loudness" character - it is an intentional trade-off, not a free loudness boost.
- **Leave Dither Off unless Apotheosis is the very last stage before a fixed-bit-depth bounce.** If your DAW's own bounce/export step already dithers, enabling it here too just adds unnecessary extra noise.
- **Don't chase 0 dBTP.** Pushing Ceiling all the way to 0 dBTP removes your safety margin against downstream lossy-encoding overshoot; -1.0 to -2.0 dBTP is the conventional range for a reason.
- **Watch your Lookahead/latency budget if you're comparing takes across plugins.** Since Lookahead only takes effect on the next `prepareToPlay()`, changing it mid-session and expecting an instant audible difference will be misleading - stop and restart playback (or reopen the plugin) after changing it.
