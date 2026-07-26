#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <cmath>

// Small shared helpers used across the Tests target. Kept dependency-light
// (juce_audio_basics + juce_dsp) so it can be included from any test file.
namespace TestHelpers
{
    // Fills every channel of the buffer with a sine wave of the given
    // frequency. `startSampleIndex` offsets the phase calculation, so
    // calling this for consecutive blocks with startSampleIndex incremented
    // by each block's length produces a phase-continuous sine across block
    // boundaries.
    inline void fillWithSine (juce::AudioBuffer<float>& buffer,
                              double sampleRate,
                              double frequencyHz,
                              float amplitude = 0.5f,
                              juce::int64 startSampleIndex = 0)
    {
        const auto numChannels = buffer.getNumChannels();
        const auto numSamples = buffer.getNumSamples();

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto* data = buffer.getWritePointer (channel);

            for (int sample = 0; sample < numSamples; ++sample)
            {
                const auto phase = juce::MathConstants<double>::twoPi * frequencyHz
                                    * static_cast<double> (startSampleIndex + sample) / sampleRate;
                data[sample] = amplitude * static_cast<float> (std::sin (phase));
            }
        }
    }

    // Root-mean-square level across all channels/samples in the buffer.
    inline double rms (const juce::AudioBuffer<float>& buffer)
    {
        double sumOfSquares = 0.0;
        juce::int64 numValues = 0;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            {
                const auto value = static_cast<double> (data[sample]);
                sumOfSquares += value * value;
                ++numValues;
            }
        }

        return numValues > 0 ? std::sqrt (sumOfSquares / static_cast<double> (numValues)) : 0.0;
    }

    // Largest absolute sample value across all channels/samples.
    inline float peakAbsolute (const juce::AudioBuffer<float>& buffer)
    {
        float peak = 0.0f;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                peak = std::max (peak, std::abs (data[sample]));
        }

        return peak;
    }

    // Returns true if every sample in the buffer is finite (no NaN/Inf).
    inline bool allSamplesFinite (const juce::AudioBuffer<float>& buffer)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* data = buffer.getReadPointer (channel);

            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                if (! std::isfinite (data[sample]))
                    return false;
        }

        return true;
    }

    // Synchronous (projection-based) amplitude of the `harmonic`-th
    // multiple of `fundamentalHz` over [startSample, startSample +
    // numSamples). Leakage-free when numSamples spans an integer number of
    // fundamental periods - the caller is responsible for choosing such a
    // window. (v0.4.0: used by the style-engine THD assertions.)
    inline double harmonicAmplitude (const juce::AudioBuffer<float>& buffer,
                                     int channel,
                                     int startSample,
                                     int numSamples,
                                     double sampleRate,
                                     double fundamentalHz,
                                     int harmonic)
    {
        const auto* data = buffer.getReadPointer (channel, startSample);
        const auto omega = juce::MathConstants<double>::twoPi * fundamentalHz * static_cast<double> (harmonic) / sampleRate;

        double inPhase = 0.0;
        double quadrature = 0.0;

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto value = static_cast<double> (data[sample]);
            inPhase += value * std::cos (omega * sample);
            quadrature += value * std::sin (omega * sample);
        }

        const auto scale = 2.0 / static_cast<double> (numSamples);
        return scale * std::sqrt (inPhase * inPhase + quadrature * quadrature);
    }

    // Total harmonic distortion (ratio, not percent): RMS of harmonics
    // 2..maxHarmonic over the fundamental's amplitude, measured
    // synchronously (see harmonicAmplitude above).
    inline double measureThd (const juce::AudioBuffer<float>& buffer,
                              int channel,
                              int startSample,
                              int numSamples,
                              double sampleRate,
                              double fundamentalHz,
                              int maxHarmonic = 20)
    {
        const auto fundamental = harmonicAmplitude (buffer, channel, startSample, numSamples, sampleRate, fundamentalHz, 1);

        if (fundamental <= 0.0)
            return 0.0;

        double harmonicPowerSum = 0.0;

        for (int harmonic = 2; harmonic <= maxHarmonic; ++harmonic)
        {
            if (fundamentalHz * harmonic >= sampleRate / 2.0)
                break;

            const auto amplitude = harmonicAmplitude (buffer, channel, startSample, numSamples, sampleRate, fundamentalHz, harmonic);
            harmonicPowerSum += amplitude * amplitude;
        }

        return std::sqrt (harmonicPowerSum) / fundamental;
    }

    // Independent (from the plugin's own internal engine) estimate of the
    // buffer's true (inter-sample) peak, expressed as a linear amplitude:
    // 4x-oversamples the buffer with a freshly constructed
    // juce::dsp::Oversampling instance and returns the largest absolute
    // sample seen in the upsampled representation. This mirrors the
    // detection technique the DSP spec calls for ("oversampled true-peak of
    // OUTPUT"), though it is not a fully independent algorithm (e.g. a
    // windowed-sinc ITU-R BS.1770-style measurement) - see the plugin
    // engineer's handoff notes for that caveat.
    inline float measureTruePeakLinear (const juce::AudioBuffer<float>& buffer)
    {
        const auto numChannels = buffer.getNumChannels();
        const auto numSamples = buffer.getNumSamples();

        if (numSamples == 0)
            return 0.0f;

        juce::dsp::Oversampling<float> oversampler (
            static_cast<size_t> (numChannels),
            2, // 2^2 = 4x
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
            true,
            true);
        oversampler.initProcessing (static_cast<size_t> (numSamples));
        oversampler.reset();

        juce::AudioBuffer<float> working;
        working.makeCopyOf (buffer);

        juce::dsp::AudioBlock<float> block (working);
        auto osBlock = oversampler.processSamplesUp (block);

        float peak = 0.0f;

        for (size_t channel = 0; channel < osBlock.getNumChannels(); ++channel)
        {
            const auto* data = osBlock.getChannelPointer (channel);

            for (size_t sample = 0; sample < osBlock.getNumSamples(); ++sample)
                peak = std::max (peak, std::abs (data[sample]));
        }

        return peak;
    }
}
