#pragma once
#include <juce_dsp/juce_dsp.h>
#include <cmath>

namespace vf
{

// Simple attack/release envelope follower operating on absolute sample values.
struct EnvelopeFollower
{
    void prepare (double sampleRate, float attackMs, float releaseMs)
    {
        sr = sampleRate;
        setTimes (attackMs, releaseMs);
        env = 0.0f;
    }

    void setTimes (float attackMs, float releaseMs)
    {
        attCoef = std::exp (-1.0f / (float (sr) * attackMs * 0.001f));
        relCoef = std::exp (-1.0f / (float (sr) * releaseMs * 0.001f));
    }

    inline float process (float x)
    {
        const float a = std::abs (x);
        const float coef = a > env ? attCoef : relCoef;
        env = coef * env + (1.0f - coef) * a;
        return env;
    }

    void reset() { env = 0.0f; }

    double sr = 44100.0;
    float attCoef = 0.0f, relCoef = 0.0f, env = 0.0f;
};

inline float dbToGain (float db)   { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g)    { return 20.0f * std::log10 (juce::jmax (g, 1.0e-6f)); }

// Feed-forward compressor with soft knee. Detection is linked across channels.
struct Compressor
{
    void prepare (double sampleRate)
    {
        follower.prepare (sampleRate, 10.0f, 100.0f);
    }

    void setParams (float thresholdDb, float ratioIn, float attackMs, float releaseMs, float makeupDb)
    {
        threshold = thresholdDb;
        ratio     = juce::jmax (1.0f, ratioIn);
        follower.setTimes (attackMs, releaseMs);
        makeup    = dbToGain (makeupDb);
    }

    // In-place on all channels, linked detection.
    void process (juce::AudioBuffer<float>& buffer)
    {
        const int numCh = buffer.getNumChannels();
        const int n     = buffer.getNumSamples();

        for (int i = 0; i < n; ++i)
        {
            float link = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                link = juce::jmax (link, std::abs (buffer.getSample (ch, i)));

            const float envDb = gainToDb (follower.process (link));
            float gr = 0.0f; // gain reduction in dB (negative)
            const float knee = 6.0f;
            const float over = envDb - threshold;

            if (over > knee * 0.5f)
                gr = -(over - over / ratio);
            else if (over > -knee * 0.5f)
            {
                const float x = over + knee * 0.5f;
                gr = -((1.0f - 1.0f / ratio) * x * x) / (2.0f * knee);
            }

            const float g = dbToGain (gr) * makeup;
            for (int ch = 0; ch < numCh; ++ch)
                buffer.getWritePointer (ch)[i] *= g;
        }
    }

    void reset() { follower.reset(); }

    EnvelopeFollower follower;
    float threshold = -18.0f, ratio = 3.0f, makeup = 1.0f;
};

// Downward expander / gate with hysteresis-free simple curve and smoothing.
struct Gate
{
    void prepare (double sampleRate)
    {
        follower.prepare (sampleRate, 1.0f, 80.0f);
        smoothed.reset (sampleRate, 0.01);
        smoothed.setCurrentAndTargetValue (1.0f);
    }

    void setThreshold (float thresholdDb) { threshold = thresholdDb; }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (threshold <= -79.5f) return; // fully off at the bottom of the range

        const int numCh = buffer.getNumChannels();
        const int n     = buffer.getNumSamples();

        for (int i = 0; i < n; ++i)
        {
            float link = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                link = juce::jmax (link, std::abs (buffer.getSample (ch, i)));

            const float envDb = gainToDb (follower.process (link));
            // 12 dB soft range below threshold, then closed.
            float target = 1.0f;
            if (envDb < threshold)
                target = dbToGain (juce::jmax ((envDb - threshold) * 3.0f, -60.0f));

            smoothed.setTargetValue (target);
            const float g = smoothed.getNextValue();
            for (int ch = 0; ch < numCh; ++ch)
                buffer.getWritePointer (ch)[i] *= g;
        }
    }

    void reset() { follower.reset(); smoothed.setCurrentAndTargetValue (1.0f); }

    EnvelopeFollower follower;
    juce::SmoothedValue<float> smoothed;
    float threshold = -80.0f;
};

// Split-band de-esser: compresses only the sibilant band above ~4.5 kHz.
struct DeEsser
{
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        lowPass.prepare (spec);
        highPass.prepare (spec);
        lowPass.setType  (juce::dsp::LinkwitzRileyFilterType::lowpass);
        highPass.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
        lowPass.setCutoffFrequency (4500.0f);
        highPass.setCutoffFrequency (4500.0f);
        follower.prepare (spec.sampleRate, 0.5f, 60.0f);
        highBuf.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize);
    }

    void setAmount (float amount01) { amount = amount01; }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (amount <= 0.001f) return;

        const int numCh = buffer.getNumChannels();
        const int n     = buffer.getNumSamples();

        highBuf.setSize (numCh, n, false, false, true);
        for (int ch = 0; ch < numCh; ++ch)
            highBuf.copyFrom (ch, 0, buffer, ch, 0, n);

        // Split
        {
            juce::dsp::AudioBlock<float> lowBlock (buffer);
            juce::dsp::ProcessContextReplacing<float> lowCtx (lowBlock);
            lowPass.process (lowCtx);

            juce::dsp::AudioBlock<float> highBlock (highBuf);
            juce::dsp::ProcessContextReplacing<float> highCtx (highBlock);
            highPass.process (highCtx);
        }

        // Compress the high band. Amount maps to threshold: more de-ess = lower threshold.
        const float threshold = juce::jmap (amount, 0.0f, 1.0f, -12.0f, -42.0f);
        for (int i = 0; i < n; ++i)
        {
            float link = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                link = juce::jmax (link, std::abs (highBuf.getSample (ch, i)));

            const float envDb = gainToDb (follower.process (link));
            float g = 1.0f;
            if (envDb > threshold)
                g = dbToGain (-(envDb - threshold) * 0.75f); // ~4:1 on the ess band

            for (int ch = 0; ch < numCh; ++ch)
                highBuf.getWritePointer (ch)[i] *= g;
        }

        // Recombine
        for (int ch = 0; ch < numCh; ++ch)
            buffer.addFrom (ch, 0, highBuf, ch, 0, n);
    }

    void reset() { lowPass.reset(); highPass.reset(); follower.reset(); }

    juce::dsp::LinkwitzRileyFilter<float> lowPass, highPass;
    EnvelopeFollower follower;
    juce::AudioBuffer<float> highBuf;
    float amount = 0.3f;
};

// Warm tanh saturation with automatic level compensation.
struct Saturator
{
    void setDrive (float amount01) { amount = amount01; }

    void process (juce::AudioBuffer<float>& buffer)
    {
        if (amount <= 0.001f) return;

        const float driveDb  = amount * 24.0f;              // up to +24 dB into the shaper
        const float pre      = dbToGain (driveDb);
        const float post     = 1.0f / std::tanh (juce::jmax (pre * 0.5f, 0.5f)); // rough level match
        const int numCh = buffer.getNumChannels();
        const int n     = buffer.getNumSamples();

        for (int ch = 0; ch < numCh; ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
                d[i] = std::tanh (d[i] * pre * 0.5f) * post * 0.5f;
        }
    }

    float amount = 0.2f;
};

} // namespace vf
