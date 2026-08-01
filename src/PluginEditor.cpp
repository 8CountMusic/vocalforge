#include "PluginEditor.h"

using namespace vfui;

//==============================================================================
VocalForgeLookAndFeel::VocalForgeLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, textDim);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, textDim);
    setColour (juce::ComboBox::backgroundColourId, panel.brighter (0.06f));
    setColour (juce::ComboBox::textColourId, textMain);
    setColour (juce::ComboBox::outlineColourId, panelLine);
    setColour (juce::ComboBox::arrowColourId, accent);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, textMain);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.25f));
}

void VocalForgeLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                              float pos, float startAngle, float endAngle, juce::Slider&)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (4.0f);
    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    bounds = bounds.withSizeKeepingCentre (size, size);
    const auto centre = bounds.getCentre();
    const float radius = size * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float arcThickness = juce::jmax (2.5f, radius * 0.14f);
    const float arcRadius = radius - arcThickness * 0.5f;

    // Track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (panelLine);
    g.strokePath (track, juce::PathStrokeType (arcThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Value arc
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);
    g.setColour (accent);
    g.strokePath (value, juce::PathStrokeType (arcThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Body
    const float bodyRadius = radius - arcThickness * 2.2f;
    g.setColour (panel.brighter (0.10f));
    g.fillEllipse (centre.x - bodyRadius, centre.y - bodyRadius, bodyRadius * 2.0f, bodyRadius * 2.0f);
    g.setColour (panelLine.brighter (0.1f));
    g.drawEllipse (centre.x - bodyRadius, centre.y - bodyRadius, bodyRadius * 2.0f, bodyRadius * 2.0f, 1.0f);

    // Pointer
    const float pointerLen = bodyRadius * 0.65f;
    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -bodyRadius + 2.0f, 3.0f, pointerLen, 1.5f);
    g.setColour (textMain);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
}

void VocalForgeLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                                  bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = button.getToggleState();

    juce::Colour fill = on ? accent.withAlpha (0.9f)
                           : panel.brighter (highlighted ? 0.14f : 0.06f);
    if (down) fill = fill.brighter (0.1f);

    g.setColour (fill);
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (on ? accent : panelLine);
    g.drawRoundedRectangle (bounds, 6.0f, 1.0f);
}

void VocalForgeLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    g.setColour (button.getToggleState() ? juce::Colours::white : textDim);
    g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
}

void VocalForgeLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                          int, int, int, int, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (1.0f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds, 6.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) width - 16.0f, ay = (float) height * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.5f, ax + 4.0f, ay - 2.5f, ax, ay + 3.5f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID, const juce::String& caption)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 68, 14);
    slider.setNumDecimalPlacesToDisplay (1);
    addAndMakeVisible (slider);

    label.setText (caption, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
    addAndMakeVisible (label);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramID, slider);
}

void Knob::resized()
{
    auto area = getLocalBounds();
    label.setBounds (area.removeFromTop (14));
    slider.setBounds (area);
}

//==============================================================================
VocalForgeEditor::VocalForgeEditor (VocalForgeProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    setLookAndFeel (&lnf);

    auto makeKnob = [this] (std::unique_ptr<Knob>& holder, const char* id, const juce::String& caption)
    {
        holder = std::make_unique<Knob> (processor.apvts, id, caption);
        addAndMakeVisible (*holder);
    };

    makeKnob (inGain,      ParamID::inGain,      "INPUT");
    makeKnob (gateThresh,  ParamID::gateThresh,  "GATE");
    makeKnob (hpfFreq,     ParamID::hpfFreq,     "LOW CUT");
    makeKnob (cutFreq,     ParamID::cutFreq,     "CUT FREQ");
    makeKnob (cutGain,     ParamID::cutGain,     "CUT DEPTH");
    makeKnob (cutQ,        ParamID::cutQ,        "CUT Q");
    makeKnob (harshGain,   ParamID::harshGain,   "HARSH");
    makeKnob (airGain,     ParamID::airGain,     "AIR");
    makeKnob (dsAmount,    ParamID::dsAmount,    "DE-ESS");
    makeKnob (compThresh,  ParamID::compThresh,  "THRESH");
    makeKnob (compRatio,   ParamID::compRatio,   "RATIO");
    makeKnob (compAttack,  ParamID::compAttack,  "ATTACK");
    makeKnob (compRelease, ParamID::compRelease, "RELEASE");
    makeKnob (compMakeup,  ParamID::compMakeup,  "MAKEUP");
    makeKnob (satDrive,    ParamID::satDrive,    "SATURATE");
    makeKnob (parAmount,   ParamID::parAmount,   "PARALLEL");
    makeKnob (charAmount,  ParamID::charAmount,  "AMOUNT");
    makeKnob (charTune,    ParamID::charTune,    "TUNE");
    makeKnob (charColor,   ParamID::charColor,   "COLOR");
    makeKnob (rvbMix,      ParamID::rvbMix,      "REVERB");
    makeKnob (rvbSize,     ParamID::rvbSize,     "SIZE");
    makeKnob (dlyMix,      ParamID::dlyMix,      "DELAY");
    makeKnob (outGain,     ParamID::outGain,     "OUTPUT");

    // Character mode buttons
    const juce::StringArray modeNames { "NATURAL", "DEEP", "ROBOT", "HARMONY", "EDM", "DUBSTEP" };
    auto* modeParam = processor.apvts.getParameter (ParamID::charMode);

    for (int i = 0; i < modeNames.size(); ++i)
    {
        auto* b = modeButtons.add (new juce::TextButton (modeNames[i]));
        b->setClickingTogglesState (false);
        b->onClick = [this, i]
        {
            if (modeAttachment != nullptr)
                modeAttachment->setValueAsCompleteGesture ((float) i);
        };
        addAndMakeVisible (b);
    }

    modeAttachment = std::make_unique<juce::ParameterAttachment> (
        *modeParam,
        [this] (float v) { updateModeButtons (v); },
        nullptr);
    modeAttachment->sendInitialUpdate();

    // Delay sync box
    dlySyncBox.addItemList ({ "1/8", "1/8 DOT", "1/4" }, 1);
    addAndMakeVisible (dlySyncBox);
    dlySyncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.apvts, ParamID::dlySync, dlySyncBox);

    dlySyncLabel.setText ("TIME", juce::dontSendNotification);
    dlySyncLabel.setJustificationType (juce::Justification::centred);
    dlySyncLabel.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
    addAndMakeVisible (dlySyncLabel);

    setSize (980, 620);
}

VocalForgeEditor::~VocalForgeEditor()
{
    setLookAndFeel (nullptr);
}

void VocalForgeEditor::updateModeButtons (float denormalisedValue)
{
    const int idx = juce::jlimit (0, modeButtons.size() - 1, (int) std::round (denormalisedValue));
    for (int i = 0; i < modeButtons.size(); ++i)
        modeButtons[i]->setToggleState (i == idx, juce::dontSendNotification);
}

void VocalForgeEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);

    // Header
    g.setColour (textMain);
    g.setFont (juce::Font (juce::FontOptions (26.0f, juce::Font::bold)));
    g.drawText ("VOCALFORGE", 24, 14, 300, 30, juce::Justification::centredLeft);
    g.setColour (accent);
    g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    g.drawText ("8 COUNT MUSIC  |  ALL-IN-ONE VOCAL CHAIN", 24, 42, 400, 16, juce::Justification::centredLeft);

    // Section panels
    for (const auto& s : sections)
    {
        auto r = s.bounds.toFloat();
        g.setColour (panel);
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (panelLine);
        g.drawRoundedRectangle (r, 10.0f, 1.0f);
        g.setColour (textDim);
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.drawText (s.title, s.bounds.getX() + 12, s.bounds.getY() + 8, s.bounds.getWidth() - 24, 16,
                    juce::Justification::centredLeft);
    }
}

void VocalForgeEditor::resized()
{
    sections.clear();

    auto area = getLocalBounds().reduced (16);
    area.removeFromTop (52); // header

    const int gap = 10;
    const int knobH = 96;
    const int titleH = 28;

    // ---------- Row 1: cleanup chain ----------
    auto row1 = area.removeFromTop (titleH + knobH * 2 + 14);

    auto placeGrid = [&] (juce::Rectangle<int> panelArea, const juce::String& title,
                          std::initializer_list<Knob*> knobs, int columns)
    {
        sections.push_back ({ title, panelArea });
        auto inner = panelArea.reduced (10);
        inner.removeFromTop (titleH - 10);
        const int rows = ((int) knobs.size() + columns - 1) / columns;
        const int cw = inner.getWidth() / columns;
        const int ch = inner.getHeight() / rows;
        int i = 0;
        for (auto* k : knobs)
        {
            const int cx = i % columns, cy = i / columns;
            k->setBounds (inner.getX() + cx * cw, inner.getY() + cy * ch, cw, ch);
            ++i;
        }
    };

    auto r1 = row1;
    auto inputPanel = r1.removeFromLeft (120); r1.removeFromLeft (gap);
    auto eqPanel    = r1.removeFromLeft (300); r1.removeFromLeft (gap);
    auto dynPanel   = r1.removeFromLeft (300); r1.removeFromLeft (gap);
    auto tonePanel  = r1;

    placeGrid (inputPanel, "INPUT",    { inGain.get(), gateThresh.get() }, 1);
    placeGrid (eqPanel,    "EQ",       { hpfFreq.get(), cutFreq.get(), cutGain.get(), cutQ.get(), harshGain.get(), airGain.get() }, 3);
    placeGrid (dynPanel,   "DYNAMICS", { dsAmount.get(), compThresh.get(), compRatio.get(), compAttack.get(), compRelease.get(), compMakeup.get() }, 3);
    placeGrid (tonePanel,  "TONE",     { satDrive.get(), parAmount.get() }, 1);

    area.removeFromTop (gap);

    // ---------- Row 2: character + space + output ----------
    auto row2 = area;
    auto charPanel  = row2.removeFromLeft (520); row2.removeFromLeft (gap);
    auto spacePanel = row2.removeFromLeft (300); row2.removeFromLeft (gap);
    auto outPanel   = row2;

    // Character panel: buttons on top, 3 knobs below
    sections.push_back ({ "CHARACTER", charPanel });
    {
        auto inner = charPanel.reduced (10);
        inner.removeFromTop (titleH - 10);
        auto buttonRow = inner.removeFromTop (34);
        const int bw = (buttonRow.getWidth() - 5 * 6) / 6;
        for (int i = 0; i < modeButtons.size(); ++i)
            modeButtons[i]->setBounds (buttonRow.getX() + i * (bw + 6), buttonRow.getY(), bw, 30);

        inner.removeFromTop (8);
        const int kw = inner.getWidth() / 3;
        charAmount->setBounds (inner.getX(),          inner.getY(), kw, inner.getHeight());
        charTune  ->setBounds (inner.getX() + kw,     inner.getY(), kw, inner.getHeight());
        charColor ->setBounds (inner.getX() + kw * 2, inner.getY(), kw, inner.getHeight());
    }

    // Space panel: 3 knobs + sync box
    sections.push_back ({ "SPACE", spacePanel });
    {
        auto inner = spacePanel.reduced (10);
        inner.removeFromTop (titleH - 10);
        auto syncArea = inner.removeFromBottom (40);
        const int kw = inner.getWidth() / 3;
        rvbMix ->setBounds (inner.getX(),          inner.getY(), kw, inner.getHeight());
        rvbSize->setBounds (inner.getX() + kw,     inner.getY(), kw, inner.getHeight());
        dlyMix ->setBounds (inner.getX() + kw * 2, inner.getY(), kw, inner.getHeight());

        dlySyncLabel.setBounds (syncArea.removeFromLeft (50));
        dlySyncBox.setBounds (syncArea.reduced (4));
    }

    // Output panel
    placeGrid (outPanel, "OUTPUT", { outGain.get() }, 1);
}
