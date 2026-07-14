#include "TruePeakLimiterEngine.h"

#include <cmath>

TruePeakLimiterEngine::TruePeakLimiterEngine() = default;

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
    slidingValues.assign (static_cast<size_t> (slidingCapacity), 0.0f);
    slidingIndices.assign (static_cast<size_t> (slidingCapacity), 0);

    delayCapacity = juce::jmax (1, lookaheadSamplesOS + 1);
    delayBuffer.setSize (numChannels, delayCapacity, false, true, true);

    ceilingSmoothed.reset (sampleRate, smoothingTimeSeconds);
    ceilingSmoothed.setCurrentAndTargetValue (lastCeilingDb);

    reset();
}

void TruePeakLimiterEngine::reset()
{
    inputGain.reset();

    if (oversampler != nullptr)
        oversampler->reset();

    delayBuffer.clear();
    delayWritePos = 0;

    slidingHead = 0;
    slidingCount = 0;
    slidingSampleCounter = 0;

    currentGain = 1.0f;
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

float TruePeakLimiterEngine::pushSlidingMin (float value) noexcept
{
    const auto ringAt = [this] (int logicalIndex) noexcept
    {
        return ((logicalIndex % slidingCapacity) + slidingCapacity) % slidingCapacity;
    };

    // Pop back while the trailing entry is >= the new value: it can never
    // be the minimum again once a smaller, more recent value has arrived.
    while (slidingCount > 0 && slidingValues[static_cast<size_t> (ringAt (slidingHead + slidingCount - 1))] >= value)
        --slidingCount;

    slidingValues[static_cast<size_t> (ringAt (slidingHead + slidingCount))] = value;
    slidingIndices[static_cast<size_t> (ringAt (slidingHead + slidingCount))] = slidingSampleCounter;
    ++slidingCount;

    // Pop front while it has fallen outside the window.
    while (slidingCount > 0 && slidingIndices[static_cast<size_t> (ringAt (slidingHead))] <= slidingSampleCounter - windowSize)
    {
        slidingHead = ringAt (slidingHead + 1);
        --slidingCount;
    }

    ++slidingSampleCounter;

    return slidingValues[static_cast<size_t> (ringAt (slidingHead))];
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

    const auto releaseMs = juce::jmax (1.0f, lastReleaseMs);
    const auto releaseCoeff = static_cast<float> (
        std::exp (-1.0 / (0.001 * static_cast<double> (releaseMs) * sampleRate * oversamplingFactor)));

    auto osBlock = oversampler->processSamplesUp (block);
    const auto numOSSamples = osBlock.getNumSamples();

    for (size_t i = 0; i < numOSSamples; ++i)
    {
        float peak = 0.0f;

        for (size_t channel = 0; channel < numChannels; ++channel)
            peak = std::max (peak, std::abs (osBlock.getChannelPointer (channel)[i]));

        const auto rawGain = (peak > internalTargetLinear) ? (internalTargetLinear / juce::jmax (peak, 1.0e-8f)) : 1.0f;
        const auto lookaheadMinGain = pushSlidingMin (rawGain);

        // Attack is instantaneous (the lookahead min already accounts for
        // the future peak); release is a one-pole ramp back towards unity.
        if (lookaheadMinGain < currentGain)
            currentGain = lookaheadMinGain;
        else
            currentGain = lookaheadMinGain + (currentGain - lookaheadMinGain) * releaseCoeff;

        for (size_t channel = 0; channel < numChannels; ++channel)
        {
            auto* channelData = osBlock.getChannelPointer (channel);
            const auto incoming = channelData[i];

            const auto delayed = delayPushAndRead (static_cast<int> (channel), incoming);

            auto outSample = delayed * currentGain;
            // Final safety backstop: the never-exceed-ceiling guarantee does
            // not rely solely on the smoothed gain envelope above.
            outSample = juce::jlimit (-ceilingLinear, ceilingLinear, outSample);
            channelData[i] = outSample;
        }

        delayAdvance();
    }

    oversampler->processSamplesDown (block);
}
