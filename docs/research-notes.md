# Apotheosis — Research Notes (deep-dive pass, v0.2.0 brief prep)

Category: brickwall true-peak mastering limiter. Reference class chosen for this pass:
**FabFilter Pro-L 2** (software industry standard, extremely well documented two-stage
architecture), **iZotope Ozone Maximizer** (IRC I–IV program-dependent release lineage,
well-documented character/psychoacoustic modeling claims), and the **ITU-R BS.1770-4/5**
true-peak specification itself (Annex 2) as the normative reference for what "true peak"
means and how it's conventionally measured. Waves L2 is cited qualitatively (Automatic
Release Control) but was not deep-fetched — see Honesty section in the brief for the access-
gap disclosure.

## 1. ITU-R BS.1770-4/5 true-peak measurement (normative spec, not a product)

- Recommendation ITU-R BS.1770-5 (11/2023), Annex 2, PDF:
  https://www.itu.int/dms_pubrec/itu-r/rec/bs/R-REC-BS.1770-5-202311-I!!PDF-E.pdf
- Reference algorithm: **4x oversampling** to reach ~192 kHz (or 176.4 kHz from a 44.1 kHz
  source), FIR-filtered, peak of the reconstructed oversampled signal taken as true peak. The
  documented example filter uses 4x FIR upsampling with **12 taps per phase (48 taps total)**.
  Source (search-engine synthesis of the spec's Annex 2, cross-checked against multiple
  secondary explainer pages — see the JUCE forum and Essentia docs below for corroboration):
  https://forum.juce.com/t/true-peak-measuring-is-it-really-that-simple/52601 ,
  https://essentia.upf.edu/tutorial_audioproblems_truepeakdetector.html
- Important spec nuance found: **"the spec is quite ambiguous and only says that you need to
  use a method for intersample detection that is similar or better in performance to the
  example algorithm."** I.e. 4x is the documented reference implementation's factor, not a
  hard mandated minimum — implementations are free to use 4x, 8x, or other techniques as long
  as they're at least as accurate. This directly validates Apotheosis v1's own 4x choice
  (`TruePeakLimiterEngine::oversamplingFactorPow2 = 2`) as spec-conformant, not a
  corner-cutting shortcut — **no gap here**, just confirmation worth citing in the manual.

## 2. FabFilter Pro-L 2 — the software reference point

Sources:
- Advanced settings help page: https://www.fabfilter.com/help/pro-l/using/advancedsettings
- Manual PDF: https://www.fabfilter.com/downloads/pdf/help/ffprol2-manual.pdf
- Secondary deep-dive on the time-constant architecture (well-corroborated, matches the
  official help page's own language almost verbatim, treated as a reliable secondary source):
  https://www.jonathanjetter.com/blog/fabfilter-prol2-timeconstants
- Product page (Style/algorithm list): https://www.fabfilter.com/products/pro-l-2-limiter-plug-in

### 2a. Two-stage transient/sustain architecture — the core mechanism gap
- Official manual quote (via advancedsettings page): **"The Attack and Release knobs control
  how quickly and heavily the release stage sets in. Shorter attack times will allow the
  release stage to set in sooner; longer release times will cause it to have more effect."**
- Secondary-source synthesis (Jetter), consistent with the above: Pro-L 2 runs **two limiting
  stages** — a **transient stage** that reacts to short peaks and **releases near-instantly
  regardless of the Release knob**, and a **sustain/release stage** that governs longer
  peaks and is what the Release control actually shapes. **Attack is not a traditional
  gain-reduction-slope control** (unlike a compressor); it sets the *crossover duration*
  between "this peak counts as a transient" and "this peak counts as sustained" — i.e. Attack
  functions as a *classifier threshold*, not a ramp time.
- Practical workflow lore (same source): "short lookahead + long attack + fast release" is
  the documented recipe for maximizing perceived loudness while preserving punch, because it
  biases more material into the near-instant-release transient-stage treatment.
- **Gap vs Apotheosis v1**: v1 has no Attack concept at all. Its entire architecture is a
  single-stage sliding-window-minimum (see `docs/architecture.md` — "There is no separate
  'attack time' control... attack is always instantaneous"). This is a legitimate, different,
  and defensible design (the lookahead-minimum-window approach *is* a real, well-known
  limiter topology — it's not wrong), but it means v1 cannot reproduce the reference class's
  single most load-bearing character difference: transients that are allowed to poke through
  briefly (for "punch") versus transients that are caught and held immediately (for
  "brickwall safety"). v1's docs even implicitly frame "always instantaneous attack" as an
  advantage ("no attack transient... rather than reactive") — which is defensible for the
  *ceiling guarantee* but is a real *character* limitation relative to what mastering
  engineers using the reference class actually reach for.

### 2b. Lookahead
- "The Lookahead knob sets the look-ahead time for the initial 'transient' stage. This allows
  the limiter to examine the incoming audio in advance and predict the amount of gain
  reduction needed to meet the requested output level. If the look-ahead time is very short,
  the limiter doesn't have much time to move to the desired level: this will generally have
  the effect of preserving transients better and increasing the apparent loudness, but at the
  expense of possible distortion. Longer look-ahead times are safer, but less loud."
  (fabfilter.com/help/pro-l/using/advancedsettings)
- The advancedsettings page references "very short look-ahead times (less than 0.1 ms)" as a
  meaningful, usable setting — confirming that sub-millisecond lookahead is a real, documented
  operating point in the reference class, not just a theoretical range extreme.
- No exact default numeric ms value for Lookahead/Attack/Release was recoverable from the
  pages fetched in this pass (the manual PDF's numeric defaults table was not directly
  extracted by the fetch tool) — **flagged as an access gap**, not fabricated. v1's own
  Lookahead default (5 ms, range 0.1–20 ms) is retained rather than replaced with an unsourced
  number.

### 2c. Channel linking
- "Two separate knobs for transient and release stages [of channel linking]." Guidance: the
  transient-stage link "often works well to choose less than 100% here," while the
  release-stage link "is best to start with 100%." (fabfilter.com/help/pro-l/using/advancedsettings)
- **Gap vs Apotheosis v1**: v1's detector always takes "the greater of the two (linked)
  channels' absolute values" (`TruePeakLimiterEngine.h` doc comment) — hard max-linked, no
  user control at all. The reference class treats stereo-link *amount* as a real, exposed
  control because a fully max-linked detector can pull the entire stereo image toward mono
  triggering behavior on hard-panned peaks; a partial-link option is standard practice.

### 2d. Style/algorithm plurality
- Pro-L 2 ships **eight** distinctly-voiced limiting algorithms, quoted from the product page:
  **Transparent** ("designed to stay true to the original sound and feel as much as possible,
  avoiding pumping effects and coloring"), **Dynamic** ("enhancing transients before actually
  applying limiting... excels in preserving the original punch and clarity... great on rock
  music"), **Allround** ("designed to work well on almost any program material"),
  **Aggressive** ("aggressive yet smart, near-clipping style... EDM/trance... rock, metal or
  pop"), **Modern** (Pro-L 2's new default, "sets the new standard for all-purpose,
  transparent limiting... allows for very near-zero lookahead settings"), **Bus** ("not meant
  to be transparent, rather the opposite" — for drum-bus/track duty), plus **Safe** and one
  further mode. (fabfilter.com/products/pro-l-2-limiter-plug-in)
- **Gap vs Apotheosis v1**: v1 has exactly one fixed algorithm (the sliding-window-minimum +
  optional Clip Mix blend). This is architecturally fine as a scope decision, but the
  reference class treats "which character of limiting" as a first-class, heavily marketed
  choice, not an afterthought — out of scope to fully replicate 8 algorithms pre-1.0, but the
  existing Clip Mix control is already a (correctly identified) step in this direction and
  should be framed/extended as such rather than left as a single binary blend.

## 3. iZotope Ozone Maximizer — program-dependent release lineage

Source: https://downloads.izotope.com/docs/ozone9/en/maximizer/index.html

- **IRC modes**, quoted: **IRC LL** — "Provides the intelligent digital loudness maximization
  of IRC I with lower latency. IRC LL is the lowest latency and least CPU intensive IRC
  mode." **IRC I** — "Analyzes source material and applies limiting psychoacoustically,
  reacting quickly to transients while responding more slowly to bass tones." **IRC II** —
  "Similar to IRC I, but optimized to preserve transients even more." **IRC III** — "enables
  aggressive limiting using advanced psychoacoustic modeling... very CPU-intensive, and
  produces high latency." **IRC IV** — "builds upon our existing IRC technology by shaping
  the spectrum to further reduce pumping and distortion."
- **Character styles** (IRC III): Clipping, Crisp, Balanced, Pumping. (IRC IV): Classic,
  Modern, Transient.
- **Transient Emphasis** control, quoted: "Using higher amount values for Transient Emphasis
  will result in more pronounced transients after the limiting process."
- **Ceiling guidance**, quoted: "-0.3 dB when dithering" or "-0.6 dB to -0.8 dB" for MP3/AAC
  mastering — a *different* numeric convention than the -1.0 dBTP figure v1 already uses and
  the wider streaming-platform convention (below); noted as an alternate school of thought,
  not adopted as a default replacement (see §4).
- **Character range**: "continuous adjustment from Fast (0.0) to Slow (10.0)" — i.e. the
  release-character behavior is exposed as a single continuous knob, not just a discrete mode
  choice, in at least one mode of a market-leading competitor.
- **Gap vs Apotheosis v1**: the entire IRC lineage's premise is that release behavior should
  be **program-dependent** (different per-frequency-band or per-transient/sustain release
  behavior, chosen automatically or by "character," rather than a single time constant plus a
  shape curve). v1's Release Curve (Exponential/Linear/Smooth) shapes *how* a single, uniform,
  user-set Release time is approached — it does not change *what* the effective release time
  *is* based on program material. This is the single largest conceptual gap between v1 and
  the reference class: v1 has release *shape* control but no release *program-dependence* at
  all.

## 4. Ceiling / true-peak calibration convention (streaming delivery)

Sources (secondary, cross-corroborating mastering-engineer articles, no single primary
platform spec page deep-fetched in this pass — flagged):
- https://veniamastering.studio/blog/true-peak-vs-peak-headroom-and-loudness/
- https://www.mixinglessons.com/dbtp-decibel-true-peak/
- https://matlefflerschulman.com/mastering-articles/true-peak-vs-inter-sample-peaks

- **"-1 dBTP is the standard ceiling for most platforms. Spotify, Apple Music, Tidal, and
  Deezer all specify -1 dBTP for standard delivery. Amazon Music asks for -2 dBTP."**
- **"If you master hotter (louder than -14 LUFS), Spotify actually advises even more
  headroom: make sure it stays below -2 dB TP if louder than -14 LUFS."**
- Rationale, paraphrased and cross-corroborated: "When this file gets encoded to AAC for
  Spotify or Apple Music, the encoding process may add up to 1 dB of additional peak level —
  which fits within the remaining headroom."
- **Confirms v1's existing -1.0 dBTP default is correctly sourced and mainstream-conventional
  — no change warranted to the default itself.** The gap is not the default value, it's that
  v1 has **no preset system yet** to surface the -2 dBTP Amazon/high-loudness variant as a
  discoverable, named starting point (addressed in the brief's Factory Presets section, not a
  DSP/parameter change).
- iZotope's own -0.3 / -0.6–0.8 dB guidance (§3) is a real, sourced, but *minority* alternate
  convention (tighter ceilings assuming the plugin's own dithering/format-specific tuning) —
  noted for completeness, not adopted, since it conflicts with the better-corroborated,
  multi-platform -1 dBTP convention.

## 5. Dither: noise shaping vs plain TPDF

Sources:
- https://www.numberanalytics.com/blog/dithering-101-for-music-producers (secondary,
  cross-checked against the Gearspace/Avid forum threads below for consistency, no single
  canonical primary source deep-fetched in this pass — flagged)
- https://duc.avid.com/showthread.php?t=147415
- http://audio.rightmark.org/lukin/dither/dither.pdf (Alexey Lukin, "Sonically Optimized
  Noise Shaping Techniques" — a genuine DSP-literature primary source on noise-shaping theory,
  title/author recorded, not deep-read line-by-line in this pass)

- **"TPDF is a form of dither that eliminates non-linear distortion during re-quantization
  and has no signal modulation artifacts... TPDF results in a noise floor at approximately
  -93 dBFS A-weighted."**
- **"POW-r is a family of dithering algorithms developed specifically for audio
  applications... For highly dynamic audio such as classical or orchestral music, POW-r 3 is
  recommended, while for heavily compressed music like rock, pop, dance or electronic music,
  POW-r 1 can be used."**
- **"Noise-shaped dither uses a feedback filter to push more quantization noise into high
  frequencies where human hearing is less sensitive. The perceived noise floor drops by
  redistributing more of the noise energy into less audible frequency regions."**
- Confirmed reference-class precedent for exposing this as a user choice: **"FabFilter Pro-L
  2 allows you to choose a 'Noise Shaping' setting: 'Off' (basic TPDF), 'Basic,' 'Optimized,'
  or 'Weighted.'"**
- Caveat found and worth citing directly for the Honesty section: **"dither choice is
  absolutely the LEAST important decision you will ever make in mastering... if it is
  correctly coded it only affects the LEAST significant bits."** I.e. this is a real,
  reference-class-precedented, low-risk feature gap — not a load-bearing one. Good candidate
  for a v0.2.0 addition precisely because it's low-risk (bounded to the LSB) while closing a
  named feature gap against the reference class.
- **Gap vs Apotheosis v1**: v1's Dither is plain TPDF only (Off/16-bit/24-bit,
  `TruePeakLimiterEngine::DitherMode`, `docs/architecture.md` §Dither: "computed as
  `(rng.nextFloat() - rng.nextFloat()) * lsb`"). No noise-shaping option exists.

## 6. Access gaps / not independently re-verified in this pass

- Exact numeric default values for Pro-L 2's own Lookahead/Attack/Release knobs (ms) were not
  recovered from the pages actually fetched (the manual PDF's parameter table was not
  extracted). Any v2 defaults for a new Attack-equivalent or Auto Release control in the brief
  are therefore **reasoned from the qualitative mechanism description**, not copied from a
  competitor's numeric defaults.
- Waves L2's "Automatic Release Control (ARC)" is cited only via a general web-search summary
  sentence, not a deep-fetched manual page — treated as corroborating context (a second
  well-known product with adaptive/program-dependent release), not a primary source for any
  specific number in the brief.
- iZotope's IRC I–IV per-mode CPU/latency claims and the Character list were fetched from the
  Ozone 9 manual page directly (a primary source, izotope.com-hosted documentation) — treated
  as reliable for the qualitative behavior claims quoted above.
- ITU-R BS.1770-5 Annex 2's exact algorithm text (the 4x/12-taps-per-phase detail) came from a
  search-engine synthesis rather than the PDF itself being opened and read directly in this
  pass — corroborated by two independent secondary technical pages (JUCE forum, Essentia
  docs), but not verified against the primary PDF's own text. Flagged accordingly in the brief.
