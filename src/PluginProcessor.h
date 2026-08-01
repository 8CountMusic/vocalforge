#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "Presets.h"
#include "dsp/CleanupChain.h"
#include "dsp/CharacterEngine.h"
#include "dsp/Space.h"

class VocalForgeProcessor : public juce::AudioProcessor
{
public:
    VocalForgeProcessor();
    ~VocalForgeProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "VocalForge"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    vf::PresetManager presets { apvts };

    // ---- Visualisation taps (read by the editor) ----
    static constexpr int fftOrder = 11;
    static constexpr int fftSize  = 1 << fftOrder; // 2048

    std::array<std::atomic<float>, 2> outputPeak { 0.0f, 0.0f };
    std::array<float, fftSize> scopeData {};
    std::atomic<bool> scopeReady { false };

private:
    void pushScopeSample (float s)
    {
        scopeFifo[(size_t) scopeIdx] = s;
        if (++scopeIdx >= fftSize)
        {
            scopeIdx = 0;
            if (! scopeReady.load())
            {
                scopeData = scopeFifo;
                scopeReady.store (true);
            }
        }
    }

    std::array<float, fftSize> scopeFifo {};
    int scopeIdx = 0;

    float getParam (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    vf::CleanupChain cleanup;
    vf::CharacterEngine character;
    vf::SpaceSection space;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalForgeProcessor)
};
