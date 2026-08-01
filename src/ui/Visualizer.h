#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace vfui
{

// Live output spectrum with the EQ response curve drawn on top.
class Visualizer : public juce::Component
{
public:
    static constexpr int fftOrder = 11;
    static constexpr int fftSize  = 1 << fftOrder;
    static constexpr int numPoints = 160;

    Visualizer() : fft (fftOrder), window (fftSize, juce::dsp::WindowingFunction<float>::hann)
    {
        levels.fill (-100.0f);
        setOpaque (false);
    }

    // Called from the editor timer with a fresh block of output samples.
    void pushFrame (const std::array<float, fftSize>& samples, double sampleRate)
    {
        sr = sampleRate > 1000.0 ? sampleRate : 48000.0;

        std::copy (samples.begin(), samples.end(), fftData.begin());
        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
        window.multiplyWithWindowingTable (fftData.data(), fftSize);
        fft.performFrequencyOnlyForwardTransform (fftData.data());

        for (int p = 0; p < numPoints; ++p)
        {
            const float freq = pointToFreq (p);
            const int bin = juce::jlimit (1, fftSize / 2 - 1, (int) std::round (freq * fftSize / sr));
            const float mag = fftData[(size_t) bin] / (float) (fftSize / 4);
            const float dB  = juce::Decibels::gainToDecibels (mag, -100.0f);

            // Fast rise, slow fall for a smooth, readable display.
            auto& lv = levels[(size_t) p];
            lv = dB > lv ? lv + (dB - lv) * 0.7f : lv + (dB - lv) * 0.15f;
        }
        repaint();
    }

    // The editor supplies a function that returns the EQ curve's gain (dB) at a frequency.
    std::function<float (float)> getEqResponseDb;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0xff11141b));
        g.fillRoundedRectangle (r, 10.0f);

        // Grid
        g.setColour (juce::Colour (0xff1d2330));
        for (float freq : { 100.0f, 1000.0f, 10000.0f })
        {
            const float x = r.getX() + freqToX (freq) * r.getWidth();
            g.drawVerticalLine ((int) x, r.getY() + 4, r.getBottom() - 4);
        }
        for (float dB : { -20.0f, -40.0f, -60.0f, -80.0f })
        {
            const float y = r.getY() + dbToY (dB) * r.getHeight();
            g.drawHorizontalLine ((int) y, r.getX() + 4, r.getRight() - 4);
        }
        g.setColour (juce::Colour (0xff5a6375));
        g.setFont (juce::Font (juce::FontOptions (9.5f)));
        g.drawText ("100", (int) (r.getX() + freqToX (100.0f) * r.getWidth()) + 3, (int) r.getBottom() - 14, 30, 12, juce::Justification::left);
        g.drawText ("1k",  (int) (r.getX() + freqToX (1000.0f) * r.getWidth()) + 3, (int) r.getBottom() - 14, 30, 12, juce::Justification::left);
        g.drawText ("10k", (int) (r.getX() + freqToX (10000.0f) * r.getWidth()) + 3, (int) r.getBottom() - 14, 30, 12, juce::Justification::left);

        // Spectrum fill
        juce::Path spec;
        spec.startNewSubPath (r.getX(), r.getBottom());
        for (int p = 0; p < numPoints; ++p)
        {
            const float x = r.getX() + (p / (float) (numPoints - 1)) * r.getWidth();
            const float y = r.getY() + dbToY (levels[(size_t) p]) * r.getHeight();
            spec.lineTo (x, y);
        }
        spec.lineTo (r.getRight(), r.getBottom());
        spec.closeSubPath();

        const juce::Colour accent (0xffff4d5a);
        g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.55f), r.getX(), r.getY(),
                                                 accent.withAlpha (0.04f), r.getX(), r.getBottom(), false));
        g.fillPath (spec);
        g.setColour (accent.withAlpha (0.9f));
        g.strokePath (spec, juce::PathStrokeType (1.4f));

        // EQ response overlay (white line, centred vertically, +/-24 dB span)
        if (getEqResponseDb != nullptr)
        {
            juce::Path eq;
            bool first = true;
            for (int p = 0; p < numPoints; ++p)
            {
                const float freq = pointToFreq (p);
                const float dB   = juce::jlimit (-24.0f, 24.0f, getEqResponseDb (freq));
                const float x = r.getX() + (p / (float) (numPoints - 1)) * r.getWidth();
                const float y = r.getCentreY() - (dB / 24.0f) * (r.getHeight() * 0.42f);
                if (first) { eq.startNewSubPath (x, y); first = false; }
                else         eq.lineTo (x, y);
            }
            g.setColour (juce::Colours::white.withAlpha (0.75f));
            g.strokePath (eq, juce::PathStrokeType (1.8f));
        }
    }

private:
    static float freqToX (float freq)
    {
        return (std::log10 (freq / 20.0f)) / std::log10 (20000.0f / 20.0f);
    }
    static float pointToFreq (int p)
    {
        const float t = p / (float) (numPoints - 1);
        return 20.0f * std::pow (20000.0f / 20.0f, t);
    }
    static float dbToY (float dB)
    {
        return juce::jlimit (0.0f, 1.0f, (dB / -100.0f));
    }

    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    std::array<float, fftSize * 2> fftData {};
    std::array<float, numPoints> levels {};
    double sr = 48000.0;
};

// Stereo peak meter with peak-hold and a dB scale.
class StereoMeter : public juce::Component
{
public:
    void update (float newL, float newR)
    {
        // Fast attack, smooth release
        auto decay = [] (float& level, float& hold, int& holdAge, float incoming)
        {
            level = incoming > level ? incoming : level * 0.86f;
            if (incoming > hold) { hold = incoming; holdAge = 0; }
            else if (++holdAge > 24) hold *= 0.94f;
        };
        decay (l, holdL, ageL, newL);
        decay (r, holdR, ageR, newR);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0xff11141b));
        g.fillRoundedRectangle (area, 10.0f);

        auto inner = area.reduced (8.0f);
        auto labelRow = inner.removeFromBottom (12.0f);
        auto scaleCol = inner.removeFromRight (26.0f);

        const float barGap = 5.0f;
        const float barW = (inner.getWidth() - barGap) / 2.0f;
        drawBar (g, { inner.getX(), inner.getY(), barW, inner.getHeight() }, l, holdL);
        drawBar (g, { inner.getX() + barW + barGap, inner.getY(), barW, inner.getHeight() }, r, holdR);

        // Scale
        g.setColour (juce::Colour (0xff5a6375));
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        for (float dB : { 0.0f, -6.0f, -12.0f, -24.0f, -48.0f })
        {
            const float y = inner.getY() + dbToY (dB) * inner.getHeight();
            g.drawText (dB == 0.0f ? "0" : juce::String ((int) dB),
                        (int) scaleCol.getX() + 2, (int) y - 5, (int) scaleCol.getWidth() - 2, 10,
                        juce::Justification::left);
        }

        g.setColour (juce::Colour (0xff8b94a5));
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.drawText ("OUT  L  R", labelRow.toNearestInt(), juce::Justification::centredLeft);
    }

private:
    static float dbToY (float dB) { return juce::jlimit (0.0f, 1.0f, dB / -60.0f); }

    void drawBar (juce::Graphics& g, juce::Rectangle<float> bar, float level, float hold)
    {
        g.setColour (juce::Colour (0xff1d2330));
        g.fillRoundedRectangle (bar, 3.0f);

        const float dB = juce::Decibels::gainToDecibels (level, -60.0f);
        const float top = bar.getY() + dbToY (dB) * bar.getHeight();
        juce::Rectangle<float> fill (bar.getX(), top, bar.getWidth(), bar.getBottom() - top);

        if (fill.getHeight() > 0.5f)
        {
            juce::ColourGradient grad (juce::Colour (0xff35d67c), bar.getX(), bar.getBottom(),
                                       juce::Colour (0xffff4d5a), bar.getX(), bar.getY(), false);
            grad.addColour (0.55, juce::Colour (0xffe8d44d));
            g.setGradientFill (grad);
            g.fillRoundedRectangle (fill, 3.0f);
        }

        const float holdDb = juce::Decibels::gainToDecibels (hold, -60.0f);
        if (holdDb > -59.0f)
        {
            const float hy = bar.getY() + dbToY (holdDb) * bar.getHeight();
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.fillRect (bar.getX(), hy, bar.getWidth(), 1.5f);
        }
    }

    float l = 0.0f, r = 0.0f, holdL = 0.0f, holdR = 0.0f;
    int ageL = 0, ageR = 0;
};

} // namespace vfui
