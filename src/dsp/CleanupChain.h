#pragma once
#include "Helpers.h"

namespace vf
{

// The full "clean vocal" section:
// gate -> EQ (low cut + surgical cut + harsh cut + air) -> de-esser
//      -> compressor -> saturation -> parallel compression blend
struct CleanupChain
{
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;

        gate.prepare (spec.sampleRate);
        deEsser.prepare (spec);
        comp.prepare (spec.sampleRate);
        parallelComp.prepare (spec.sampleRate);
        parallelComp.setParams (-35.0f, 8.0f, 2.0f, 120.0f, 12.0f); // fixed NY-style smash

        for (auto* f : { &hpf, &cutBand, &harshBand, &airShelf })
            f->prepare (spec);

        parallelBuf.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize);
        updateFilters (true);
    }

    void setParams (float gateThreshDb,
                    float hpfFreqIn, float cutFreqIn, float cutGainIn, float cutQIn,
                    float harshGainIn, float airGainIn,
                    float deEssAmount01,
                    float compThreshDb, float compRatioIn, float compAttackMs, float compReleaseMs, float compMakeupDb,
                    float satAmount01, float parallelAmount01)
    {
        gate.setThreshold (gateThreshDb);
        deEsser.setAmount (deEssAmount01);
        comp.setParams (compThreshDb, compRatioIn, compAttackMs, compReleaseMs, compMakeupDb);
        saturator.setDrive (satAmount01);
        parallelAmount = parallelAmount01;

        const bool filtersChanged = (hpfFreq != hpfFreqIn || cutFreq != cutFreqIn || cutGain != cutGainIn
                                     || cutQ != cutQIn || harshGain != harshGainIn || airGain != airGainIn);
        hpfFreq = hpfFreqIn; cutFreq = cutFreqIn; cutGain = cutGainIn; cutQ = cutQIn;
        harshGain = harshGainIn; airGain = airGainIn;

        if (filtersChanged)
            updateFilters (false);
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        const int numCh = buffer.getNumChannels();
        const int n     = buffer.getNumSamples();

        gate.process (buffer);

        {
            juce::dsp::AudioBlock<float> block (buffer);
            juce::dsp::ProcessContextReplacing<float> ctx (block);
            hpf.process (ctx);
            if (cutGain < -0.05f)   cutBand.process (ctx);
            if (harshGain < -0.05f) harshBand.process (ctx);
            airShelf.process (ctx);
        }

        deEsser.process (buffer);
        comp.process (buffer);
        saturator.process (buffer);

        // Parallel compression: blend in a heavily smashed copy of the post-chain signal.
        if (parallelAmount > 0.001f)
        {
            parallelBuf.setSize (numCh, n, false, false, true);
            for (int ch = 0; ch < numCh; ++ch)
                parallelBuf.copyFrom (ch, 0, buffer, ch, 0, n);

            parallelComp.process (parallelBuf);

            const float wet = parallelAmount * 0.7f; // keep the blend musical
            for (int ch = 0; ch < numCh; ++ch)
                buffer.addFrom (ch, 0, parallelBuf, ch, 0, n, wet);
        }
    }

    void reset()
    {
        gate.reset(); deEsser.reset(); comp.reset(); parallelComp.reset();
        for (auto* f : { &hpf, &cutBand, &harshBand, &airShelf })
            f->reset();
    }

private:
    void updateFilters (bool force)
    {
        juce::ignoreUnused (force);
        using Coefs = juce::dsp::IIR::Coefficients<float>;
        *hpf.state       = *Coefs::makeHighPass  (sampleRate, juce::jlimit (20.0f, 400.0f, hpfFreq), 0.707f);
        *cutBand.state   = *Coefs::makePeakFilter (sampleRate, juce::jlimit (100.0f, 8000.0f, cutFreq),
                                                   juce::jlimit (0.5f, 8.0f, cutQ), dbToGain (juce::jmin (cutGain, 0.0f)));
        *harshBand.state = *Coefs::makePeakFilter (sampleRate, 3500.0f, 1.4f, dbToGain (juce::jmin (harshGain, 0.0f)));
        *airShelf.state  = *Coefs::makeHighShelf (sampleRate, 12000.0f, 0.707f, dbToGain (juce::jmax (airGain, 0.0f)));
    }

    using StereoFilter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                        juce::dsp::IIR::Coefficients<float>>;

    double sampleRate = 44100.0;
    Gate gate;
    DeEsser deEsser;
    Compressor comp, parallelComp;
    Saturator saturator;
    StereoFilter hpf, cutBand, harshBand, airShelf;
    juce::AudioBuffer<float> parallelBuf;

    float hpfFreq = 80.0f, cutFreq = 300.0f, cutGain = 0.0f, cutQ = 2.5f, harshGain = 0.0f, airGain = 2.0f;
    float parallelAmount = 0.25f;
};

} // namespace vf
