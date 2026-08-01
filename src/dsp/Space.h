#pragma once
#include "Helpers.h"

namespace vf
{

// Reverb + tempo-synced delay, both run as "sends" (added to the dry signal).
struct SpaceSection
{
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        reverb.prepare (spec);

        const int maxDelaySamples = (int) (sampleRate * 2.5) + 1;
        delayLine.setMaximumDelayInSamples (maxDelaySamples);
        delayLine.prepare (spec);

        dampFilter.prepare (spec);
        dampFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        dampFilter.setCutoffFrequency (4500.0f);

        sendBuf.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize);
        reset();
    }

    void setParams (float reverbMix01, float reverbSize01, float delayMix01, int delaySyncChoice)
    {
        rvbMix = reverbMix01;
        dlyMix = delayMix01;
        syncChoice = delaySyncChoice;

        juce::dsp::Reverb::Parameters p;
        p.roomSize   = 0.25f + reverbSize01 * 0.65f;
        p.damping    = 0.45f;
        p.wetLevel   = 1.0f;
        p.dryLevel   = 0.0f;
        p.width      = 1.0f;
        p.freezeMode = 0.0f;
        reverb.setParameters (p);
    }

    void process (juce::AudioBuffer<float>& buffer, double bpm)
    {
        const int numCh = buffer.getNumChannels();
        const int n     = buffer.getNumSamples();

        // ---- Delay send (before reverb so echoes get reverb tail too) ----
        if (dlyMix > 0.001f)
        {
            static const float beats[] = { 0.5f, 0.75f, 1.0f }; // 1/8, dotted 1/8, 1/4
            const float delaySec = beats[juce::jlimit (0, 2, syncChoice)] * (float) (60.0 / bpm);
            const float delaySamples = juce::jlimit (1.0f, (float) delayLine.getMaximumDelayInSamples() - 1.0f,
                                                     delaySec * (float) sampleRate);

            const float send = dlyMix * 0.9f;
            const float feedback = 0.35f;

            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* d = buffer.getWritePointer (ch);
                for (int i = 0; i < n; ++i)
                {
                    float echo = delayLine.popSample (ch, delaySamples);
                    echo = dampFilter.processSample (ch, echo);
                    delayLine.pushSample (ch, d[i] + echo * feedback);
                    d[i] += echo * send;
                }
            }
        }

        // ---- Reverb send ----
        if (rvbMix > 0.001f)
        {
            sendBuf.setSize (numCh, n, false, false, true);
            for (int ch = 0; ch < numCh; ++ch)
                sendBuf.copyFrom (ch, 0, buffer, ch, 0, n);

            juce::dsp::AudioBlock<float> block (sendBuf.getArrayOfWritePointers(), (size_t) numCh, (size_t) n);
            juce::dsp::ProcessContextReplacing<float> ctx (block);
            reverb.process (ctx);

            for (int ch = 0; ch < numCh; ++ch)
                buffer.addFrom (ch, 0, sendBuf, ch, 0, n, rvbMix * 0.8f);
        }
    }

    void reset()
    {
        reverb.reset();
        delayLine.reset();
        dampFilter.reset();
    }

    double sampleRate = 44100.0;
    juce::dsp::Reverb reverb;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine { 220500 };
    juce::dsp::StateVariableTPTFilter<float> dampFilter;
    juce::AudioBuffer<float> sendBuf;
    float rvbMix = 0.15f, dlyMix = 0.0f;
    int syncChoice = 1;
};

} // namespace vf
