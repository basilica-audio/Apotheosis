# Apotheosis — brickwall true-peak limiter (mastering)

Per-repo working memory for Claude Code sessions on this plugin. Part of the **Basilica Audio** plugin suite — sacred-architecture DSP for heavy music (`github.com/basilica-audio`).

## What this is
Apotheosis is the "brickwall true-peak limiter (mastering)" member of the suite. AU / VST3 / Standalone, JUCE 8.

## Status (v0.3.0 — M3 photoreal GUI done)
Core DSP unchanged since v0.2.0, **103/103 Catch2 tests green** locally (80 DSP/preset/i18n + 23 new GUI tests). CI (macOS + Windows, pluginval strictness 10 + auval) previously green as of v0.2.0; re-verify after this PR. GUI is now the M3 photoreal skeuomorphic editor (`src/PluginEditor.*`, `src/PluginEditorLayout.h`, `src/gui/*` copied verbatim from the suite pilot `basilica-audio/silentium`): a pre-rendered faceplate, brass filmstrip knobs, three `AnalogMeter` needle displays (Gain Reduction/True Peak/LUFS), and BasilicaLookAndFeel-styled combo boxes for the three discrete choices (no bespoke asset for those yet — see `docs/gui-components.md`). No signing yet (roadmap M4). v0.2.0 shipped: four research-derived controls (Attack, Auto Release, Stereo Link, Dither Shape — see `docs/design-brief.md`/`docs/research-notes.md`, all additive, bit-identical to v1 at their defaults), the suite's M2 preset system (`src/presets/`, copied verbatim from the pilot `basilica-audio/nave` — see `docs/preset-system-notes.md`), 8 factory presets, and a German frame-string localisation. See GitHub **milestones/issues** for the open work.

## DSP
`TruePeakLimiterEngine` (`src/dsp/TruePeakLimiterEngine.{h,cpp}`) does input gain, then upsamples 4x (`juce::dsp::Oversampling`, half-band polyphase IIR, `useIntegerLatency=true`) and performs true-peak detection AND gain-reduction application entirely inside that oversampled domain, before downsampling back — this is the key design choice that makes the never-exceed guarantee hold by construction rather than by inference. A monotonic-deque sliding-window minimum over the raw per-sample required gain gives an instantaneous, click-free attack (the "lookahead" is literally how far into the future that window looks); a lookahead-delayed audio ring buffer keeps the gain envelope time-aligned with the signal it multiplies. As of v0.2.0 the gain envelope and sliding-window-minimum are **per channel** (was a single shared instance in v1) to support **Stereo Link** (0–100%, default 100% fully max-linked — crossfades each channel's detector input between the max-linked peak and its own, bit-identical to v1 at 100%). **Attack** (0–50 ms, default 0 ms) classifies each gain-reduction event by its *windowed* (not raw) duration — short events recover via a fixed near-instant coefficient, long ones follow the normal Release path; 0 ms is bit-identical to v1. **Auto Release** (0–100%, default 0%) modulates the effective Release time from a slow (2 s time constant), asymmetric running average of recent gain-reduction depth — a from-scratch reasoned design, not a vendor algorithm copy; 0% is an exact no-op. **Release Curve** (Exponential/Linear/Smooth) shapes the release phase only — attack always stays instantaneous regardless of curve. **Clip Mix** (0–100%) blends the transparent limiter path with an alternate tanh soft-clip "clipper" path, both bound by the same final hard clamp. A small internal headroom margin (0.3 dB, +extra scaled by Clip Mix for the clipper path) plus a final per-oversampled-sample hard clamp to the nominal ceiling back-stop the guarantee. **Dither** (Off/16-bit/24-bit TPDF) is applied post-downsample at the base rate, crossed with **Dither Shape** (Flat/Shaped, default Flat) — Shaped runs the injected TPDF noise through a simple fixed first-order noise-shaping filter; Flat is bit-identical to v1. The engine also computes and publishes **metering** (gain reduction, output true peak, Momentary/Short-Term/Integrated LUFS per a documented-approximation of ITU-R BS.1770-4) via relaxed atomics for a future GUI (M3) or any host/test consumer. Reported latency = Lookahead (ms→samples) + the oversampler's own round-trip latency (`juce::dsp::Oversampling::getLatencyInSamples()`); Lookahead is deliberately a prepare-time-latched "setup" parameter (not live-automatable) since it sizes real-time buffers and changes the reported latency — documented explicitly in `docs/architecture.md` and in code comments, not silently dropped. Full parameter reference and usage tips: `docs/manual.md`.

## Presets & i18n
`src/presets/` (`PresetManager`, `PresetBar`, `Localisation`) implements the Basilica Audio suite-wide M2 preset system, copied verbatim from the pilot (`basilica-audio/nave`) per `docs/preset-system-notes.md`'s replication recipe. Eight factory presets embedded via BinaryData (`presets/factory/*.json`, documented per-preset in `docs/presets.md`); user presets at `~/Library/Audio/Presets/Yves Vogl/Apotheosis/` (macOS). The preset bar's frame strings are localised to German automatically via `SystemStats::getUserLanguage()` (`resources/i18n/de.txt`) — parameter names/units never translate.

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
