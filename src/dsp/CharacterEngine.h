#pragma once
#include "Helpers.h"
#include "signalsmith-stretch/signalsmith-stretch.h"

namespace vf
{

// Fixed-length delay used to time-align the dry signal with the pitch shifter's latency.
struct FixedDelay
{
    void prepare (int numChannels, int delaySamples)
    {
        delay = juce::jmax (0, delaySamples);
        lines.assign ((size_t) numChannels, std::vector<float> ((size_t) delay + 1, 0.0f));
        idx.assign ((size_t) numChannels, 0);
    }

    void processChannel (int ch, float* data, int n)
    {
        if (delay == 0) return;
        auto& line = lines[(size_t) ch];
        auto& w    = idx[(size_t) ch];
        const int len = (int) line.size();
        for (int i = 0; i < n; ++i)
        {
            const float in = data[i];
            int r = w - delay; if (r < 0) r += len;
            data[i] = line[(size_t) r];
            line[(size_t) w] = in;
            if (++w >= len) w = 0;
        }
    }

    void reset()
    {
        for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f);
        std::fill (idx.begin(), idx.end(), 0);
    }

    int delay = 0;
    std::vector<std::vector<float>> lines;
    std::vector<int> idx;
};

// The vocal character section. Modes:
//   0 Natural | 1 Deep | 2 Robot | 3 Harmony | 4 EDM | 5 Dubstep
struct CharacterEngine
{
    enum Mode { Natural = 0, Deep, Robot, Harmony, EDM, Dubstep };

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        numChannels = (int) spec.numChannels;
        maxBlock = (int) spec.maximumBlockSize;

        stretchA.presetCheaper (numChannels, (float) sampleRate);
        stretchB.presetCheaper (numChannels, (float) sampleRate);
        latencySamples = stretchA.inputLatency() + stretchA.outputLatency();

        dryDelay.prepare (numChannels, latencySamples);

        dryBuf.setSize (numChannels, maxBlock);
        wetBuf.setSize (numChannels, maxBlock);
        v2Buf.setSize (numChannels, maxBlock);

        // Robot voicing
        robotBand.prepare (spec);
        robotBand.setType (juce::dsp::StateVariableTPTFilterType::bandpass);
        robotBand.setCutoffFrequency (1200.0f);
        robotBand.setResonance (0.4f);

        // Dubstep wobble
        wobbleFilter.prepare (spec);
        wobbleFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        wobbleFilter.setResonance (2.2f);

        // EDM extras
        chorus.prepare (spec);
        chorus.setRate (0.6f); chorus.setDepth (0.25f); chorus.setCentreDelay (8.0f);
        chorus.setFeedback (0.0f); chorus.setMix (0.4f);

        edmLow.prepare (spec);  edmLow.setType  (juce::dsp::LinkwitzRileyFilterType::lowpass);  edmLow.setCutoffFrequency (200.0f);
        edmMidLo.prepare (spec); edmMidLo.setType (juce::dsp::LinkwitzRileyFilterType::highpass); edmMidLo.setCutoffFrequency (200.0f);
        edmMidHi.prepare (spec); edmMidHi.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);  edmMidHi.setCutoffFrequency (2400.0f);
        edmHigh.prepare (spec); edmHigh.setType (juce::dsp::LinkwitzRileyFilterType::highpass); edmHigh.setCutoffFrequency (2400.0f);
        for (auto* e : { &edmEnvLow, &edmEnvMid, &edmEnvHigh })
            e->prepare (sampleRate, 5.0f, 150.0f);

        bandLowBuf.setSize (numChannels, maxBlock);
        bandMidBuf.setSize (numChannels, maxBlock);
        bandHighBuf.setSize (numChannels, maxBlock);

        amountSmoothed.reset (sampleRate, 0.02);
        amountSmoothed.setCurrentAndTargetValue (1.0f);

        inPtrs.resize ((size_t) numChannels);
        outPtrs.resize ((size_t) numChannels);
        reset();
    }

    int getLatencySamples() const { return latencySamples; }

    void setParams (int modeIn, float amount01, float tuneSemis, float color01)
    {
        if (modeIn != mode)
        {
            mode = modeIn;
            needsReset = true;
        }
        amountSmoothed.setTargetValue (amount01);
        tune = tuneSemis;
        color = color01;
    }

    void process (juce::AudioBuffer<float>& buffer, double bpm)
    {
        const int n = buffer.getNumSamples();
        const int numCh = juce::jmin (numChannels, buffer.getNumChannels());

        if (needsReset)
        {
            softReset();
            needsReset = false;
        }

        // Keep a copy of the raw input for the pitch shifter, then delay the in-buffer
        // copy so "dry" lines up with the shifter's latency.
        dryBuf.setSize (numCh, n, false, false, true);
        for (int ch = 0; ch < numCh; ++ch)
            dryBuf.copyFrom (ch, 0, buffer, ch, 0, n);

        for (int ch = 0; ch < numCh; ++ch)
            dryDelay.processChannel (ch, buffer.getWritePointer (ch), n);
        // From here: `buffer` = time-aligned dry, `dryBuf` = raw (early) input.

        if (mode == Natural)
        {
            amountSmoothed.skip (n);
            return; // aligned dry only — character section idle
        }

        wetBuf.setSize (numCh, n, false, false, true);
        wetBuf.clear();

        switch (mode)
        {
            case Deep:    renderDeep (numCh, n);          break;
            case Robot:   renderRobot (buffer, numCh, n); break;
            case Harmony: renderHarmony (buffer, numCh, n); break;
            case EDM:     renderEDM (numCh, n);           break;
            case Dubstep: renderDubstep (buffer, numCh, n, bpm); break;
            default: break;
        }

        // Equal-ish power dry/wet blend.
        for (int i = 0; i < n; ++i)
        {
            const float amt = amountSmoothed.getNextValue();
            const float dryG = std::cos (amt * juce::MathConstants<float>::halfPi);
            const float wetG = std::sin (amt * juce::MathConstants<float>::halfPi);
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* d = buffer.getWritePointer (ch);
                d[i] = d[i] * dryG + wetBuf.getSample (ch, i) * wetG;
            }
        }
    }

    void reset()
    {
        stretchA.reset();
        stretchB.reset();
        softReset();
    }

private:
    void softReset()
    {
        dryDelay.reset();
        robotBand.reset();
        wobbleFilter.reset();
        chorus.reset();
        edmLow.reset(); edmMidLo.reset(); edmMidHi.reset(); edmHigh.reset();
        edmEnvLow.reset(); edmEnvMid.reset(); edmEnvHigh.reset();
        ringPhase = 0.0f; lfoPhase = 0.0f; crushCounter = 0;
        crushHeld.assign ((size_t) numChannels, 0.0f);
    }

    void runStretch (signalsmith::stretch::SignalsmithStretch<float>& s,
                     float pitchSemis, float formantSemis,
                     juce::AudioBuffer<float>& dest, int numCh, int n)
    {
        s.setTransposeSemitones (pitchSemis, 8000.0f / (float) sampleRate);
        s.setFormantSemitones (formantSemis, false);
        s.setFormantBase (0.0f);

        for (int ch = 0; ch < numCh; ++ch)
        {
            inPtrs[(size_t) ch]  = dryBuf.getReadPointer (ch);
            outPtrs[(size_t) ch] = dest.getWritePointer (ch);
        }
        s.process (inPtrs.data(), n, outPtrs.data(), n);
    }

    void renderDeep (int numCh, int n)
    {
        // Knob at 0 already gives the classic movie-trailer drop.
        const float pitch   = -5.0f + tune * 0.5f;
        const float formant = -1.5f - (color / 100.0f) * 4.5f;
        runStretch (stretchA, pitch, formant, wetBuf, numCh, n);
    }

    void renderHarmony (juce::AudioBuffer<float>& alignedDry, int numCh, int n)
    {
        // Two harmony voices above the lead (default: major 3rd + 5th), tucked under the dry.
        v2Buf.setSize (numCh, n, false, false, true);
        runStretch (stretchA, tune + 4.0f, 0.0f, wetBuf, numCh, n);
        runStretch (stretchB, tune + 7.0f, 0.0f, v2Buf, numCh, n);

        const float voiceLevel = 0.35f + (color / 100.0f) * 0.45f;
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* w = wetBuf.getWritePointer (ch);
            const auto* v2 = v2Buf.getReadPointer (ch);
            const auto* dry = alignedDry.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
                w[i] = dry[i] + (w[i] + v2[i]) * voiceLevel;
        }
    }

    void renderRobot (juce::AudioBuffer<float>& alignedDry, int numCh, int n)
    {
        // Ring mod + bit crush + telephone-ish bandpass. Zero-latency source = aligned dry.
        const float ringHz = 60.0f * std::pow (2.0f, tune / 12.0f);
        const float phaseInc = ringHz / (float) sampleRate;

        const float crush01 = color / 100.0f;
        const int   holdSamples = 1 + (int) (crush01 * 7.0f);           // sample-rate reduction
        const float levels = std::pow (2.0f, juce::jmap (crush01, 12.0f, 5.0f)); // bit depth 12 -> 5

        if ((int) crushHeld.size() < numCh) crushHeld.assign ((size_t) numCh, 0.0f);

        for (int i = 0; i < n; ++i)
        {
            const float ring = std::sin (ringPhase * juce::MathConstants<float>::twoPi);
            ringPhase += phaseInc; if (ringPhase >= 1.0f) ringPhase -= 1.0f;

            const bool holdNew = (crushCounter == 0);
            if (++crushCounter >= holdSamples) crushCounter = 0;

            for (int ch = 0; ch < numCh; ++ch)
            {
                float x = alignedDry.getSample (ch, i);
                x = x * (0.35f + 0.65f * ring);                       // ring mod (kept partly dry so words survive)
                if (holdNew) crushHeld[(size_t) ch] = std::round (x * levels) / levels;
                wetBuf.getWritePointer (ch)[i] = crushHeld[(size_t) ch];
            }
        }

        juce::dsp::AudioBlock<float> block (wetBuf.getArrayOfWritePointers(), (size_t) numCh, (size_t) n);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        robotBand.process (ctx);
        wetBuf.applyGain (2.2f); // bandpass level makeup
    }

    void renderEDM (int numCh, int n)
    {
        // Pitch/formant lift -> 3-band OTT-style squash -> chorus width -> bright shelf feel.
        const float formant = 0.5f + (color / 100.0f) * 3.0f;
        runStretch (stretchA, tune, formant, wetBuf, numCh, n);

        // Split into 3 bands
        bandLowBuf.setSize (numCh, n, false, false, true);
        bandMidBuf.setSize (numCh, n, false, false, true);
        bandHighBuf.setSize (numCh, n, false, false, true);
        for (int ch = 0; ch < numCh; ++ch)
        {
            bandLowBuf.copyFrom (ch, 0, wetBuf, ch, 0, n);
            bandMidBuf.copyFrom (ch, 0, wetBuf, ch, 0, n);
            bandHighBuf.copyFrom (ch, 0, wetBuf, ch, 0, n);
        }

        auto runFilter = [numCh, n] (juce::dsp::LinkwitzRileyFilter<float>& f, juce::AudioBuffer<float>& b)
        {
            juce::dsp::AudioBlock<float> blk (b.getArrayOfWritePointers(), (size_t) numCh, (size_t) n);
            juce::dsp::ProcessContextReplacing<float> c (blk);
            f.process (c);
        };
        runFilter (edmLow, bandLowBuf);
        runFilter (edmMidLo, bandMidBuf);
        runFilter (edmMidHi, bandMidBuf);
        runFilter (edmHigh, bandHighBuf);

        // OTT feel: drag each band toward a target loudness (up AND down).
        const float depth = 0.55f;
        auto ottBand = [&] (juce::AudioBuffer<float>& b, EnvelopeFollower& env, float targetDb)
        {
            for (int i = 0; i < n; ++i)
            {
                float link = 0.0f;
                for (int ch = 0; ch < numCh; ++ch)
                    link = juce::jmax (link, std::abs (b.getSample (ch, i)));
                const float envDb = gainToDb (env.process (link));
                const float gDb   = juce::jlimit (-14.0f, 14.0f, (targetDb - envDb) * depth);
                const float g     = dbToGain (gDb);
                for (int ch = 0; ch < numCh; ++ch)
                    b.getWritePointer (ch)[i] *= g;
            }
        };
        ottBand (bandLowBuf,  edmEnvLow,  -20.0f);
        ottBand (bandMidBuf,  edmEnvMid,  -16.0f);
        ottBand (bandHighBuf, edmEnvHigh, -18.0f);

        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* w = wetBuf.getWritePointer (ch);
            const auto* lo = bandLowBuf.getReadPointer (ch);
            const auto* mi = bandMidBuf.getReadPointer (ch);
            const auto* hi = bandHighBuf.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
                w[i] = (lo[i] + mi[i] + hi[i] * 1.25f) * 0.8f;
        }

        juce::dsp::AudioBlock<float> blk (wetBuf.getArrayOfWritePointers(), (size_t) numCh, (size_t) n);
        juce::dsp::ProcessContextReplacing<float> c (blk);
        chorus.process (c);
    }

    void renderDubstep (juce::AudioBuffer<float>& alignedDry, int numCh, int n, double bpm)
    {
        // Tempo-synced resonant wobble ("wub") + drive. Color picks the rate, Tune moves the growl center.
        static const float rateMultipliers[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f }; // whole, half, 1/4, 1/8, 1/16 (in beats)
        const int rateIdx = juce::jlimit (0, 4, (int) std::floor ((color / 100.0f) * 4.999f));
        const float beatsPerSec = (float) (bpm / 60.0);
        const float lfoHz = beatsPerSec * rateMultipliers[rateIdx];
        const float phaseInc = lfoHz / (float) sampleRate;

        const float centerHz = 700.0f * std::pow (2.0f, tune / 12.0f);

        for (int i = 0; i < n; ++i)
        {
            const float lfo = std::sin (lfoPhase * juce::MathConstants<float>::twoPi);
            lfoPhase += phaseInc; if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;

            const float cutoff = juce::jlimit (80.0f, 9000.0f, centerHz * std::pow (2.0f, lfo * 2.2f));
            wobbleFilter.setCutoffFrequency (cutoff);

            for (int ch = 0; ch < numCh; ++ch)
            {
                float x = alignedDry.getSample (ch, i);
                x = wobbleFilter.processSample (ch, x);
                x = std::tanh (x * 2.5f) * 0.9f; // growl drive
                wetBuf.getWritePointer (ch)[i] = x;
            }
        }
    }

    double sampleRate = 44100.0;
    int numChannels = 2, maxBlock = 512, latencySamples = 0;
    int mode = Natural;
    bool needsReset = false;
    float tune = 0.0f, color = 50.0f;

    signalsmith::stretch::SignalsmithStretch<float> stretchA, stretchB;
    FixedDelay dryDelay;
    juce::AudioBuffer<float> dryBuf, wetBuf, v2Buf, bandLowBuf, bandMidBuf, bandHighBuf;
    std::vector<const float*> inPtrs;
    std::vector<float*> outPtrs;

    juce::dsp::StateVariableTPTFilter<float> robotBand, wobbleFilter;
    juce::dsp::Chorus<float> chorus;
    juce::dsp::LinkwitzRileyFilter<float> edmLow, edmMidLo, edmMidHi, edmHigh;
    EnvelopeFollower edmEnvLow, edmEnvMid, edmEnvHigh;

    juce::SmoothedValue<float> amountSmoothed;
    float ringPhase = 0.0f, lfoPhase = 0.0f;
    int crushCounter = 0;
    std::vector<float> crushHeld;
};

} // namespace vf
