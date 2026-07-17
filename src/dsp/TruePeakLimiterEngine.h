#pragma once

#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <limits>
#include <vector>

// The complete Apotheosis signal path, independent of juce::AudioProcessor
// so it can be exercised directly by unit tests without instantiating a
// full plugin (see tests/LimiterTests.cpp, tests/LatencyTests.cpp). Owns all
// DSP state; every buffer/oversampler is allocated in prepare() and never
// reallocated on the audio thread.
//
// Signal flow (see docs/architecture.md for the full diagram and the
// latency/headroom-margin rationale):
//
//   input -> Input Gain -> [4x oversampled: per-channel true-peak detection
//         (Stereo Link-weighted) -> lookahead sliding-window-minimum gain
//         envelope, per channel -> Attack classifier (transient vs sustain
//         routing) -> Release (curve-shaped, Auto-Release-modulated) ->
//         apply gain / clip-mix blend to the lookahead-delayed signal ->
//         hard ceiling clamp] -> Dither (Flat/Shaped) -> output
//
// Detection AND gain-reduction application both happen inside the same
// oversampled domain (rather than detecting there and correcting at the
// base rate), which is what makes the true-peak guarantee meaningful: the
// gain multiply acts directly on the highest-resolution representation of
// the signal, before it is filtered back down to the host's sample rate.
//
// v0.2.0 (docs/design-brief.md) adds four research-derived controls -
// Attack, Auto Release, Stereo Link, Dither Shape - each purely additive
// with a default that reproduces v1's exact prior behaviour bit-for-bit
// (see tests/RegressionTests.cpp). This is why the gain envelope
// (currentGain/smoothReleaseStage), the sliding-window-minimum, and the
// per-event Attack-classifier tracking below are now PER CHANNEL (were a
// single shared scalar/window in v1): Stereo Link < 100% requires each
// channel to be able to detect and limit fully independently, and at the
// default 100% both channels receive an identical (max-linked) input peak
// every oversampled sample, so their independently-computed envelopes stay
// numerically identical to each other and to v1's single shared envelope -
// see setStereoLinkPercent()'s docs and TruePeakLimiterEngine.cpp.
//
// Metering (gain reduction, output true peak, momentary/short-term/
// integrated LUFS) is computed here too, published via relaxed atomics so a
// future GUI (roadmap M3) can poll it from the message thread without any
// lock/allocation on the audio thread.
class TruePeakLimiterEngine
{
public:
    // Selects the shape of the release ramp back towards unity gain once
    // the required gain reduction starts decreasing. Attack is always
    // instantaneous (via the lookahead minimum), regardless of this choice
    // - only the release phase is affected. Indices match the "Release
    // Curve" AudioParameterChoice (see ParameterLayout.cpp).
    enum class ReleaseCurve
    {
        exponential = 0, // Classic one-pole ramp (the original v0.1 behaviour).
        linear = 1,      // Constant-rate approach to the target gain.
        smooth = 2,      // Two-stage (critically-damped) one-pole cascade: a softer, overshoot-free "S" onset, at the cost of a slower perceived release.
    };

    // Selects TPDF dither added at the very end of the chain, after
    // downsampling back to the base rate - i.e. at the output word length,
    // the conventional placement for dither. Indices match the "Dither"
    // AudioParameterChoice.
    enum class DitherMode
    {
        off = 0,
        bit16 = 1,
        bit24 = 2,
    };

    // Selects whether the dither noise injected by DitherMode (above) is
    // plain flat TPDF (v1's only behaviour) or pushed through a fixed
    // noise-shaping filter that redistributes quantisation-noise energy
    // toward the top of the audible band. Only has an audible effect when
    // DitherMode != off. Indices match the "Dither Shape" AudioParameterChoice.
    // Flat is a from-scratch, project-owned fixed filter design - not a
    // copy of any vendor's specific shaping curve (see
    // docs/design-brief.md's Honesty section).
    enum class DitherShape
    {
        flat = 0,
        shaped = 1,
    };

    TruePeakLimiterEngine();

    // Allocates all DSP state (oversampler, lookahead delay buffer,
    // sliding-window-minimum ring buffer, LUFS metering windows), sized for
    // the CURRENT Lookahead parameter value (see setLookaheadMs()). Must be
    // called (and completed) before the first process() call, and again
    // whenever sample rate/block size/channel count change - or whenever
    // Lookahead itself changes. Lookahead is therefore treated as a "setup"
    // parameter: setLookaheadMs() only takes effect at the *next* prepare()
    // call, not mid-stream, both because it changes the plugin's reported
    // latency and because resizing these buffers is not real-time safe.
    void prepare (const juce::dsp::ProcessSpec& spec);

    // Clears all oversampler/delay-line/envelope/metering state without
    // deallocating. Safe to call from the audio thread.
    void reset();

    // Processes `block` in place. A zero-sample block is a safe no-op. No
    // allocation occurs here. Non-finite (NaN/Inf) input samples are
    // sanitised to 0 before entering the oversampler, so a momentarily
    // broken upstream signal can never poison the internal filter/envelope
    // state indefinitely.
    //
    // `block` may contain MORE samples than the maximumBlockSize declared
    // to prepare() (some hosts occasionally hand over an oversized block -
    // offline bounce/render, buffer-size renegotiation - see issue #14):
    // juce::dsp::Oversampling's internal buffer is sized to exactly that
    // maximum at prepare()-time and only guards its writes with a
    // debug-only jassert, so passing an oversized block straight through
    // would silently corrupt the heap in a Release build. process() detects
    // this and transparently loops over prepare()-sized chunks instead (see
    // processChunk() below) rather than truncating the block, so the
    // never-exceed-Ceiling guarantee still holds for every sample.
    void process (juce::dsp::AudioBlock<float>& block);

    // Parameter setters, in real units. InputGain/Ceiling/Release/ClipMix
    // are safe to call every block from the audio thread (smoothed
    // internally, cheap, no allocation). ReleaseCurve/Dither are cheap
    // discrete-mode switches (no allocation, no buffer resize) also safe to
    // call every block. Lookahead is latched at prepare() - see above.
    void setInputGainDb (float newInputGainDb);
    void setCeilingDb (float newCeilingDb);
    void setReleaseMs (float newReleaseMs);
    void setLookaheadMs (float newLookaheadMs);
    void setReleaseCurve (int newReleaseCurveIndex) noexcept;
    void setDitherMode (int newDitherModeIndex) noexcept;
    void setClipMixPercent (float newClipMixPercent) noexcept;

    // v0.2.0 deep-dive additions (docs/design-brief.md) - all cheap to call
    // every block from the audio thread (no allocation), same contract as
    // the setters above. See ParameterIds.h for the full per-control
    // rationale/sourcing.
    void setAttackMs (float newAttackMs) noexcept;
    void setAutoReleasePercent (float newAutoReleasePercent) noexcept;
    void setStereoLinkPercent (float newStereoLinkPercent) noexcept;
    void setDitherShape (int newDitherShapeIndex) noexcept;

    // Total reported latency in samples, valid after prepare() has run:
    // Lookahead (converted to samples at the prepared sample rate) plus the
    // 4x oversampler's own round-trip latency.
    int getLatencySamples() const noexcept { return totalLatencySamples; }

    // Metering readout, safe to call from any thread (message thread GUI
    // polling in particular). Values reflect the most recently processed
    // non-empty block; they do not change on a zero-sample process() call.
    float getGainReductionDb() const noexcept { return gainReductionDbAtomic.load (std::memory_order_relaxed); }
    float getOutputTruePeakDb() const noexcept { return outputTruePeakDbAtomic.load (std::memory_order_relaxed); }
    float getMomentaryLufs() const noexcept { return momentaryLufsAtomic.load (std::memory_order_relaxed); }
    float getShortTermLufs() const noexcept { return shortTermLufsAtomic.load (std::memory_order_relaxed); }
    float getIntegratedLufs() const noexcept { return integratedLufsAtomic.load (std::memory_order_relaxed); }

private:
    static constexpr int oversamplingFactorPow2 = 2; // 2^2 = 4x oversampling
    static constexpr int oversamplingFactor = 1 << oversamplingFactorPow2;
    static constexpr double smoothingTimeSeconds = 0.05;

    // Extra internal headroom subtracted (in dB) from the user-facing
    // Ceiling before computing the gain-reduction *target*, to absorb the
    // small amount of reconstruction-filter ripple the oversampler's own
    // downsampling stage can introduce. The final per-sample hard clamp
    // (see process()) still backstops at the exact nominal ceiling
    // regardless, so the never-exceed guarantee does not depend on this
    // margin being exactly right - see docs/architecture.md.
    static constexpr float headroomMarginDb = 0.3f;

    // Additional headroom (dB, at Clip Mix = 100%, scaled linearly by the
    // current Clip Mix amount) applied only to the tanh soft-clip "clipper"
    // path's target, on top of headroomMarginDb. The clipper path generates
    // new high-frequency harmonic content (unlike the linear gain-reduction
    // path), which needs a little more margin against downsample
    // reconstruction-filter ripple - see process(). Has zero effect at
    // Clip Mix = 0%.
    static constexpr float clipExtraHeadroomDb = 1.0f;

    // LUFS metering absolute gate (ITU-R BS.1770-4): momentary loudness
    // readings quieter than this are excluded from the Integrated Loudness
    // accumulator. See docs/architecture.md for the documented deviations
    // from the full two-pass relative-gated spec algorithm (this engine
    // implements the absolute gate only, evaluated once per processed
    // block rather than per 400ms gating block, for O(1) real-time-safe
    // accumulation).
    static constexpr float integratedGateLufs = -70.0f;

    // Maximum channel count this engine supports independent per-channel
    // state for - matches PluginProcessor::isBusesLayoutSupported's
    // mono/stereo-only contract (same fixed-size-array pattern as
    // kWeightShelf/kWeightHighPass below, pre-dating v0.2.0).
    static constexpr int maxChannels = 2;

    // Attack classifier (v0.2.0, docs/design-brief.md): a fixed, near-
    // instant release time constant used ONLY for a gain-reduction event
    // classified as a short transient (its windowed-gain-below-unity
    // duration is shorter than the current Attack setting) - independent of Release/
    // Auto Release, mirroring the reference class's documented "transient
    // stage releases near-instantly regardless of the Release knob"
    // (research-notes.md S2a). A reasoned, fixed constant, not sourced from
    // any competitor's numeric default (none was recoverable - see
    // research-notes.md S6).
    static constexpr float fastAttackReleaseMs = 1.0f;

    // Auto Release (v0.2.0): the running average of recent gain-reduction
    // depth is a one-pole low-pass with this time constant ("order of
    // seconds" per docs/design-brief.md), plus the dB reference used to
    // normalise that average to a 0..1 "how deep/sustained has it been"
    // factor (depthNorm). All are reasoned engineering choices (see
    // docs/design-brief.md's Honesty section) chosen so that Auto Release =
    // 0% is an exact (not approximate) no-op - see processChunk().
    //
    // The lengthen/shorten ranges are deliberately ASYMMETRIC around the
    // depthNorm = 0 ("idle, nothing has needed gain reduction recently")
    // baseline: depthNorm near 1 (deep/sustained recent reduction) can
    // lengthen the effective Release time up to 2x at 100% Auto Release,
    // while depthNorm near 0 (sparse/idle) only shortens it modestly (down
    // to 0.7x). This asymmetry is what makes an isolated, brief peak's own
    // post-peak recovery change only mildly across the full Auto Release
    // sweep (its depthNorm barely moves off 0, since one brief peak against
    // a multi-second averager stays close to the idle baseline) while a
    // sustained, dense passage's recovery changes dramatically (its
    // depthNorm saturates near 1) - directly the "reacts quickly to
    // transients, responds more slowly to sustained content" qualitative
    // principle the brief cites, and the property
    // tests/AttackAutoReleaseTests.cpp's Guarantee 3 test asserts.
    static constexpr double autoReleaseTimeConstantSeconds = 2.0;
    static constexpr float autoReleaseModDepthReferenceDb = 4.0f;
    static constexpr float autoReleaseLengthenRangeFraction = 2.5f;
    static constexpr float autoReleaseShortenRangeFraction = 0.3f;

    double sampleRate = 44100.0;

    // The maximumBlockSize declared to prepare() - i.e. the largest sample
    // count the oversampler's internal buffer (and every other prepare()-
    // sized buffer below) was actually allocated for. process() chunks any
    // larger incoming block down to this size before calling processChunk()
    // - see process()'s doc comment and issue #14. Always >= 1 once
    // prepare() has run.
    size_t maxPreparedBlockSamples = 0;

    juce::dsp::Gain<float> inputGain;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> ceilingSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> clipMixSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> stereoLinkSmoothed;

    // Lookahead delay buffer for the (oversampled) audio signal, sized and
    // latched at prepare() time.
    juce::AudioBuffer<float> delayBuffer;
    int delayCapacity = 1;
    int delayWritePos = 0;

    // Sliding-window-minimum (monotonic deque, implemented over two fixed-
    // capacity ring arrays PER CHANNEL - see the class-level v0.2.0 docs
    // above) over the raw, non-smoothed per-sample required gain. This is
    // what gives the "instantaneous, non-clipping attack" the DSP spec
    // calls for: by the time a given (lookahead-delayed) sample reaches the
    // gain-multiply stage, the minimum gain required by every sample up to
    // lookaheadSamplesOS samples into its future has already been folded
    // in. O(1) amortised per sample; capacity is fixed and allocated in
    // prepare() - no allocation on the audio thread. `slidingSampleCounter`
    // is a single shared "now" tick (not per channel): both channels'
    // windows are evaluated against the same tick every oversampled sample,
    // which is what keeps them numerically identical to each other (and to
    // v1's single shared window) when Stereo Link = 100% - see
    // pushSlidingMin().
    std::vector<float> slidingValues[maxChannels];
    std::vector<juce::int64> slidingIndices[maxChannels];
    int slidingCapacity = 1;
    int slidingHead[maxChannels] = { 0, 0 };
    int slidingCount[maxChannels] = { 0, 0 };
    juce::int64 slidingSampleCounter = 0;
    int windowSize = 1;

    int lookaheadSamplesBase = 0;
    int lookaheadSamplesOS = 0;
    int totalLatencySamples = 0;

    // Release-smoothed gain-reduction state, in the oversampled domain - PER
    // CHANNEL as of v0.2.0 (was a single shared scalar in v1; see the
    // class-level docs above for why Stereo Link requires this).
    float currentGain[maxChannels] = { 1.0f, 1.0f };
    // Second stage used only by ReleaseCurve::smooth (a two-pole cascade);
    // kept in sync with currentGain on every attack so switching curves (or
    // switching between attack/release) never reintroduces a stale lag.
    float smoothReleaseStage[maxChannels] = { 1.0f, 1.0f };

    // Attack classifier (v0.2.0) per-channel event-duration tracking: counts
    // consecutive oversampled samples (per channel) whose WINDOWED
    // (lookahead-min) gain is below unity - i.e. a candidate gain-reduction
    // "event" as defined in docs/design-brief.md, measured on the same
    // smoothed signal that drives the attack/release envelope (not the raw
    // pre-window gain - see TruePeakLimiterEngine.cpp's processChunk() for
    // why: raw per-sample gain dips below unity only within each cycle's
    // brief peak excursion for any oscillating material, which would
    // misclassify essentially all continuous programme material as ultra-
    // short "transients"). `lastCompletedEventRawSamples` is a
    // sentinel (a very large value) until the first event completes, which
    // deliberately makes it impossible to classify as "shorter than Attack"
    // before any real event has been observed - see processChunk().
    int currentEventRawSamples[maxChannels] = { 0, 0 };
    juce::int64 lastCompletedEventRawSamples[maxChannels] = {
        std::numeric_limits<juce::int64>::max() / 2, std::numeric_limits<juce::int64>::max() / 2
    };

    // Auto Release (v0.2.0): a single shared (not per-channel) slow-moving
    // average of recent gain-reduction depth, in dB (0 = no reduction).
    // Updated once per processed chunk from the block's own measured depth
    // - see processChunk().
    double autoReleaseDepthAvgDb = 0.0;

    ReleaseCurve releaseCurve = ReleaseCurve::exponential;
    DitherMode ditherMode = DitherMode::off;
    DitherShape ditherShape = DitherShape::flat;
    juce::Random ditherRng;

    // Dither Shape (v0.2.0) per-channel noise-shaping filter state: the
    // previous chunk's raw TPDF draw, fed into a simple fixed first-order
    // differencing filter when DitherShape::shaped is selected - see
    // processChunk()'s Dither section. Unused (but harmlessly updated) when
    // DitherShape::flat, which keeps Flat's output bit-identical to v1's
    // plain-TPDF dither at every setting.
    float previousDitherTpdf[maxChannels] = { 0.0f, 0.0f };

    // Last commanded values (ParameterLayout defaults until a setter is
    // called), re-applied on every prepare() so re-prepare (sample-rate
    // change, etc.) never silently resets a live parameter back to a
    // built-in default.
    float lastInputGainDb = 0.0f;
    float lastCeilingDb = -1.0f;
    float lastReleaseMs = 50.0f;
    float lastLookaheadMs = 5.0f;
    float lastClipMixPercent = 0.0f;
    float lastAttackMs = 0.0f;
    float lastAutoReleasePercent = 0.0f;
    float lastStereoLinkPercent = 100.0f;

    //==================================================================
    // Metering state (published via atomics - see the public getters).
    //==================================================================
    std::atomic<float> gainReductionDbAtomic { 0.0f };
    std::atomic<float> outputTruePeakDbAtomic { -100.0f };
    std::atomic<float> momentaryLufsAtomic { -100.0f };
    std::atomic<float> shortTermLufsAtomic { -100.0f };
    std::atomic<float> integratedLufsAtomic { -100.0f };

    // ITU-R BS.1770-4 K-weighting pre-filter: stage 1 (high shelf) then
    // stage 2 (high pass), applied per channel at the BASE sample rate
    // (i.e. after downsampling, on the actual output signal). Up to 2
    // channels supported (mono/stereo - the only layouts this plugin
    // accepts, see PluginProcessor::isBusesLayoutSupported).
    juce::dsp::IIR::Filter<float> kWeightShelf[2];
    juce::dsp::IIR::Filter<float> kWeightHighPass[2];

    // Fixed-capacity sliding sum-of-squares window used for both the
    // Momentary (400 ms) and Short-Term (3 s) LUFS meters. O(1) amortised
    // per-sample push; capacity is fixed and allocated in prepare().
    struct LoudnessWindow
    {
        std::vector<float> buffer;
        int capacity = 1;
        int writePos = 0;
        int count = 0;
        double runningSum = 0.0;

        void prepare (int capacitySamples);
        void reset();
        double pushAndGetMeanPower (float power) noexcept;
    };

    LoudnessWindow momentaryWindow;
    LoudnessWindow shortTermWindow;

    double integratedPowerSum = 0.0;
    juce::int64 integratedSampleCount = 0;

    // Does the actual per-chunk work formerly done directly inside
    // process(): `block` here is guaranteed by process() to be no larger
    // than maxPreparedBlockSamples. All per-sample state (gain envelope,
    // lookahead delay/sliding-window, LUFS accumulators, dither RNG) lives
    // in member variables, so calling this repeatedly across sub-block
    // chunks of one oversized block is equivalent to a single call with the
    // full block.
    void processChunk (juce::dsp::AudioBlock<float>& block);

    // `nowIndex` is the shared "now" tick (see slidingSampleCounter's docs
    // above) - the caller advances it once per oversampled sample, after
    // every channel's pushSlidingMin() call for that sample, not once per
    // channel.
    float pushSlidingMin (int channel, juce::int64 nowIndex, float value) noexcept;
    float delayPushAndRead (int channel, float newSample) noexcept;
    void delayAdvance() noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TruePeakLimiterEngine)
};
