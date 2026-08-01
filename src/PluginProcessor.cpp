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
    detector.prepare (sampleRate);
    autoTune.prepare (spec);
    lastReportedLatency = character.getLatencySamples();
    setLatencySamples (lastReportedLatency);
}

void VocalForgeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // ---- Collect held MIDI notes (for tune targeting / chord-follow harmony) ----
    for (const auto meta : midiMessages)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            if (std::find (heldNotes.begin(), heldNotes.end(), m.getNoteNumber()) == heldNotes.end())
                heldNotes.push_back (m.getNoteNumber());
            std::sort (heldNotes.begin(), heldNotes.end());
        }
        else if (m.isNoteOff())
        {
            heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), m.getNoteNumber()), heldNotes.end());
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            heldNotes.clear();
        }
    }

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

    // ---- Pitch detection (feeds tune, chord-follow harmony, MIDI out, and the UI) ----
    {
        const int n = buffer.getNumSamples();
        const int numCh = buffer.getNumChannels();
        detectorMono.setSize (1, n, false, false, true);
        detectorMono.clear();
        for (int ch = 0; ch < numCh; ++ch)
            detectorMono.addFrom (0, 0, buffer, ch, 0, n, 1.0f / (float) juce::jmax (1, numCh));
        detector.push (detectorMono.getReadPointer (0), n);
    }
    const float pitchHz = detector.getHz();
    const bool voiced = detector.isVoiced();
    detectedPitchHz.store (voiced ? pitchHz : 0.0f);

    // ---- Autotune ----
    const bool followMidi = getParam (ParamID::midiFollow) > 0.5f;
    autoTune.setParams (getParam (ParamID::tuneOn) > 0.5f,
                        (int) getParam (ParamID::tuneKey),
                        (int) getParam (ParamID::tuneScale),
                        getParam (ParamID::tuneSpeed) / 100.0f,
                        getParam (ParamID::tuneAmount) / 100.0f,
                        followMidi);
    autoTune.setHeldNotes (heldNotes);
    targetPitchHz.store (autoTune.process (buffer, pitchHz, voiced));

    // Report latency changes when tune toggles on/off
    const int wantedLatency = character.getLatencySamples()
                              + (autoTune.isOn() ? autoTune.getLatencySamples() : 0);
    if (wantedLatency != lastReportedLatency)
    {
        lastReportedLatency = wantedLatency;
        setLatencySamples (wantedLatency);
    }

    // ---- Chord-follow harmony offsets ----
    if (followMidi && voiced && ! heldNotes.empty())
    {
        const float base = vf::hzToMidi (pitchHz);
        const float s1 = (float) heldNotes[0] - base;
        const float s2 = heldNotes.size() > 1 ? (float) heldNotes[1] - base : s1 + 12.0f;
        character.setHarmonyOverride (true, s1, s2);
    }
    else
    {
        character.setHarmonyOverride (false, 0.0f, 0.0f);
    }

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

    // ---- MIDI out: detected vocal pitch as notes for other plugins ----
    midiMessages.clear();
    if (getParam (ParamID::midiOut) > 0.5f)
        midiTracker.process (midiMessages, pitchHz, voiced, outputPeak[0].load());
    else
        midiTracker.allNotesOff (midiMessages);
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
