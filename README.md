<p align="center"><img src="docs/assets/icon.png" alt="Apotheosis icon" width="160"/></p>

# Apotheosis

*The final ascension — a lookahead true-peak brickwall limiter for the master.*

[![CI](https://github.com/metal-up-your-ass/apotheosis/actions/workflows/ci.yml/badge.svg)](https://github.com/metal-up-your-ass/apotheosis/actions/workflows/ci.yml)
[![License: AGPL v3](https://img.shields.io/badge/License-AGPL%20v3-blue.svg)](https://www.gnu.org/licenses/agpl-3.0)

> **Work in progress.** Apotheosis is pre-1.0 and under active development. There are no built binaries or releases yet — building from source is currently the only way to run it. Expect breaking changes until v1.0.0 ships (see [Roadmap](#roadmap)).

<!-- ==BEGIN BODY== (plugin engineer: replace this block with What it is / Features / Signal flow / Roadmap) -->
## What it is

Apotheosis is a lookahead brickwall **true-peak limiter** for the master bus, built on JUCE 8. It is the final gate before export: input gain drives an oversampled true-peak detector, a lookahead delay makes the resulting gain reduction instantaneous (no attack transient) rather than reactive, and a smooth release relaxes it back once the peak has passed. The output's true (inter-sample) peak never exceeds the Ceiling.

## Features (v0.1.0 scope)

- **Input Gain** - -12 to +24 dB trim into the limiter
- **Ceiling** - -12 to 0 dBTP true-peak target, default -1.0 dBTP (conventional mastering safety margin)
- **True-peak detection** - 4x oversampled, so inter-sample peaks the naked sample stream would hide are caught and limited, not just sample-domain peaks
- **Lookahead** - 0.1-20 ms; the mechanism that makes gain-reduction attack instantaneous and click-free rather than a reactive time constant
- **Release** - 5-1000 ms, log-mapped, how quickly gain reduction relaxes back towards unity
- **Release Curve** - Exponential / Linear / Smooth, shaping the release phase only (attack always stays instantaneous)
- **Clip Mix** - 0-100% blend between the transparent limiter path and an alternate tanh soft-clip "clipper" character, both backed by the same never-exceed-ceiling guarantee
- **Dither** - Off / 16-bit / 24-bit TPDF dither at the output word length
- **Metering (engine-side)** - gain reduction, output true peak, and Momentary/Short-Term/Integrated LUFS, published via the processor for a future GUI or any host/test harness
- Full state save/recall via `AudioProcessorValueTreeState`

## Signal flow

```
Input --> Input Gain --> [4x oversampled] true-peak detect --> lookahead min-gain envelope --> Release (curve-shaped)
                                                                                                       |
             Output <-- Dither <-- ceiling clamp <-- Clip Mix blend <-- apply gain to lookahead-delayed signal <--+
```

See [`docs/manual.md`](docs/manual.md) for the full parameter reference and usage tips, and [`docs/architecture.md`](docs/architecture.md) for the engineering breakdown, including the lookahead/release mechanism, the latency model, and the internal headroom-margin rationale.

## Roadmap

| Milestone | Description | Status |
|---|---|---|
| M1 | DSP completion & test coverage - Release Curve, Dither, Clip Mix, metering, broadened Catch2 suite | Done |
| M2 | Presets & state recall | Planned |
| M3 | Custom GUI & accessibility | Planned |
| M4 | Release engineering - signing, notarization, installers, v1.0.0 | Planned |
<!-- ==END BODY== -->

## Installation

No pre-built binaries are published yet (see the work-in-progress notice above). Once releases begin, installation will follow the standard plugin locations:

**macOS**

| Format | Path |
|---|---|
| AU (Component) | `~/Library/Audio/Plug-Ins/Components/` |
| VST3 | `~/Library/Audio/Plug-Ins/VST3/` |

If Logic Pro doesn't pick up the plugin after installing, force a rescan by resetting the AU cache:

```sh
killall -9 AudioComponentRegistrar
auval -a
```

**Windows**

| Format | Path |
|---|---|
| VST3 | `C:\Program Files\Common Files\VST3\` |

## Building from source

Requires JUCE 8.0.14, C++20, and CMake ≥ 3.24. See [`docs/building.md`](docs/building.md) for full prerequisites and step-by-step build/test commands for macOS and Windows.

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## License

Apotheosis is licensed under the [GNU Affero General Public License v3.0](LICENSE) (AGPLv3).

This project uses [JUCE](https://juce.com) 8, whose open-source tier is licensed under AGPLv3 (as of JUCE 8; JUCE 7 and earlier used GPLv3), which is why this project is AGPLv3 rather than GPLv3. See [`docs/adr/0002-agplv3-licensing.md`](docs/adr/0002-agplv3-licensing.md) for the full reasoning.

VST is a registered trademark of Steinberg Media Technologies GmbH.

Apotheosis is an independent open-source project and is not affiliated with, endorsed by, or sponsored by any plugin manufacturer.
