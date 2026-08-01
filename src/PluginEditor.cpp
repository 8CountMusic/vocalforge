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
    setColour (juce::PopupMenu::headerTextColourId, accentSoft);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.25f));
    setColour (juce::AlertWindow::backgroundColourId, panel);
    setColour (juce::AlertWindow::textColourId, textMain);
    setColour (juce::TextEditor::backgroundColourId, background);
    setColour (juce::TextEditor::textColourId, textMain);
    setColour (juce::TextEditor::outlineColourId, panelLine);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
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

    // Value arc with a soft glow
    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);
    g.setColour (accent.withAlpha (0.25f));
    g.strokePath (value, juce::PathStrokeType (arcThickness * 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (accent);
    g.strokePath (value, juce::PathStrokeType (arcThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Body
    const float bodyRadius = radius - arcThickness * 2.2f;
    g.setGradientFill (juce::ColourGradient (panel.brighter (0.18f), centre.x, centre.y - bodyRadius,
                                             panel.brighter (0.04f), centre.x, centre.y + bodyRadius, false));
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
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, 14);
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

    // Header: presets
    presetButton.setButtonText (processor.presets.getCurrentName());
    presetButton.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible (presetButton);

    saveButton.onClick = [this] { showSaveDialog(); };
    addAndMakeVisible (saveButton);

    // Visualiser + meter
    visualizer.getEqResponseDb = [this] (float freq) { return eqResponseDb (freq); };
    addAndMakeVisible (visualizer);
    addAndMakeVisible (meter);

    // ---- Tune section ----
    auto setupToggle = [this] (juce::TextButton& b,
                               std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>& att,
                               const char* paramID)
    {
        b.setClickingTogglesState (true);
        addAndMakeVisible (b);
        att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, paramID, b);
    };
    setupToggle (tuneOnButton,     tuneOnAtt,     ParamID::tuneOn);
    setupToggle (midiFollowButton, midiFollowAtt, ParamID::midiFollow);
    setupToggle (midiOutButton,    midiOutAtt,    ParamID::midiOut);

    keyBox.addItemList ({ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 1);
    scaleBox.addItemList ({ "CHROMATIC", "MAJOR", "MINOR" }, 1);
    addAndMakeVisible (keyBox);
    addAndMakeVisible (scaleBox);
    keyAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, ParamID::tuneKey, keyBox);
    scaleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, ParamID::tuneScale, scaleBox);

    for (auto* l : { &keyLabel, &scaleLabel })
    {
        l->setJustificationType (juce::Justification::centredLeft);
        l->setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
        addAndMakeVisible (*l);
    }
    keyLabel.setText ("KEY", juce::dontSendNotification);
    scaleLabel.setText ("SCALE", juce::dontSendNotification);

    makeKnob (tuneSpeed,  ParamID::tuneSpeed,  "SPEED");
    makeKnob (tuneAmount, ParamID::tuneAmount, "AMOUNT");

    addAndMakeVisible (pitchDisplay);

    startTimerHz (30);
    setSize (1024, 906);
}

VocalForgeEditor::~VocalForgeEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
void VocalForgeEditor::timerCallback()
{
    if (processor.scopeReady.load())
    {
        visualizer.pushFrame (processor.scopeData, processor.getSampleRate());
        processor.scopeReady.store (false);
    }

    const float pl = processor.outputPeak[0].exchange (0.0f);
    const float pr = processor.outputPeak[1].exchange (0.0f);
    meter.update (pl, pr);

    pitchDisplay.update (processor.detectedPitchHz.load(), processor.targetPitchHz.load());
}

void VocalForgeEditor::PitchDisplay::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xff11141b));
    g.fillRoundedRectangle (r, 8.0f);

    auto noteName = [] (float hz) -> juce::String
    {
        if (hz <= 0.0f) return "--";
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const float midi = 69.0f + 12.0f * std::log2 (hz / 440.0f);
        const int m = (int) std::round (midi);
        if (m < 0 || m > 127) return "--";
        return juce::String (names[m % 12]) + juce::String (m / 12 - 1);
    };

    g.setColour (textDim);
    g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    g.drawText ("PITCH", 10, 6, 60, 12, juce::Justification::left);

    const bool active = det > 0.0f;
    g.setColour (active ? textMain : textDim.withAlpha (0.5f));
    g.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));

    juce::String text = noteName (det);
    if (tgt > 0.0f)
        text += "  >  " + noteName (tgt);
    g.drawText (text, getLocalBounds().reduced (8).withTrimmedTop (10), juce::Justification::centred);
}

float VocalForgeEditor::eqResponseDb (float freq) const
{
    using Coefs = juce::dsp::IIR::Coefficients<float>;
    double sr = processor.getSampleRate();
    if (sr < 1000.0) sr = 48000.0;

    auto get = [this] (const char* id) { return processor.apvts.getRawParameterValue (id)->load(); };

    const auto hpf   = Coefs::makeHighPass  (sr, juce::jlimit (20.0f, 400.0f, get (ParamID::hpfFreq)), 0.707f);
    const auto cut   = Coefs::makePeakFilter (sr, juce::jlimit (100.0f, 8000.0f, get (ParamID::cutFreq)),
                                              juce::jlimit (0.5f, 8.0f, get (ParamID::cutQ)),
                                              juce::Decibels::decibelsToGain (juce::jmin (get (ParamID::cutGain), 0.0f)));
    const auto harsh = Coefs::makePeakFilter (sr, 3500.0f, 1.4f,
                                              juce::Decibels::decibelsToGain (juce::jmin (get (ParamID::harshGain), 0.0f)));
    const auto air   = Coefs::makeHighShelf (sr, 12000.0f, 0.707f,
                                             juce::Decibels::decibelsToGain (juce::jmax (get (ParamID::airGain), 0.0f)));

    double mag = 1.0;
    for (const auto& c : { hpf, cut, harsh, air })
        mag *= c->getMagnitudeForFrequency ((double) freq, sr);

    return (float) juce::Decibels::gainToDecibels (mag, -60.0);
}

void VocalForgeEditor::showPresetMenu()
{
    userPresetCache = processor.presets.getUserPresetNames();

    juce::PopupMenu m;
    m.setLookAndFeel (&lnf);
    m.addSectionHeader ("FACTORY");
    const auto& factory = vf::getFactoryPresets();
    for (int i = 0; i < (int) factory.size(); ++i)
        m.addItem (1 + i, factory[(size_t) i].name);

    if (! userPresetCache.isEmpty())
    {
        m.addSeparator();
        m.addSectionHeader ("USER");
        for (int i = 0; i < userPresetCache.size(); ++i)
            m.addItem (1000 + i, userPresetCache[i]);
    }

    m.addSeparator();
    m.addItem (9000, "Save current as...");

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetButton),
        [this] (int result)
        {
            if (result == 0) return;
            if (result == 9000) { showSaveDialog(); return; }

            if (result >= 1000)
            {
                const int idx = result - 1000;
                if (idx < userPresetCache.size())
                    processor.presets.loadUserPreset (userPresetCache[idx]);
            }
            else
            {
                processor.presets.applyFactoryPreset (result - 1);
            }
            presetButton.setButtonText (processor.presets.getCurrentName());
        });
}

void VocalForgeEditor::showSaveDialog()
{
    auto* aw = new juce::AlertWindow ("Save Preset", "Name your preset:", juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("name", processor.presets.getCurrentName());
    aw->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->setLookAndFeel (&lnf);

    aw->enterModalState (true,
        juce::ModalCallbackFunction::create ([this, aw] (int result)
        {
            if (result == 1)
            {
                const auto name = aw->getTextEditorContents ("name");
                if (processor.presets.saveUserPreset (name))
                    presetButton.setButtonText (processor.presets.getCurrentName());
            }
            aw->setLookAndFeel (nullptr);
        }),
        true); // delete when dismissed
}

void VocalForgeEditor::updateModeButtons (float denormalisedValue)
{
    const int idx = juce::jlimit (0, modeButtons.size() - 1, (int) std::round (denormalisedValue));
    for (int i = 0; i < modeButtons.size(); ++i)
        modeButtons[i]->setToggleState (i == idx, juce::dontSendNotification);
}

//==============================================================================
void VocalForgeEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (background.brighter (0.04f), 0.0f, 0.0f,
                                             background.darker (0.15f), 0.0f, (float) getHeight(), false));
    g.fillAll();

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

    // ---------- Header ----------
    auto header = area.removeFromTop (52);
    auto presetArea = header.removeFromRight (300).withSizeKeepingCentre (300, 32);
    saveButton.setBounds (presetArea.removeFromRight (68));
    presetArea.removeFromRight (8);
    presetButton.setBounds (presetArea);

    area.removeFromTop (6);

    // ---------- Visualiser strip ----------
    auto visRow = area.removeFromTop (150);
    meter.setBounds (visRow.removeFromRight (120));
    visRow.removeFromRight (10);
    visualizer.setBounds (visRow);

    area.removeFromTop (12);

    const int gap = 10;
    const int titleH = 28;

    // ---------- Tune row ----------
    auto tuneRow = area.removeFromTop (114);
    sections.push_back ({ "TUNE", tuneRow });
    {
        auto inner = tuneRow.reduced (10);
        inner.removeFromTop (titleH - 10);

        auto mid = inner.withSizeKeepingCentre (inner.getWidth(), 32);
        auto left = mid;

        tuneOnButton.setBounds (left.removeFromLeft (76)); left.removeFromLeft (12);
        keyLabel.setBounds (left.removeFromLeft (30));
        keyBox.setBounds (left.removeFromLeft (64).reduced (0, 1)); left.removeFromLeft (12);
        scaleLabel.setBounds (left.removeFromLeft (44));
        scaleBox.setBounds (left.removeFromLeft (110).reduced (0, 1)); left.removeFromLeft (16);

        // Knobs get taller bounds than the button strip
        auto knobArea = juce::Rectangle<int> (left.getX(), inner.getY(), 190, inner.getHeight());
        tuneSpeed ->setBounds (knobArea.removeFromLeft (95));
        tuneAmount->setBounds (knobArea);
        left.removeFromLeft (200);

        midiFollowButton.setBounds (left.removeFromLeft (110)); left.removeFromLeft (10);
        midiOutButton.setBounds (left.removeFromLeft (92)); left.removeFromLeft (12);

        pitchDisplay.setBounds (juce::Rectangle<int> (left.getX(), inner.getY(), left.getWidth(), inner.getHeight()));
    }

    area.removeFromTop (gap);

    // ---------- Row 1: cleanup chain ----------
    auto row1 = area.removeFromTop (236);

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
    auto eqPanel    = r1.removeFromLeft (356); r1.removeFromLeft (gap);
    auto dynPanel   = r1.removeFromLeft (356); r1.removeFromLeft (gap);
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
