#pragma once
#include "Params.h"

namespace vf
{

struct FactoryPreset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values; // paramID -> denormalised value
};

// Every preset starts from defaults, then applies its overrides.
inline const std::vector<FactoryPreset>& getFactoryPresets()
{
    using namespace ParamID;
    static const std::vector<FactoryPreset> presets = {
        { "Clean & Tight", {
            { gateThresh, -55.0f }, { hpfFreq, 90.0f }, { dsAmount, 40.0f },
            { compThresh, -20.0f }, { compRatio, 3.0f }, { compMakeup, 4.0f },
            { satDrive, 12.0f }, { parAmount, 30.0f }, { rvbMix, 8.0f }, { rvbSize, 30.0f } } },

        { "Radio Ready", {
            { gateThresh, -55.0f }, { hpfFreq, 110.0f }, { harshGain, -3.0f }, { airGain, 4.5f },
            { dsAmount, 45.0f }, { compThresh, -24.0f }, { compRatio, 4.0f }, { compMakeup, 7.0f },
            { satDrive, 35.0f }, { parAmount, 50.0f }, { rvbMix, 10.0f }, { dlyMix, 12.0f }, { dlySync, 1.0f } } },

        { "Trailer Voice", {
            { charMode, 1.0f }, { charAmount, 100.0f }, { charTune, 0.0f }, { charColor, 60.0f },
            { hpfFreq, 60.0f }, { compThresh, -22.0f }, { compRatio, 4.0f }, { compMakeup, 6.0f },
            { satDrive, 30.0f }, { rvbMix, 20.0f }, { rvbSize, 65.0f } } },

        { "Monster", {
            { charMode, 1.0f }, { charAmount, 100.0f }, { charTune, -6.0f }, { charColor, 95.0f },
            { satDrive, 55.0f }, { parAmount, 40.0f }, { rvbMix, 15.0f }, { rvbSize, 55.0f } } },

        { "Cyborg", {
            { charMode, 2.0f }, { charAmount, 90.0f }, { charTune, 0.0f }, { charColor, 60.0f },
            { compThresh, -20.0f }, { compRatio, 4.0f }, { compMakeup, 5.0f },
            { dlyMix, 18.0f }, { dlySync, 0.0f } } },

        { "Broken Android", {
            { charMode, 2.0f }, { charAmount, 100.0f }, { charTune, -7.0f }, { charColor, 95.0f },
            { satDrive, 45.0f }, { rvbMix, 12.0f } } },

        { "Choir Stack", {
            { charMode, 3.0f }, { charAmount, 100.0f }, { charTune, 0.0f }, { charColor, 80.0f },
            { compThresh, -18.0f }, { compMakeup, 4.0f },
            { rvbMix, 35.0f }, { rvbSize, 75.0f } } },

        { "Psy Alien", { // Manipulator-style formant mangling
            { charMode, 4.0f }, { charAmount, 100.0f }, { charTune, 3.0f }, { charColor, 95.0f },
            { satDrive, 40.0f }, { dlyMix, 22.0f }, { dlySync, 1.0f }, { rvbMix, 18.0f }, { rvbSize, 50.0f } } },

        { "Festival Lead", {
            { charMode, 4.0f }, { charAmount, 85.0f }, { charTune, 0.0f }, { charColor, 70.0f },
            { airGain, 6.0f }, { compThresh, -22.0f }, { compRatio, 4.5f }, { compMakeup, 7.0f },
            { parAmount, 55.0f }, { dlyMix, 25.0f }, { dlySync, 1.0f }, { rvbMix, 22.0f }, { rvbSize, 60.0f } } },

        { "Wub Machine", {
            { charMode, 5.0f }, { charAmount, 100.0f }, { charTune, -2.0f }, { charColor, 60.0f },
            { satDrive, 40.0f }, { parAmount, 35.0f }, { rvbMix, 8.0f } } },

        { "Growl Bass", {
            { charMode, 5.0f }, { charAmount, 100.0f }, { charTune, -7.0f }, { charColor, 80.0f },
            { satDrive, 60.0f }, { compThresh, -24.0f }, { compRatio, 5.0f }, { compMakeup, 6.0f } } },
    };
    return presets;
}

class PresetManager
{
public:
    explicit PresetManager (juce::AudioProcessorValueTreeState& state) : apvts (state)
    {
        getUserPresetFolder().createDirectory();
    }

    static juce::File getUserPresetFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("VocalForge Presets");
    }

    void applyFactoryPreset (int index)
    {
        const auto& presets = getFactoryPresets();
        if (index < 0 || index >= (int) presets.size()) return;

        resetAllToDefault();
        for (const auto& [id, value] : presets[(size_t) index].values)
            setParam (id, value);
        currentName = presets[(size_t) index].name;
    }

    juce::StringArray getUserPresetNames() const
    {
        juce::StringArray names;
        for (const auto& f : getUserPresetFolder().findChildFiles (juce::File::findFiles, false, "*.vfpreset"))
            names.add (f.getFileNameWithoutExtension());
        names.sortNatural();
        return names;
    }

    bool saveUserPreset (const juce::String& name)
    {
        if (name.trim().isEmpty()) return false;
        auto file = getUserPresetFolder().getChildFile (juce::File::createLegalFileName (name) + ".vfpreset");
        if (auto xml = apvts.copyState().createXml())
        {
            if (xml->writeTo (file))
            {
                currentName = name;
                return true;
            }
        }
        return false;
    }

    bool loadUserPreset (const juce::String& name)
    {
        auto file = getUserPresetFolder().getChildFile (juce::File::createLegalFileName (name) + ".vfpreset");
        if (! file.existsAsFile()) return false;
        if (auto xml = juce::XmlDocument::parse (file))
        {
            // Apply through parameters (not replaceState) so the host records automation-safe changes.
            auto tree = juce::ValueTree::fromXml (*xml);
            if (! tree.isValid()) return false;
            resetAllToDefault();
            for (int i = 0; i < tree.getNumChildren(); ++i)
            {
                auto child = tree.getChild (i);
                if (child.hasType ("PARAM"))
                    setParam (child.getProperty ("id").toString().toRawUTF8(),
                              (float) (double) child.getProperty ("value"));
            }
            currentName = name;
            return true;
        }
        return false;
    }

    juce::String getCurrentName() const { return currentName; }

private:
    void resetAllToDefault()
    {
        for (auto* p : apvts.processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                ranged->beginChangeGesture();
                ranged->setValueNotifyingHost (ranged->getDefaultValue());
                ranged->endChangeGesture();
            }
    }

    void setParam (const char* id, float denormalised)
    {
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (denormalised));
            p->endChangeGesture();
        }
    }

    juce::AudioProcessorValueTreeState& apvts;
    juce::String currentName { "Default" };
};

} // namespace vf
