# Apotheosis — brickwall true-peak limiter (mastering)

Per-repo working memory for Claude Code sessions on this plugin. Part of the **Metal up your ass** symphonic-metal plugin suite (`github.com/basilica-audio`).

## What this is
Apotheosis is the "brickwall true-peak limiter (mastering)" member of the suite. AU / VST3 / Standalone, JUCE 8.

## Status (v0.1.0 — M1 DSP completion & test coverage done)
Core DSP complete for v0.1.0, **44/44 Catch2 tests green** locally. CI (macOS + Windows, pluginval strictness 10 + auval) green. GUI is a functional v0.1 slider/combo-box editor covering every parameter (custom LookAndFeel + visual metering is roadmap M3). No signing yet (roadmap M4). See GitHub **milestones/issues** for the open work.

## DSP
`TruePeakLimiterEngine` (`src/dsp/TruePeakLimiterEngine.{h,cpp}`) does input gain, then upsamples 4x (`juce::dsp::Oversampling`, half-band polyphase IIR, `useIntegerLatency=true`) and performs true-peak detection AND gain-reduction application entirely inside that oversampled domain, before downsampling back — this is the key design choice that makes the never-exceed guarantee hold by construction rather than by inference. A monotonic-deque sliding-window minimum over the raw per-sample required gain gives an instantaneous, click-free attack (the "lookahead" is literally how far into the future that window looks); a lookahead-delayed audio ring buffer keeps the gain envelope time-aligned with the signal it multiplies; **Release Curve** (Exponential/Linear/Smooth) shapes the release phase only — attack always stays instantaneous regardless of curve. **Clip Mix** (0–100%) blends the transparent limiter path with an alternate tanh soft-clip "clipper" path, both bound by the same final hard clamp. A small internal headroom margin (0.3 dB, +extra scaled by Clip Mix for the clipper path) plus a final per-oversampled-sample hard clamp to the nominal ceiling back-stop the guarantee. **Dither** (Off/16-bit/24-bit TPDF) is applied post-downsample at the base rate. The engine also computes and publishes **metering** (gain reduction, output true peak, Momentary/Short-Term/Integrated LUFS per a documented-approximation of ITU-R BS.1770-4) via relaxed atomics for a future GUI (M3) or any host/test consumer. Reported latency = Lookahead (ms→samples) + the oversampler's own round-trip latency (`juce::dsp::Oversampling::getLatencyInSamples()`); Lookahead is deliberately a prepare-time-latched "setup" parameter (not live-automatable) since it sizes real-time buffers and changes the reported latency — documented explicitly in `docs/architecture.md` and in code comments, not silently dropped. Full parameter reference and usage tips: `docs/manual.md`.

## Build & test
```sh
export CPM_SOURCE_CACHE="$HOME/.cache/CPM"      # shared JUCE 8.0.14 + Catch2 cache
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target Tests Apotheosis_Standalone --parallel 4
ctest --test-dir build --output-on-failure
```
Release/universal + pluginval + auval run in CI, not locally.

## Conventions & guardrails
- JUCE 8.0.14 via CPM · C++20 · AGPLv3 · Pamplejuce `SharedCode` pattern · manufacturer `Yvsv`, plugin code `Apth`, `com.yvesvogl.apotheosis`.
- **Real-time safety:** no alloc/lock/file-IO/logging on the audio thread; allocate in `prepareToPlay`; `reset()` clears all state; `ScopedNoDenormals`; smoothed params; report latency via `setLatencySamples` where the chain adds any.
- **DryWetMixer gotcha (JUCE 8.0.14):** prime `setWetMixProportion(mix)` before `reset()` in `prepare()` (else it ramps from 100% wet). See sibling `overture`.
- **`main` is protected** — no direct commits; feature branch + PR, green CI required (Conventional Commits). New DSP needs tests (null/reference, NaN/Inf sweep, state round-trip, latency).

## Roadmap
GitHub milestones (M1 DSP & tests · M2 presets/state · M3 GUI & a11y · M4 release/signing/v1.0.0) + issues. Read with `gh issue list --repo basilica-audio/apotheosis`.

## Suite context
Style references: sibling `basilica-audio/overture` and `basilica-audio/crypta`. The suite: overture, tenebrae, nave, silentium, requiem, seraph, aureate, firmament, triptych, apotheosis, crypta.
