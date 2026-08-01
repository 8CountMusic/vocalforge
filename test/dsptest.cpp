// Offline audio verification for the character modes.
// Renders a synthetic "vocal" and checks that each mode audibly transforms it.
#include "../src/dsp/CharacterEngine.h"
#include <cstdio>

using namespace vf;

static juce::AudioBuffer<float> makeVocal (double sr, int seconds)
{
    const int n = (int) sr * seconds;
    juce::AudioBuffer<float> buf (2, n);
    buf.clear();

    // Saw at 200 Hz with word-like amplitude envelope (0.4 s on, 0.2 s off), softened.
    double phase = 0.0;
    float lpState = 0.0f;
    const float lpCoef = 0.25f;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double cycle = std::fmod (t, 0.6);
        const float envOn = cycle < 0.4 ? 1.0f : 0.0f;
        const float ramp = (float) juce::jmin (1.0, juce::jmin (cycle, 0.4 - juce::jmin (cycle, 0.39)) * 50.0);
        phase += 200.0 / sr; if (phase >= 1.0) phase -= 1.0;
        float s = (float) (2.0 * phase - 1.0) * 0.4f * envOn * ramp;
        lpState += (s - lpState) * lpCoef; // tame the top end a bit
        buf.setSample (0, i, lpState);
        buf.setSample (1, i, lpState);
    }
    return buf;
}

static juce::AudioBuffer<float> renderMode (const juce::AudioBuffer<float>& input, int mode, double sr, int blockSize)
{
    CharacterEngine engine;
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) blockSize, 2 };
    engine.prepare (spec);
    engine.setParams (mode, 1.0f, 0.0f, 60.0f);
    engine.setCarrierHz ({ 150.0f });

    juce::AudioBuffer<float> out (2, input.getNumSamples());
    juce::AudioBuffer<float> block (2, blockSize);
    for (int pos = 0; pos < input.getNumSamples(); pos += blockSize)
    {
        const int len = juce::jmin (blockSize, input.getNumSamples() - pos);
        block.setSize (2, len, false, false, true);
        for (int ch = 0; ch < 2; ++ch)
            block.copyFrom (ch, 0, input, ch, pos, len);
        engine.process (block, 150.0);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, pos, block, ch, 0, len);
    }
    return out;
}

static float rmsRange (const juce::AudioBuffer<float>& b, int start, int end)
{
    double sum = 0.0; int count = 0;
    for (int i = start; i < end && i < b.getNumSamples(); ++i)
    {
        const float s = b.getSample (0, i);
        sum += (double) s * s; ++count;
    }
    return count > 0 ? (float) std::sqrt (sum / count) : 0.0f;
}

static bool hasBadSamples (const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float s = b.getSample (ch, i);
            if (! std::isfinite (s) || std::abs (s) > 2.0f) return true;
        }
    return false;
}

int main()
{
    const double sr = 44100.0;
    const int blockSize = 512;
    int fails = 0;

    auto vocal = makeVocal (sr, 3);
    auto natural = renderMode (vocal, 0, sr, blockSize); // aligned dry reference
    auto robot   = renderMode (vocal, 2, sr, blockSize);
    auto grit    = renderMode (vocal, 5, sr, blockSize);

    // Sanity: no NaNs or blowups anywhere
    for (auto* b : { &natural, &robot, &grit })
        if (hasBadSamples (*b)) { printf ("FAIL: bad samples\n"); ++fails; }

    // Analyse the second half (past all latency/warm-up)
    const int a = (int) sr * 1, z = (int) sr * 3;
    const float rmsNat   = rmsRange (natural, a, z);
    const float rmsRobot = rmsRange (robot, a, z);
    const float rmsGrit  = rmsRange (grit, a, z);
    printf ("RMS  natural=%.4f  robot=%.4f  grit=%.4f\n", rmsNat, rmsRobot, rmsGrit);

    // Robot: audible and level-matched to the source vocal
    if (rmsRobot < rmsNat * 0.3f || rmsRobot > rmsNat * 2.0f) { printf ("FAIL: vocoder level off\n"); ++fails; }

    // Robot gate: gap regions must be much quieter than voiced regions.
    // Words repeat every 0.6 s: ON [0, 0.4), OFF [0.4, 0.6). Account for engine latency.
    CharacterEngine probe;
    {
        juce::dsp::ProcessSpec spec { sr, (juce::uint32) blockSize, 2 };
        probe.prepare (spec);
    }
    const double Loff = probe.getLatencySamples() / sr;
    printf ("Engine latency: %.0f samples (%.3f s)\n", (double) probe.getLatencySamples(), Loff);
    const float voicedRms = rmsRange (robot, (int)(sr * (1.30 + Loff)), (int)(sr * (1.45 + Loff)));
    const float gapRms    = rmsRange (robot, (int)(sr * (1.68 + Loff)), (int)(sr * (1.78 + Loff)));
    printf ("Vocoder voiced=%.4f gap=%.4f\n", voicedRms, gapRms);
    if (gapRms > voicedRms * 0.25f) { printf ("FAIL: vocoder gate leaking\n"); ++fails; }

    // Grit: must differ AUDIBLY from natural (relative difference of the waveforms)
    double diffSum = 0.0, refSum = 0.0;
    for (int i = a; i < z; ++i)
    {
        const float d = grit.getSample (0, i) - natural.getSample (0, i);
        diffSum += (double) d * d;
        refSum  += (double) natural.getSample (0, i) * natural.getSample (0, i);
    }
    const float relDiff = (float) std::sqrt (diffSum / juce::jmax (1.0e-12, refSum));
    printf ("Grit relative difference vs natural: %.2f\n", relDiff);
    if (relDiff < 0.25f) { printf ("FAIL: grit too subtle\n"); ++fails; }

    // Grit level shouldn't explode or vanish
    if (rmsGrit < rmsNat * 0.4f || rmsGrit > rmsNat * 2.5f) { printf ("FAIL: grit level off\n"); ++fails; }

    printf (fails ? "TESTS FAILED (%d)\n" : "ALL AUDIO TESTS PASS\n", fails);
    return fails;
}
