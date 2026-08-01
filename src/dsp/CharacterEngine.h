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

// A classic channel vocoder (Vocodex-style): the vocal's band envelopes shape a
// synth carrier that follows MIDI notes or the detected vocal pitch.
struct Vocoder
{
    static constexpr int numBands = 24;
    static constexpr int noiseBandStart = 20; // top bands use noise for consonant clarity
    static constexpr int maxVoices = 3;

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        juce::dsp::ProcessSpec monoSpec { spec.sampleRate, spec.maximumBlockSize, 1 };

        for (int k = 0; k < numBands; ++k)
        {
            const float f = 100.0f * std::pow (7800.0f / 100.0f, (float) k / (float) (numBands - 1));
            for (auto* filt : { &bandMod[(size_t) k], &bandCar[(size_t) k] })
            {
                filt->prepare (monoSpec);
                filt->setType (juce::dsp::StateVariableTPTFilterType::bandpass);
                filt->setCutoffFrequency (f);
                filt->setResonance (4.0f);
            }
            env[(size_t) k].prepare (spec.sampleRate, 4.0f, 35.0f);
        }
        carrierLP.prepare (monoSpec);
        carrierLP.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        carrierLP.setResonance (0.5f);
        gateEnv.prepare (spec.sampleRate, 2.0f, 80.0f);
        levelIn.prepare (spec.sampleRate, 30.0f, 150.0f);
        levelOut.prepare (spec.sampleRate, 30.0f, 150.0f);

        // Portamento: ~30 ms glide keeps the carrier from lurching when the pitch detector updates.
        glideAlpha = 1.0f - std::exp (-1.0f / (0.030f * (float) sampleRate));
        phases.fill (0.0f);
        currentFreqs.fill (0.0f);
        reset();
    }

    void setCarrier (const std::vector<float>& freqs, float brightness01)
    {
        if (! freqs.empty())              // hold the last pitch through consonants/gaps
            targetFreqs = freqs;
        carrierLP.setCutoffFrequency (juce::jmap (brightness01, 2500.0f, 9500.0f));
    }

    // modulator: time-aligned dry vocal. dest: wet output (all channels get the same signal).
    void process (const juce::AudioBuffer<float>& modulator, juce::AudioBuffer<float>& dest, int numCh, int n)
    {
        const int voices = juce::jmax (1, juce::jmin (maxVoices, (int) targetFreqs.size()));

        for (int v = 0; v < voices; ++v)
            if (currentFreqs[(size_t) v] <= 0.0f)
                currentFreqs[(size_t) v] = targetFreqs[(size_t) v]; // jump instantly on first note

        for (int i = 0; i < n; ++i)
        {
            // Mono modulator
            float mod = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                mod += modulator.getSample (ch, i);
            mod /= (float) juce::jmax (1, numCh);

            // Overall gate: silence in = silence out (no hiss between words)
            const float g = juce::jlimit (0.0f, 1.0f, (gateEnv.process (mod) - 0.004f) * 120.0f);

            // Carrier: each voice = two slightly detuned saws (richer, Vocodex-ish)
            float car = 0.0f;
            for (int v = 0; v < voices; ++v)
            {
                auto& f = currentFreqs[(size_t) v];
                f += (targetFreqs[(size_t) v] - f) * glideAlpha;

                auto& phA = phases[(size_t) (v * 2)];
                auto& phB = phases[(size_t) (v * 2 + 1)];
                phA += f * 0.9965f / (float) sampleRate; if (phA >= 1.0f) phA -= 1.0f;
                phB += f * 1.0035f / (float) sampleRate; if (phB >= 1.0f) phB -= 1.0f;
                car += (2.0f * phA - 1.0f) + (2.0f * phB - 1.0f);
            }
            car /= (float) (voices * 2);
            car = carrierLP.processSample (0, car);

            const float noise = rng.nextFloat() * 2.0f - 1.0f;

            float out = 0.0f;
            for (int k = 0; k < numBands; ++k)
            {
                const float m = bandMod[(size_t) k].processSample (0, mod);
                const float e = env[(size_t) k].process (m);
                const float source = k >= noiseBandStart ? noise * 0.7f : car;
                const float s = bandCar[(size_t) k].processSample (0, source);
                out += s * e;
            }
            // Level-match to the vocal so the vocoder follows its dynamics instead of railing.
            const float eIn  = levelIn.process (mod);
            const float eOut = levelOut.process (out);
            out *= juce::jlimit (0.05f, 10.0f, eIn / juce::jmax (eOut, 1.0e-5f));
            out = std::tanh (out * 1.3f) * 0.9f * g; // soft safety + gate

            for (int ch = 0; ch < numCh; ++ch)
                dest.getWritePointer (ch)[i] = out;
        }
    }

    void reset()
    {
        for (int k = 0; k < numBands; ++k)
        {
            bandMod[(size_t) k].reset();
            bandCar[(size_t) k].reset();
            env[(size_t) k].reset();
        }
        carrierLP.reset();
        gateEnv.reset();
        levelIn.reset();
        levelOut.reset();
        phases.fill (0.0f);
        currentFreqs.fill (0.0f);
    }

    double sampleRate = 44100.0;
    std::array<juce::dsp::StateVariableTPTFilter<float>, numBands> bandMod, bandCar;
    std::array<EnvelopeFollower, numBands> env;
    juce::dsp::StateVariableTPTFilter<float> carrierLP;
    EnvelopeFollower gateEnv, levelIn, levelOut;
    std::array<float, maxVoices * 2> phases {};
    std::array<float, maxVoices> currentFreqs {};
    std::vector<float> targetFreqs { 110.0f };
    float glideAlpha = 0.01f;
    juce::Random rng;
};

// The vocal character section. Modes:
//   0 Natural | 1 Deep | 2 Robot (vocoder) | 3 Harmony | 4 EDM | 5 Dubstep (grit)
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

        // Grit tone filter (formerly the dubstep wobble)
        wobbleFilter.prepare (spec);
        wobbleFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        wobbleFilter.setResonance (0.6f);

        // Vocoder
        vocoder.prepare (spec);

        // Grit level-matching envelopes
        gritEnvIn.prepare (sampleRate, 20.0f, 200.0f);
        gritEnvOut.prepare (sampleRate, 20.0f, 200.0f);

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

    // Carrier pitches for the vocoder (Hz) — held MIDI notes or the detected vocal pitch.
    void setCarrierHz (const std::vector<float>& freqs) { carrierHz = freqs; }

    void setHarmonyOverride (bool active, float semis1, float semis2)
    {
        harmOverride = active;
        harmS1 = juce::jlimit (-24.0f, 24.0f, semis1);
        harmS2 = juce::jlimit (-24.0f, 24.0f, semis2);
    }

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
        vocoder.reset();
        gritEnvIn.reset();
        gritEnvOut.reset();
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
        // With MIDI follow active, the offsets come from the held chord instead.
        const float s1 = harmOverride ? harmS1 : tune + 4.0f;
        const float s2 = harmOverride ? harmS2 : tune + 7.0f;
        v2Buf.setSize (numCh, n, false, false, true);
        runStretch (stretchA, s1, 0.0f, wetBuf, numCh, n);
        runStretch (stretchB, s2, 0.0f, v2Buf, numCh, n);

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
        // True channel vocoder (Vocodex-style). The carrier follows held MIDI notes or the
        // vocal's own detected pitch; TUNE transposes it, COLOR sets carrier brightness.
        // Empty carrier list = keep the last pitch (the vocoder holds it through consonants).
        tmpCarrier = carrierHz;
        const float shift = std::pow (2.0f, tune / 12.0f);
        for (auto& f : tmpCarrier)
            f = juce::jlimit (40.0f, 1500.0f, f * shift);

        vocoder.setCarrier (tmpCarrier, color / 100.0f);
        vocoder.process (alignedDry, wetBuf, numCh, n);
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
        // Distorted vocal grit (no pulsing). COLOR = drive amount, TUNE = tone (dark..bright).
        // Presence emphasis into an asymmetric clipper so the character is clearly audible.
        juce::ignoreUnused (bpm);

        const float c01 = color / 100.0f;
        const float drive = 1.5f + c01 * c01 * 15.0f; // light at low COLOR, aggressive at the top
        const float toneHz = juce::jlimit (1500.0f, 9500.0f, 4500.0f * std::pow (2.0f, tune / 12.0f));
        wobbleFilter.setCutoffFrequency (toneHz);
        wobbleFilter.setResonance (0.6f);
        robotBand.setCutoffFrequency (2200.0f); // presence band pushed into the clipper
        robotBand.setResonance (0.9f);

        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float x = alignedDry.getSample (ch, i);
                const float presence = robotBand.processSample (ch, x);
                float y = std::tanh ((x + 0.2f * x * x + 0.5f * presence) * drive);
                y = wobbleFilter.processSample (ch, y);

                // Auto level-match so grit changes the tone, not the volume.
                const float inEnv  = gritEnvIn.process (x);
                const float outEnv = gritEnvOut.process (y);
                const float match  = juce::jlimit (0.3f, 3.0f, inEnv / juce::jmax (outEnv, 1.0e-5f));
                wetBuf.getWritePointer (ch)[i] = y * match;
            }
        }
    }

    double sampleRate = 44100.0;
    int numChannels = 2, maxBlock = 512, latencySamples = 0;
    int mode = Natural;
    bool needsReset = false;
    float tune = 0.0f, color = 50.0f;
    bool harmOverride = false;
    float harmS1 = 4.0f, harmS2 = 7.0f;
    Vocoder vocoder;
    std::vector<float> carrierHz { 110.0f }, tmpCarrier;
    EnvelopeFollower gritEnvIn, gritEnvOut;

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
