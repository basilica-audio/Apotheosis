#pragma once

#include <juce_dsp/juce_dsp.h>

#include <atomic>
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
//   input -> Input Gain -> [4x oversampled: per-sample true-peak detection
//         -> lookahead sliding-window-minimum gain envelope -> release
//         smoothing (selectable curve) -> apply gain / clip-mix blend to
//         the lookahead-delayed signal -> hard ceiling clamp] -> Dither
//         -> output
//
// Detection AND gain-reduction application both happen inside the same
// oversampled domain (rather than detecting there and correcting at the
// base rate), which is what makes the true-peak guarantee meaningful: the
// gain multiply acts directly on the highest-resolution representation of
// the signal, before it is filtered back down to the host's sample rate.
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

    // Lookahead delay buffer for the (oversampled) audio signal, sized and
    // latched at prepare() time.
    juce::AudioBuffer<float> delayBuffer;
    int delayCapacity = 1;
    int delayWritePos = 0;

    // Sliding-window-minimum (monotonic deque, implemented over two fixed-
    // capacity ring arrays) over the raw, non-smoothed per-sample required
    // gain. This is what gives the "instantaneous, non-clipping attack" the
    // DSP spec calls for: by the time a given (lookahead-delayed) sample
    // reaches the gain-multiply stage, the minimum gain required by every
    // sample up to lookaheadSamplesOS samples into its future has already
    // been folded in. O(1) amortised per sample; capacity is fixed and
    // allocated in prepare() - no allocation on the audio thread.
    std::vector<float> slidingValues;
    std::vector<juce::int64> slidingIndices;
    int slidingCapacity = 1;
    int slidingHead = 0;
    int slidingCount = 0;
    juce::int64 slidingSampleCounter = 0;
    int windowSize = 1;

    int lookaheadSamplesBase = 0;
    int lookaheadSamplesOS = 0;
    int totalLatencySamples = 0;

    // Release-smoothed gain-reduction state, in the oversampled domain.
    float currentGain = 1.0f;
    // Second stage used only by ReleaseCurve::smooth (a two-pole cascade);
    // kept in sync with currentGain on every attack so switching curves (or
    // switching between attack/release) never reintroduces a stale lag.
    float smoothReleaseStage = 1.0f;

    ReleaseCurve releaseCurve = ReleaseCurve::exponential;
    DitherMode ditherMode = DitherMode::off;
    juce::Random ditherRng;

    // Last commanded values (ParameterLayout defaults until a setter is
    // called), re-applied on every prepare() so re-prepare (sample-rate
    // change, etc.) never silently resets a live parameter back to a
    // built-in default.
    float lastInputGainDb = 0.0f;
    float lastCeilingDb = -1.0f;
    float lastReleaseMs = 50.0f;
    float lastLookaheadMs = 5.0f;
    float lastClipMixPercent = 0.0f;

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

    float pushSlidingMin (float value) noexcept;
    float delayPushAndRead (int channel, float newSample) noexcept;
    void delayAdvance() noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TruePeakLimiterEngine)
};
