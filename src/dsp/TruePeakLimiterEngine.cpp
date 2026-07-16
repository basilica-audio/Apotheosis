#include "TruePeakLimiterEngine.h"

#include <cmath>

TruePeakLimiterEngine::TruePeakLimiterEngine() = default;

void TruePeakLimiterEngine::LoudnessWindow::prepare (int capacitySamples)
{
    capacity = juce::jmax (1, capacitySamples);
    buffer.assign (static_cast<size_t> (capacity), 0.0f);
    writePos = 0;
    count = 0;
    runningSum = 0.0;
}

void TruePeakLimiterEngine::LoudnessWindow::reset()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
    count = 0;
    runningSum = 0.0;
}

double TruePeakLimiterEngine::LoudnessWindow::pushAndGetMeanPower (float power) noexcept
{
    runningSum -= static_cast<double> (buffer[static_cast<size_t> (writePos)]);
    buffer[static_cast<size_t> (writePos)] = power;
    runningSum += static_cast<double> (power);

    writePos = (writePos + 1) % capacity;

    if (count < capacity)
        ++count;

    return count > 0 ? runningSum / static_cast<double> (count) : 0.0;
}

void TruePeakLimiterEngine::prepare (const juce::dsp::ProcessSpec& spec)
{
    sampleRate = spec.sampleRate;
    const auto numChannels = static_cast<int> (spec.numChannels);

    inputGain.setRampDurationSeconds (smoothingTimeSeconds);
    inputGain.prepare (spec);
    inputGain.setGainDecibels (lastInputGainDb);

    // 4x oversampling (2^2), half-band polyphase IIR: lower latency than
    // the equiripple FIR alternative for a given stopband quality.
    // useIntegerLatency=true so getLatencyInSamples() (and therefore the
    // reported plugin latency) is an exact integer sample count.
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        spec.numChannels,
        oversamplingFactorPow2,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true,
        true);
    oversampler->initProcessing (static_cast<size_t> (spec.maximumBlockSize));

    // Never 0, so process()'s chunking loop below can't divide the block
    // into zero-sized (i.e. infinite) chunks - see issue #14.
    maxPreparedBlockSamples = juce::jmax (static_cast<size_t> (1), static_cast<size_t> (spec.maximumBlockSize));

    const auto detectionLatencySamplesBase = static_cast<int> (std::round (oversampler->getLatencyInSamples()));

    // Lookahead is latched here (not re-read in process()) because it both
    // determines the size of the buffers below and directly contributes to
    // the plugin's reported latency - see setLookaheadMs().
    lookaheadSamplesBase = juce::jmax (0, juce::roundToInt (lastLookaheadMs * 0.001 * sampleRate));
    lookaheadSamplesOS = lookaheadSamplesBase * oversamplingFactor;

    totalLatencySamples = lookaheadSamplesBase + detectionLatencySamplesBase;

    // The sliding-window-minimum's window covers "now" plus every future
    // oversampled sample up to lookaheadSamplesOS ahead, so its size is
    // lookaheadSamplesOS + 1. A monotonic deque can never hold more entries
    // than the window size, but +1 capacity keeps the ring-index arithmetic
    // safe even at windowSize == 1 (zero lookahead).
    windowSize = lookaheadSamplesOS + 1;
    slidingCapacity = windowSize + 1;

    for (int channel = 0; channel < maxChannels; ++channel)
    {
        slidingValues[channel].assign (static_cast<size_t> (slidingCapacity), 0.0f);
        slidingIndices[channel].assign (static_cast<size_t> (slidingCapacity), 0);
        slidingHead[channel] = 0;
        slidingCount[channel] = 0;
    }

    delayCapacity = juce::jmax (1, lookaheadSamplesOS + 1);
    delayBuffer.setSize (numChannels, delayCapacity, false, true, true);

    ceilingSmoothed.reset (sampleRate, smoothingTimeSeconds);
    ceilingSmoothed.setCurrentAndTargetValue (lastCeilingDb);

    clipMixSmoothed.reset (sampleRate, smoothingTimeSeconds);
    clipMixSmoothed.setCurrentAndTargetValue (lastClipMixPercent * 0.01f);

    stereoLinkSmoothed.reset (sampleRate, smoothingTimeSeconds);
    stereoLinkSmoothed.setCurrentAndTargetValue (lastStereoLinkPercent * 0.01f);

    //==================================================================
    // LUFS metering: K-weighting filters (ITU-R BS.1770-4 analog
    // prototype, re-derived per sample rate via the standard bilinear-
    // transform shelf/high-pass design so this works correctly at any
    // supported sample rate, not just the 48 kHz the spec's published
    // digital coefficients target) plus the momentary (400 ms) and
    // short-term (3 s) sliding windows. Everything here runs at the BASE
    // sample rate (post-downsample), operating on the actual output
    // signal - see docs/architecture.md.
    //==================================================================
    constexpr float shelfFrequencyHz = 1681.9744509555319f;
    constexpr float shelfQ = 0.7071752369554196f;
    constexpr float shelfGainDb = 3.999843853973347f;
    constexpr float highPassFrequencyHz = 38.13547087602444f;
    constexpr float highPassQ = 0.5003270373238773f;

    const auto shelfCoefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
        sampleRate, shelfFrequencyHz, shelfQ, juce::Decibels::decibelsToGain (shelfGainDb));
    const auto highPassCoefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (
        sampleRate, highPassFrequencyHz, highPassQ);

    juce::dsp::ProcessSpec monoSpec { spec.sampleRate, spec.maximumBlockSize, 1 };

    for (auto& filter : kWeightShelf)
    {
        filter.coefficients = shelfCoefficients;
        filter.prepare (monoSpec);
    }

    for (auto& filter : kWeightHighPass)
    {
        filter.coefficients = highPassCoefficients;
        filter.prepare (monoSpec);
    }

    momentaryWindow.prepare (juce::jmax (1, juce::roundToInt (0.4 * sampleRate)));
    shortTermWindow.prepare (juce::jmax (1, juce::roundToInt (3.0 * sampleRate)));

    reset();
}

void TruePeakLimiterEngine::reset()
{
    inputGain.reset();

    if (oversampler != nullptr)
        oversampler->reset();

    delayBuffer.clear();
    delayWritePos = 0;

    slidingSampleCounter = 0;

    for (int channel = 0; channel < maxChannels; ++channel)
    {
        slidingHead[channel] = 0;
        slidingCount[channel] = 0;

        currentGain[channel] = 1.0f;
        smoothReleaseStage[channel] = 1.0f;

        currentEventRawSamples[channel] = 0;
        lastCompletedEventRawSamples[channel] = std::numeric_limits<juce::int64>::max() / 2;

        previousDitherTpdf[channel] = 0.0f;
    }

    autoReleaseDepthAvgDb = 0.0;

    for (auto& filter : kWeightShelf)
        filter.reset();

    for (auto& filter : kWeightHighPass)
        filter.reset();

    momentaryWindow.reset();
    shortTermWindow.reset();
    integratedPowerSum = 0.0;
    integratedSampleCount = 0;

    gainReductionDbAtomic.store (0.0f, std::memory_order_relaxed);
    outputTruePeakDbAtomic.store (-100.0f, std::memory_order_relaxed);
    momentaryLufsAtomic.store (-100.0f, std::memory_order_relaxed);
    shortTermLufsAtomic.store (-100.0f, std::memory_order_relaxed);
    integratedLufsAtomic.store (-100.0f, std::memory_order_relaxed);
}

void TruePeakLimiterEngine::setInputGainDb (float newInputGainDb)
{
    lastInputGainDb = newInputGainDb;
    inputGain.setGainDecibels (newInputGainDb);
}

void TruePeakLimiterEngine::setCeilingDb (float newCeilingDb)
{
    lastCeilingDb = newCeilingDb;
    ceilingSmoothed.setTargetValue (newCeilingDb);
}

void TruePeakLimiterEngine::setReleaseMs (float newReleaseMs)
{
    lastReleaseMs = newReleaseMs;
}

void TruePeakLimiterEngine::setLookaheadMs (float newLookaheadMs)
{
    // Latched only - see the header comment on prepare() for why this does
    // not take effect until the next prepare() call.
    lastLookaheadMs = newLookaheadMs;
}

void TruePeakLimiterEngine::setReleaseCurve (int newReleaseCurveIndex) noexcept
{
    releaseCurve = static_cast<ReleaseCurve> (juce::jlimit (0, 2, newReleaseCurveIndex));
}

void TruePeakLimiterEngine::setDitherMode (int newDitherModeIndex) noexcept
{
    ditherMode = static_cast<DitherMode> (juce::jlimit (0, 2, newDitherModeIndex));
}

void TruePeakLimiterEngine::setClipMixPercent (float newClipMixPercent) noexcept
{
    lastClipMixPercent = juce::jlimit (0.0f, 100.0f, newClipMixPercent);
    clipMixSmoothed.setTargetValue (lastClipMixPercent * 0.01f);
}

void TruePeakLimiterEngine::setAttackMs (float newAttackMs) noexcept
{
    lastAttackMs = juce::jlimit (0.0f, 50.0f, newAttackMs);
}

void TruePeakLimiterEngine::setAutoReleasePercent (float newAutoReleasePercent) noexcept
{
    lastAutoReleasePercent = juce::jlimit (0.0f, 100.0f, newAutoReleasePercent);
}

void TruePeakLimiterEngine::setStereoLinkPercent (float newStereoLinkPercent) noexcept
{
    lastStereoLinkPercent = juce::jlimit (0.0f, 100.0f, newStereoLinkPercent);
    stereoLinkSmoothed.setTargetValue (lastStereoLinkPercent * 0.01f);
}

void TruePeakLimiterEngine::setDitherShape (int newDitherShapeIndex) noexcept
{
    ditherShape = static_cast<DitherShape> (juce::jlimit (0, 1, newDitherShapeIndex));
}

float TruePeakLimiterEngine::pushSlidingMin (int channel, juce::int64 nowIndex, float value) noexcept
{
    auto& values = slidingValues[channel];
    auto& indices = slidingIndices[channel];
    auto& head = slidingHead[channel];
    auto& count = slidingCount[channel];

    const auto ringAt = [this] (int logicalIndex) noexcept
    {
        return ((logicalIndex % slidingCapacity) + slidingCapacity) % slidingCapacity;
    };

    // Pop back while the trailing entry is >= the new value: it can never
    // be the minimum again once a smaller, more recent value has arrived.
    while (count > 0 && values[static_cast<size_t> (ringAt (head + count - 1))] >= value)
        --count;

    values[static_cast<size_t> (ringAt (head + count))] = value;
    indices[static_cast<size_t> (ringAt (head + count))] = nowIndex;
    ++count;

    // Pop front while it has fallen outside the window.
    while (count > 0 && indices[static_cast<size_t> (ringAt (head))] <= nowIndex - windowSize)
    {
        head = ringAt (head + 1);
        --count;
    }

    return values[static_cast<size_t> (ringAt (head))];
}

float TruePeakLimiterEngine::delayPushAndRead (int channel, float newSample) noexcept
{
    if (lookaheadSamplesOS == 0)
        return newSample;

    const auto readPos = ((delayWritePos - lookaheadSamplesOS) % delayCapacity + delayCapacity) % delayCapacity;
    const auto delayed = delayBuffer.getSample (channel, readPos);
    delayBuffer.setSample (channel, delayWritePos, newSample);
    return delayed;
}

void TruePeakLimiterEngine::delayAdvance() noexcept
{
    delayWritePos = (delayWritePos + 1) % delayCapacity;
}

void TruePeakLimiterEngine::process (juce::dsp::AudioBlock<float>& block)
{
    const auto numSamples = block.getNumSamples();

    if (numSamples == 0)
        return;

    if (numSamples <= maxPreparedBlockSamples)
    {
        processChunk (block);
        return;
    }

    // Oversized block (larger than the maximumBlockSize declared to
    // prepare()) - see process()'s doc comment and issue #14. Chunk it down
    // rather than truncating: juce::dsp::Oversampling's internal buffer is
    // sized to exactly maxPreparedBlockSamples * oversamplingFactor samples,
    // and every processSamplesUp/Down override only guards its unchecked
    // writes with a debug-only jassert (compiled to nothing in Release), so
    // passing an oversized block straight through would silently corrupt
    // the heap. Truncating instead of chunking would be simpler but wrong
    // for a *limiter* specifically: any samples left unprocessed would pass
    // through unclamped, raw, and un-ceiling-limited, defeating the very
    // guarantee this engine exists to provide.
    size_t position = 0;

    while (position < numSamples)
    {
        const auto chunkSize = juce::jmin (maxPreparedBlockSamples, numSamples - position);
        auto chunk = block.getSubBlock (position, chunkSize);
        processChunk (chunk);
        position += chunkSize;
    }
}

void TruePeakLimiterEngine::processChunk (juce::dsp::AudioBlock<float>& block)
{
    const auto numSamples = block.getNumSamples();
    const auto numChannels = block.getNumChannels();

    // Sanitise non-finite input up front: NaN/Inf entering the oversampler's
    // internal IIR filter state would otherwise poison it indefinitely,
    // long after the offending samples themselves have passed through.
    for (size_t channel = 0; channel < numChannels; ++channel)
    {
        auto* data = block.getChannelPointer (channel);

        for (size_t sample = 0; sample < numSamples; ++sample)
            if (! std::isfinite (data[sample]))
                data[sample] = 0.0f;
    }

    juce::dsp::ProcessContextReplacing<float> context (block);
    inputGain.process (context);

    const auto ceilingDb = ceilingSmoothed.skip (static_cast<int> (numSamples));
    const auto ceilingLinear = juce::Decibels::decibelsToGain (ceilingDb);
    const auto internalTargetLinear = ceilingLinear * juce::Decibels::decibelsToGain (-headroomMarginDb);
    const auto clipMixAmount = clipMixSmoothed.skip (static_cast<int> (numSamples));
    // The tanh soft-clip path generates new high-frequency harmonic content
    // (unlike the plain gain-reduction path, which is linear), so the
    // downsample reconstruction filter's ripple against it needs a little
    // more margin than headroomMarginDb alone provides. Scaled by
    // clipMixAmount so a Clip Mix of 0% is completely unaffected (and stays
    // bit-identical to the pure limiter path - see DspFeatureTests.cpp).
    const auto clipTargetLinear = internalTargetLinear
                                   * juce::Decibels::decibelsToGain (-clipExtraHeadroomDb * clipMixAmount);

    // Stereo Link (v0.2.0): 1.0 = fully max-linked across channels (v1's
    // only behaviour, and the default), 0.0 = each channel detects fully
    // independently. See processChunk()'s per-sample loop below and
    // TruePeakLimiterEngine.h's class-level docs for why this changes
    // *which peak value feeds* each channel's own (now independent)
    // detector, rather than requiring a second detector implementation.
    const auto stereoLinkAmount = stereoLinkSmoothed.skip (static_cast<int> (numSamples));

    // Auto Release (v0.2.0): compute the *effective* Release time fed into
    // the curve state machine below, from the CURRENT (i.e. carried over
    // from the previous chunk, not yet updated by this one) slow-moving
    // gain-reduction-depth average - see the header's class-level docs and
    // ParameterIds.h::autoRelease. At lastAutoReleasePercent == 0, the
    // multiplier below is exactly 1.0f regardless of autoReleaseDepthAvgDb
    // (0.0f * anything == 0.0f, and 1.0f + 0.0f == 1.0f bit-exactly), so
    // effectiveReleaseMs is bit-identical to the plain `releaseMs` value v1
    // used - the core no-op-at-0%-regression guarantee (docs/design-brief.md
    // Guarantee 1/3).
    const auto autoReleaseAmount01 = lastAutoReleasePercent * 0.01f;
    const auto depthNorm = juce::jlimit (0.0f, 1.0f, static_cast<float> (autoReleaseDepthAvgDb) / autoReleaseModDepthReferenceDb);
    // Asymmetric around depthNorm == 0 (idle baseline) - see the header's
    // docs on autoReleaseLengthenRangeFraction/autoReleaseShortenRangeFraction
    // for why. At autoReleaseAmount01 == 0 both branches evaluate to exactly
    // 1.0f regardless of depthNorm (0.0f * anything == 0.0f), so
    // effectiveReleaseMs below is bit-identical to plain `lastReleaseMs` -
    // the no-op-at-0%-regression guarantee (docs/design-brief.md Guarantee
    // 1/3).
    const auto releaseMultiplier = (depthNorm >= 0.5f)
                                        ? 1.0f + autoReleaseAmount01 * (depthNorm - 0.5f) * 2.0f * autoReleaseLengthenRangeFraction
                                        : 1.0f - autoReleaseAmount01 * (0.5f - depthNorm) * 2.0f * autoReleaseShortenRangeFraction;
    const auto effectiveReleaseMs = juce::jmax (1.0f, lastReleaseMs * juce::jmax (0.1f, releaseMultiplier));

    const auto releaseCoeff = static_cast<float> (
        std::exp (-1.0 / (0.001 * static_cast<double> (effectiveReleaseMs) * sampleRate * oversamplingFactor)));
    // Constant per-sample step used by ReleaseCurve::linear: the time to
    // recover the full 0..1 gain range at a constant rate, anchored to the
    // same (Auto-Release-modulated) effective Release time (not directly
    // comparable to the exponential curve's 1/e time constant, but both
    // scale the same way with the Release parameter).
    const auto linearStepPerSample = 1.0f / juce::jmax (1.0f, effectiveReleaseMs * 0.001f * static_cast<float> (sampleRate) * oversamplingFactor);

    // Attack classifier (v0.2.0): the fixed, near-instant coefficient used
    // for a gain-reduction event classified as a short transient -
    // independent of Release/Auto Release/Release Curve entirely (see
    // fastAttackReleaseMs's docs in the header).
    const auto fastReleaseCoeff = static_cast<float> (
        std::exp (-1.0 / (0.001 * static_cast<double> (fastAttackReleaseMs) * sampleRate * oversamplingFactor)));

    auto osBlock = oversampler->processSamplesUp (block);
    const auto numOSSamples = osBlock.getNumSamples();

    float blockMinGain = 1.0f;
    float blockPeakLinear = 0.0f;

    // Reused per-iteration scratch (fixed-size, stack-allocated - no
    // allocation on the audio thread).
    float perChannelPeak[maxChannels] = { 0.0f, 0.0f };

    for (size_t i = 0; i < numOSSamples; ++i)
    {
        // Pass 1: per-channel peak and the max-linked peak across channels -
        // unchanged detection math from v1, just also retaining the
        // per-channel values for Stereo Link's crossfade below.
        float linkedPeak = 0.0f;

        for (size_t channel = 0; channel < numChannels; ++channel)
        {
            const auto peak = std::abs (osBlock.getChannelPointer (channel)[i]);
            perChannelPeak[channel] = peak;
            linkedPeak = juce::jmax (linkedPeak, peak);
        }

        const auto nowIndex = slidingSampleCounter;

        // Pass 2: per-channel detection, envelope, gain application. At
        // Stereo Link = 100% (default), peakForChannel below equals
        // linkedPeak for every channel - identical input driving each
        // channel's independent (but identically-seeded and identically-
        // fed) envelope, so all channels converge on the same currentGain
        // trajectory v1's single shared envelope produced - see the
        // header's class-level docs.
        for (size_t channel = 0; channel < numChannels; ++channel)
        {
            const auto ch = static_cast<int> (channel);
            const auto peakForChannel = linkedPeak * stereoLinkAmount + perChannelPeak[channel] * (1.0f - stereoLinkAmount);

            const auto rawGain = (peakForChannel > internalTargetLinear)
                                      ? (internalTargetLinear / juce::jmax (peakForChannel, 1.0e-8f))
                                      : 1.0f;

            const auto lookaheadMinGain = pushSlidingMin (ch, nowIndex, rawGain);

            // Attack classifier event-duration tracking (v0.2.0): counts
            // consecutive samples of the WINDOWED (lookahead-min) gain below
            // unity for this channel, not the raw pre-window gain. This
            // matters: for any oscillating (AC) programme material, raw
            // per-sample gain dips below unity only within each cycle's
            // brief peak excursion and returns to unity near every zero-
            // crossing, so a raw-gain-based duration would misclassify
            // almost all continuous, genuinely-sustained loud material as a
            // string of ultra-short "transients". The windowed min gain -
            // the same signal that already drives the attack/release
            // envelope below - persists below unity for the actual
            // perceptually-relevant duration a loud passage keeps the
            // limiter engaged (the sliding window's own lookahead span
            // bridges the gaps between individual cycle peaks), which is
            // the duration this classifier needs. A completed event's
            // length becomes the classifier input the next time this
            // channel enters its release phase below.
            if (lookaheadMinGain < 1.0f)
            {
                ++currentEventRawSamples[ch];
            }
            else
            {
                if (currentEventRawSamples[ch] > 0)
                    lastCompletedEventRawSamples[ch] = currentEventRawSamples[ch];

                currentEventRawSamples[ch] = 0;
            }

            // Attack is instantaneous (the lookahead min already accounts
            // for the future peak) regardless of ReleaseCurve/Attack/Auto
            // Release; only the release (increasing-gain) phase below is
            // shaped by the selected curve (and, for short-classified
            // events, the fixed fast coefficient instead).
            if (lookaheadMinGain < currentGain[ch])
            {
                currentGain[ch] = lookaheadMinGain;
                // Keep the smooth-curve's second stage in lock-step during
                // attack so switching back to release never resumes from a
                // stale (lagging) value.
                smoothReleaseStage[ch] = lookaheadMinGain;
            }
            else
            {
                // Attack classifier (v0.2.0): if the event that most
                // recently drove this channel's gain down was shorter than
                // the current Attack setting, its recovery uses the fixed
                // fast coefficient instead of the (curve-shaped, Auto-
                // Release-modulated) normal path - see
                // docs/design-brief.md's Attack module spec. At
                // lastAttackMs == 0 (default) this condition can never be
                // true (a duration can't be < 0), so every event is
                // classified "sustained" and the switch below always runs -
                // bit-identical regression to v1 (Guarantee 1).
                const auto lastEventMs = static_cast<double> (lastCompletedEventRawSamples[ch])
                                          / oversamplingFactor / sampleRate * 1000.0;

                if (lastAttackMs > 0.0f && lastEventMs < static_cast<double> (lastAttackMs))
                {
                    currentGain[ch] = lookaheadMinGain + (currentGain[ch] - lookaheadMinGain) * fastReleaseCoeff;
                    smoothReleaseStage[ch] = currentGain[ch];
                }
                else
                {
                    switch (releaseCurve)
                    {
                        case ReleaseCurve::linear:
                            currentGain[ch] = juce::jmin (lookaheadMinGain, currentGain[ch] + linearStepPerSample);
                            smoothReleaseStage[ch] = currentGain[ch];
                            break;

                        case ReleaseCurve::smooth:
                            // Two cascaded one-pole stages (critically-
                            // damped): softer, overshoot-free onset than a
                            // single pole, at the cost of a slower perceived
                            // release.
                            smoothReleaseStage[ch] = lookaheadMinGain + (smoothReleaseStage[ch] - lookaheadMinGain) * releaseCoeff;
                            currentGain[ch] = smoothReleaseStage[ch] + (currentGain[ch] - smoothReleaseStage[ch]) * releaseCoeff;
                            break;

                        case ReleaseCurve::exponential:
                        default:
                            currentGain[ch] = lookaheadMinGain + (currentGain[ch] - lookaheadMinGain) * releaseCoeff;
                            smoothReleaseStage[ch] = currentGain[ch];
                            break;
                    }
                }
            }

            blockMinGain = juce::jmin (blockMinGain, currentGain[ch]);

            auto* channelData = osBlock.getChannelPointer (channel);
            const auto incoming = channelData[i];

            const auto delayed = delayPushAndRead (ch, incoming);

            const auto limiterSample = delayed * currentGain[ch];
            auto outSample = limiterSample;

            if (clipMixAmount > 0.0f)
            {
                // Alternate "clipper" path: a tanh soft-clip normalised to
                // the same (headroom-adjusted) target, applied directly to
                // the lookahead-delayed signal - independent of the gain-
                // reduction envelope. Blended in on top of the transparent
                // limiter path for extra perceived loudness/character.
                const auto clipped = clipTargetLinear
                                      * std::tanh (delayed / juce::jmax (clipTargetLinear, 1.0e-8f));
                outSample = limiterSample + (clipped - limiterSample) * clipMixAmount;
            }

            // Final safety backstop: the never-exceed-ceiling guarantee does
            // not rely solely on the smoothed gain envelope (or the clip-mix
            // blend) above - this clamp is unconditional.
            outSample = juce::jlimit (-ceilingLinear, ceilingLinear, outSample);
            channelData[i] = outSample;

            blockPeakLinear = juce::jmax (blockPeakLinear, std::abs (outSample));
        }

        delayAdvance();
        ++slidingSampleCounter;
    }

    oversampler->processSamplesDown (block);

    gainReductionDbAtomic.store (juce::Decibels::gainToDecibels (blockMinGain, -100.0f), std::memory_order_relaxed);
    outputTruePeakDbAtomic.store (juce::Decibels::gainToDecibels (blockPeakLinear, -100.0f), std::memory_order_relaxed);

    // Auto Release (v0.2.0): update the slow-moving gain-reduction-depth
    // average for the NEXT chunk, from THIS chunk's own measured depth
    // (blockMinGain, just computed above) - deliberately not used to
    // compute this same chunk's own effectiveReleaseMs above, keeping the
    // update strictly causal/one-chunk-delayed like every other smoothed
    // parameter in this engine (ceilingSmoothed, clipMixSmoothed, etc.).
    {
        const auto chunkDurationSeconds = static_cast<double> (numSamples) / sampleRate;
        const auto avgCoeff = std::exp (-chunkDurationSeconds / autoReleaseTimeConstantSeconds);
        const auto instantaneousDepthDb = juce::jmax (0.0f, -juce::Decibels::gainToDecibels (blockMinGain, -100.0f));
        autoReleaseDepthAvgDb = static_cast<double> (instantaneousDepthDb)
                                 + (autoReleaseDepthAvgDb - static_cast<double> (instantaneousDepthDb)) * avgCoeff;
    }

    //==================================================================
    // LUFS metering (K-weighted, base rate, post-downsample - i.e. the
    // actual output signal). See docs/architecture.md for the documented
    // simplifications versus the full ITU-R BS.1770-4 two-pass relative-
    // gated Integrated Loudness algorithm.
    //==================================================================
    double latestMomentaryMeanPower = 0.0;
    double latestShortTermMeanPower = 0.0;
    double blockWeightedPowerSum = 0.0;

    for (size_t i = 0; i < numSamples; ++i)
    {
        double weightedPower = 0.0;

        for (size_t channel = 0; channel < numChannels; ++channel)
        {
            const auto chIndex = static_cast<int> (juce::jmin (channel, static_cast<size_t> (1)));
            auto* data = block.getChannelPointer (channel);

            const auto weighted = kWeightHighPass[chIndex].processSample (kWeightShelf[chIndex].processSample (data[i]));
            weightedPower += static_cast<double> (weighted) * static_cast<double> (weighted);
        }

        latestMomentaryMeanPower = momentaryWindow.pushAndGetMeanPower (static_cast<float> (weightedPower));
        latestShortTermMeanPower = shortTermWindow.pushAndGetMeanPower (static_cast<float> (weightedPower));
        blockWeightedPowerSum += weightedPower;
    }

    const auto momentaryDb = latestMomentaryMeanPower > 0.0
                                  ? static_cast<float> (-0.691 + 10.0 * std::log10 (latestMomentaryMeanPower))
                                  : -100.0f;
    const auto shortTermDb = latestShortTermMeanPower > 0.0
                                  ? static_cast<float> (-0.691 + 10.0 * std::log10 (latestShortTermMeanPower))
                                  : -100.0f;

    momentaryLufsAtomic.store (momentaryDb, std::memory_order_relaxed);
    shortTermLufsAtomic.store (shortTermDb, std::memory_order_relaxed);

    // Absolute-gated (only) Integrated Loudness accumulation - see the
    // integratedGateLufs comment in the header for the documented
    // deviation from the full spec's relative gate / 400ms gating blocks.
    if (momentaryDb > integratedGateLufs)
    {
        integratedPowerSum += blockWeightedPowerSum;
        integratedSampleCount += static_cast<juce::int64> (numSamples);
    }

    const auto integratedMeanPower = integratedSampleCount > 0
                                          ? integratedPowerSum / static_cast<double> (integratedSampleCount)
                                          : 0.0;
    const auto integratedDb = integratedMeanPower > 0.0
                                   ? static_cast<float> (-0.691 + 10.0 * std::log10 (integratedMeanPower))
                                   : -100.0f;
    integratedLufsAtomic.store (integratedDb, std::memory_order_relaxed);

    //==================================================================
    // Dither: TPDF noise added at the very end, after downsampling, at the
    // output word length - the conventional placement. Off by default
    // (bit-identical to the pre-dither signal path). Added after the
    // ceiling backstop was already applied above (in the oversampled
    // domain), so this operates purely at the base rate on the final
    // output samples. Its amplitude (<= 1 LSB at 16/24-bit) is far below
    // the true-peak measurement tolerance used by this project's own
    // truepeak tests, but it can still nudge a sample that is already
    // sitting exactly at the oversampled-domain clamp's ceiling up to ~1
    // LSB past the nominal Ceiling at the base rate - see issue #9. Re-
    // clamping to the same ceilingLinear used by that earlier clamp keeps
    // the "never exceeds Ceiling" guarantee true after dither too, at
    // negligible cost (this loop already visits every sample).
    //
    // Dither Shape (v0.2.0): when DitherShape::shaped, the raw per-sample
    // TPDF draw is run through a simple fixed first-order differencing
    // filter (this sample's draw minus the previous one, per channel,
    // halved to keep the injected amplitude in the same ballpark as plain
    // TPDF) before being scaled by ditherLsb - a from-scratch, project-
    // owned noise-shaping design (not a copy of any vendor's shaping
    // curve), which measurably redistributes quantisation-noise energy
    // toward higher frequencies (see tests/StereoLinkDitherShapeTests.cpp's
    // spectral-tilt assertion). DitherShape::flat (the default) uses the
    // raw draw directly and is therefore bit-identical to v1's plain-TPDF
    // dither at every setting.
    //==================================================================
    if (ditherMode != DitherMode::off)
    {
        const auto ditherLsb = (ditherMode == DitherMode::bit16) ? std::exp2 (-15.0f) : std::exp2 (-23.0f);

        for (size_t channel = 0; channel < numChannels; ++channel)
        {
            const auto ch = static_cast<int> (channel);
            auto* data = block.getChannelPointer (channel);

            for (size_t sample = 0; sample < numSamples; ++sample)
            {
                const auto tpdf = ditherRng.nextFloat() - ditherRng.nextFloat();

                auto shapingSample = tpdf;

                if (ditherShape == DitherShape::shaped)
                    shapingSample = (tpdf - previousDitherTpdf[ch]) * 0.5f;

                previousDitherTpdf[ch] = tpdf;

                const auto dithered = data[sample] + shapingSample * ditherLsb;
                data[sample] = juce::jlimit (-ceilingLinear, ceilingLinear, dithered);
            }
        }
    }
}
