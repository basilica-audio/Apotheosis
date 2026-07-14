#pragma once

#include <juce_dsp/juce_dsp.h>

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
//         smoothing -> apply gain to the lookahead-delayed signal -> hard
//         ceiling clamp] -> output
//
// Detection AND gain-reduction application both happen inside the same
// oversampled domain (rather than detecting there and correcting at the
// base rate), which is what makes the true-peak guarantee meaningful: the
// gain multiply acts directly on the highest-resolution representation of
// the signal, before it is filtered back down to the host's sample rate.
class TruePeakLimiterEngine
{
public:
    TruePeakLimiterEngine();

    // Allocates all DSP state (oversampler, lookahead delay buffer,
    // sliding-window-minimum ring buffer), sized for the CURRENT Lookahead
    // parameter value (see setLookaheadMs()). Must be called (and
    // completed) before the first process() call, and again whenever sample
    // rate/block size/channel count change - or whenever Lookahead itself
    // changes. Lookahead is therefore treated as a "setup" parameter:
    // setLookaheadMs() only takes effect at the *next* prepare() call, not
    // mid-stream, both because it changes the plugin's reported latency and
    // because resizing these buffers is not real-time safe.
    void prepare (const juce::dsp::ProcessSpec& spec);

    // Clears all oversampler/delay-line/envelope state without
    // deallocating. Safe to call from the audio thread.
    void reset();

    // Processes `block` in place. `block` must have at most the maximum
    // sample/channel counts declared to prepare(); a zero-sample block is a
    // safe no-op. No allocation occurs here. Non-finite (NaN/Inf) input
    // samples are sanitised to 0 before entering the oversampler, so a
    // momentarily broken upstream signal can never poison the internal
    // filter/envelope state indefinitely.
    void process (juce::dsp::AudioBlock<float>& block);

    // Parameter setters, in real units. InputGain/Ceiling/Release are safe
    // to call every block from the audio thread (smoothed internally,
    // cheap, no allocation). Lookahead is latched at prepare() - see above.
    void setInputGainDb (float newInputGainDb);
    void setCeilingDb (float newCeilingDb);
    void setReleaseMs (float newReleaseMs);
    void setLookaheadMs (float newLookaheadMs);

    // Total reported latency in samples, valid after prepare() has run:
    // Lookahead (converted to samples at the prepared sample rate) plus the
    // 4x oversampler's own round-trip latency.
    int getLatencySamples() const noexcept { return totalLatencySamples; }

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

    double sampleRate = 44100.0;

    juce::dsp::Gain<float> inputGain;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> ceilingSmoothed;

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

    // Last commanded values (ParameterLayout defaults until a setter is
    // called), re-applied on every prepare() so re-prepare (sample-rate
    // change, etc.) never silently resets a live parameter back to a
    // built-in default.
    float lastInputGainDb = 0.0f;
    float lastCeilingDb = -1.0f;
    float lastReleaseMs = 50.0f;
    float lastLookaheadMs = 5.0f;

    float pushSlidingMin (float value) noexcept;
    float delayPushAndRead (int channel, float newSample) noexcept;
    void delayAdvance() noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TruePeakLimiterEngine)
};
