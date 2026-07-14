# Apotheosis — brickwall true-peak limiter (mastering)

Per-repo working memory for Claude Code sessions on this plugin. Part of the **Metal up your ass** symphonic-metal plugin suite (`github.com/metal-up-your-ass`).

## What this is
Apotheosis is the "brickwall true-peak limiter (mastering)" member of the suite. AU / VST3 / Standalone, JUCE 8.

## Status (v0.1 — bootstrap complete)
Core DSP working, **19 Catch2 tests green**, CI (macOS + Windows, pluginval strictness 10 + auval) green. GUI is a functional v0.1 slider editor (custom LookAndFeel is roadmap M3). No signing yet (roadmap M4). Open work is tracked in this repo's GitHub **milestones/issues**.

## DSP
TruePeakLimiterEngine (src/dsp/TruePeakLimiterEngine.{h,cpp}) does input gain, then upsamples 4x (juce::dsp::Oversampling, half-band polyphase IIR, useIntegerLatency=true) and performs true-peak detection AND gain-reduction application entirely inside that oversampled domain, before downsampling back — this is the key design choice that makes the never-exceed guarantee hold by construction rather than by inference. A monotonic-deque sliding-window minimum over the raw per-sample required gain gives an instantaneous, click-free attack (the "lookahead" is literally how far into the future that window looks); a lookahead-delayed audio ring buffer keeps the gain envelope time-aligned with the signal it multiplies; release is the only smoothed time constant (one-pole ramp back to unity). A small internal headroom margin (0.3 dB) plus a final per-oversampled-sample hard clamp to the nominal ceiling back-stop the guarantee. Reported latency = Lookahead (ms→samples) + the oversampler's own round-trip latency (juce::dsp::Oversampling::getLatencyInSamples()); Lookahead is deliberately a prepare-time-latched "setup" parameter (not live-automatable) since it sizes real-time buffers and changes the reported latency — documented explicitly in docs/architecture.md and in code comments, not silently dropped.

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
GitHub milestones (M1 DSP & tests · M2 presets/state · M3 GUI & a11y · M4 release/signing/v1.0.0) + issues. Read with `gh issue list --repo metal-up-your-ass/apotheosis`.

## Suite context
Style references: sibling `metal-up-your-ass/overture` and `metal-up-your-ass/twist-your-guts`. The suite: overture, tenebrae, nave, silentium, requiem, seraph, aureate, firmament, triptych, apotheosis, twist-your-guts.
