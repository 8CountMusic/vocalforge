#include "PluginProcessor.h"
#include "PluginEditor.h"

VocalForgeProcessor::VocalForgeProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createParameterLayout())
{
}

bool VocalForgeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void VocalForgeProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec { sampleRate,
                                  (juce::uint32) samplesPerBlock,
                                  (juce::uint32) juce::jmax (1, getTotalNumOutputChannels()) };
    cleanup.prepare (spec);
    character.prepare (spec);
    space.prepare (spec);
    setLatencySamples (character.getLatencySamples());
}

void VocalForgeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // Host tempo (falls back to 150 — cheer mix standard — when the host doesn't say)
    double bpm = 150.0;
    if (auto* playHead = getPlayHead())
        if (auto pos = playHead->getPosition())
            if (auto hostBpm = pos->getBpm())
                if (*hostBpm > 20.0) bpm = *hostBpm;

    buffer.applyGain (vf::dbToGain (getParam (ParamID::inGain)));

    cleanup.setParams (getParam (ParamID::gateThresh),
                       getParam (ParamID::hpfFreq),
                       getParam (ParamID::cutFreq),
                       getParam (ParamID::cutGain),
                       getParam (ParamID::cutQ),
                       getParam (ParamID::harshGain),
                       getParam (ParamID::airGain),
                       getParam (ParamID::dsAmount) / 100.0f,
                       getParam (ParamID::compThresh),
                       getParam (ParamID::compRatio),
                       getParam (ParamID::compAttack),
                       getParam (ParamID::compRelease),
                       getParam (ParamID::compMakeup),
                       getParam (ParamID::satDrive) / 100.0f,
                       getParam (ParamID::parAmount) / 100.0f);
    cleanup.process (buffer);

    character.setParams ((int) getParam (ParamID::charMode),
                         getParam (ParamID::charAmount) / 100.0f,
                         getParam (ParamID::charTune),
                         getParam (ParamID::charColor));
    character.process (buffer, bpm);

    space.setParams (getParam (ParamID::rvbMix) / 100.0f,
                     getParam (ParamID::rvbSize) / 100.0f,
                     getParam (ParamID::dlyMix) / 100.0f,
                     (int) getParam (ParamID::dlySync));
    space.process (buffer, bpm);

    buffer.applyGain (vf::dbToGain (getParam (ParamID::outGain)));

    // Safety clip so nothing ever slams the master bus.
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            d[i] = juce::jlimit (-1.5f, 1.5f, d[i]);
    }

    // ---- Feed the editor's meters and spectrum analyser ----
    const int numCh = buffer.getNumChannels();
    const int n     = buffer.getNumSamples();

    for (int ch = 0; ch < juce::jmin (2, numCh); ++ch)
    {
        const float blockPeak = buffer.getMagnitude (ch, 0, n);
        float current = outputPeak[(size_t) ch].load();
        if (blockPeak > current)
            outputPeak[(size_t) ch].store (blockPeak);
    }
    if (numCh == 1)
        outputPeak[1].store (outputPeak[0].load());

    for (int i = 0; i < n; ++i)
    {
        float mono = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            mono += buffer.getSample (ch, i);
        pushScopeSample (mono / (float) juce::jmax (1, numCh));
    }
}

void VocalForgeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void VocalForgeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* VocalForgeProcessor::createEditor()
{
    return new VocalForgeEditor (*this);
}

// JUCE plugin entry point
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalForgeProcessor();
}
