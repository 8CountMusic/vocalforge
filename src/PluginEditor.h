#pragma once
#include "PluginProcessor.h"
#include "ui/Visualizer.h"

namespace vfui
{
    const juce::Colour background { 0xff0f1218 };
    const juce::Colour panel      { 0xff171c25 };
    const juce::Colour panelLine  { 0xff232a37 };
    const juce::Colour accent     { 0xffff4d5a };
    const juce::Colour accentSoft { 0xffff8b94 };
    const juce::Colour textMain   { 0xffe8ebf0 };
    const juce::Colour textDim    { 0xff8b94a5 };
}

class VocalForgeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    VocalForgeLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
};

// A rotary knob with its caption, wired to a parameter.
struct Knob : public juce::Component
{
    Knob (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID, const juce::String& caption);
    void resized() override;

    juce::Slider slider;
    juce::Label label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

class VocalForgeEditor : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit VocalForgeEditor (VocalForgeProcessor&);
    ~VocalForgeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Section { juce::String title; juce::Rectangle<int> bounds; };

    void timerCallback() override;
    void showPresetMenu();
    void showSaveDialog();
    float eqResponseDb (float freq) const;

    VocalForgeProcessor& processor;
    VocalForgeLookAndFeel lnf;

    // Header / presets
    juce::TextButton presetButton { "Default" };
    juce::TextButton saveButton { "SAVE" };
    juce::StringArray userPresetCache;

    // Visualisation
    vfui::Visualizer visualizer;
    vfui::StereoMeter meter;

    // Clean chain knobs
    std::unique_ptr<Knob> inGain, gateThresh;
    std::unique_ptr<Knob> hpfFreq, cutFreq, cutGain, cutQ, harshGain, airGain;
    std::unique_ptr<Knob> dsAmount, compThresh, compRatio, compAttack, compRelease, compMakeup;
    std::unique_ptr<Knob> satDrive, parAmount;

    // Character
    juce::OwnedArray<juce::TextButton> modeButtons;
    std::unique_ptr<juce::ParameterAttachment> modeAttachment;
    std::unique_ptr<Knob> charAmount, charTune, charColor;

    // Space + output
    std::unique_ptr<Knob> rvbMix, rvbSize, dlyMix;
    juce::ComboBox dlySyncBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> dlySyncAttachment;
    juce::Label dlySyncLabel;
    std::unique_ptr<Knob> outGain;

    std::vector<Section> sections;

    void updateModeButtons (float denormalisedValue);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalForgeEditor)
};
