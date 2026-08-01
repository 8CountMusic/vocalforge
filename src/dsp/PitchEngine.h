#pragma once
#include "Helpers.h"
#include "signalsmith-stretch/signalsmith-stretch.h"
#include <set>

namespace vf
{

// ---------------------------------------------------------------------------
// Real-time monophonic pitch detector (YIN, on 2x-decimated audio).
// ---------------------------------------------------------------------------
struct YinDetector
{
    static constexpr int windowSize = 600;   // at sr/2
    static constexpr int maxTau     = 300;   // fmin ~ (sr/2)/300  (~73 Hz at 44.1k)
    static constexpr int hopSize    = 256;   // at sr/2  (~23 ms at 44.1k)

    void prepare (double sampleRate)
    {
        sr2 = sampleRate * 0.5;
        ring.assign (ringSize, 0.0f);
        writePos = 0; hopCount = 0; haveSample = false;
        currentHz = 0.0f; voiced = false;
    }

    // Push full-rate mono samples.
    void push (const float* x, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            if (! haveSample) { pending = x[i]; haveSample = true; continue; }
            const float s = (pending + x[i]) * 0.5f; // decimate by 2
            haveSample = false;

            ring[(size_t) writePos] = s;
            writePos = (writePos + 1) % ringSize;
            if (++hopCount >= hopSize)
            {
                hopCount = 0;
                analyse();
            }
        }
    }

    float getHz() const   { return currentHz; }
    bool  isVoiced() const { return voiced; }

private:
    static constexpr int ringSize = 2048; // > windowSize + maxTau

    void analyse()
    {
        // Copy the newest (windowSize + maxTau) samples into a linear scratch.
        const int needed = windowSize + maxTau;
        int start = writePos - needed;
        while (start < 0) start += ringSize;
        for (int i = 0; i < needed; ++i)
            scratch[(size_t) i] = ring[(size_t) ((start + i) % ringSize)];

        // Quick energy gate
        float energy = 0.0f;
        for (int i = 0; i < windowSize; ++i)
            energy += scratch[(size_t) i] * scratch[(size_t) i];
        if (energy < 1.0e-5f) { voiced = false; return; }

        // YIN difference + cumulative mean normalised difference
        float running = 0.0f;
        int bestTau = -1;
        float bestVal = 1.0f;
        const int minTau = (int) (sr2 / 1000.0); // fmax 1 kHz

        for (int tau = 1; tau < maxTau; ++tau)
        {
            float d = 0.0f;
            for (int i = 0; i < windowSize; ++i)
            {
                const float diff = scratch[(size_t) i] - scratch[(size_t) (i + tau)];
                d += diff * diff;
            }
            running += d;
            const float cmnd = running > 0.0f ? d * (float) tau / running : 1.0f;
            cmndBuf[(size_t) tau] = cmnd;

            if (tau >= minTau)
            {
                if (cmnd < 0.12f) // first dip below threshold wins
                {
                    // walk to the local minimum
                    int t = tau;
                    while (t + 1 < maxTau)
                    {
                        float dn = 0.0f;
                        for (int i = 0; i < windowSize; ++i)
                        {
                            const float diff = scratch[(size_t) i] - scratch[(size_t) (i + t + 1)];
                            dn += diff * diff;
                        }
                        running += dn;
                        const float cn = dn * (float) (t + 1) / running;
                        cmndBuf[(size_t) (t + 1)] = cn;
                        if (cn < cmndBuf[(size_t) t]) ++t; else break;
                    }
                    bestTau = t; bestVal = cmndBuf[(size_t) t];
                    break;
                }
                if (cmnd < bestVal) { bestVal = cmnd; bestTau = tau; }
            }
        }

        if (bestTau <= 0 || bestVal > 0.35f) { voiced = false; return; }

        // Parabolic refinement
        float tauF = (float) bestTau;
        if (bestTau > 1 && bestTau < maxTau - 1)
        {
            const float a = cmndBuf[(size_t) (bestTau - 1)];
            const float b = cmndBuf[(size_t) bestTau];
            const float c = cmndBuf[(size_t) (bestTau + 1)];
            const float denom = a - 2.0f * b + c;
            if (std::abs (denom) > 1.0e-9f)
                tauF += 0.5f * (a - c) / denom;
        }

        currentHz = (float) (sr2 / (double) tauF);
        voiced = currentHz > 60.0f && currentHz < 1100.0f;
    }

    double sr2 = 22050.0;
    std::vector<float> ring;
    std::array<float, windowSize + maxTau> scratch {};
    std::array<float, maxTau + 1> cmndBuf {};
    int writePos = 0, hopCount = 0;
    float pending = 0.0f;
    bool haveSample = false;
    float currentHz = 0.0f;
    bool voiced = false;
};

// ---------------------------------------------------------------------------
// Scale helpers
// ---------------------------------------------------------------------------
inline float hzToMidi (float hz)   { return 69.0f + 12.0f * std::log2 (hz / 440.0f); }
inline float midiToHz (float midi) { return 440.0f * std::pow (2.0f, (midi - 69.0f) / 12.0f); }

// scale: 0 = chromatic, 1 = major, 2 = minor. key: 0..11 (C..B)
inline float quantizeToScale (float midiNote, int key, int scale)
{
    static const bool majorMask[12] = { 1,0,1,0,1,1,0,1,0,1,0,1 };
    static const bool minorMask[12] = { 1,0,1,1,0,1,0,1,1,0,1,0 };

    if (scale == 0)
        return std::round (midiNote);

    const bool* mask = scale == 1 ? majorMask : minorMask;
    float best = std::round (midiNote);
    float bestDist = 1.0e9f;
    const int base = (int) std::round (midiNote);
    for (int cand = base - 6; cand <= base + 6; ++cand)
    {
        int deg = (cand - key) % 12; if (deg < 0) deg += 12;
        if (! mask[deg]) continue;
        const float dist = std::abs ((float) cand - midiNote);
        if (dist < bestDist) { bestDist = dist; best = (float) cand; }
    }
    return best;
}

// ---------------------------------------------------------------------------
// The autotune module: detector-driven pitch correction via Signalsmith Stretch.
// ---------------------------------------------------------------------------
struct AutoTune
{
    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        numChannels = (int) spec.numChannels;
        stretch.presetCheaper (numChannels, (float) sampleRate);
        latencySamples = stretch.inputLatency() + stretch.outputLatency();
        inBuf.setSize (numChannels, (int) spec.maximumBlockSize);
        inPtrs.resize ((size_t) numChannels);
        outPtrs.resize ((size_t) numChannels);
        reset();
    }

    int getLatencySamples() const { return latencySamples; }

    void setParams (bool onIn, int keyIn, int scaleIn, float speed01In, float amount01In, bool midiFollowIn)
    {
        if (onIn != on) { on = onIn; if (on) { stretch.reset(); smoothedRatio = 1.0f; } }
        key = keyIn; scale = scaleIn; speed01 = speed01In; amount01 = amount01In; midiFollow = midiFollowIn;
    }

    void setHeldNotes (const std::vector<int>& notes) { heldNotes = notes; }

    // Returns the target Hz it is pulling toward (0 if idle) for the UI.
    float process (juce::AudioBuffer<float>& buffer, float detectedHz, bool voiced)
    {
        if (! on) return 0.0f;

        const int numCh = juce::jmin (numChannels, buffer.getNumChannels());
        const int n = buffer.getNumSamples();

        float targetHz = 0.0f;
        float corrSemis = 0.0f;

        if (voiced && detectedHz > 0.0f)
        {
            const float midiNote = hzToMidi (detectedHz);
            float targetNote;

            if (midiFollow && ! heldNotes.empty())
            {
                // Snap to the nearest held MIDI note
                float bestDist = 1.0e9f; targetNote = midiNote;
                for (int held : heldNotes)
                {
                    const float dist = std::abs ((float) held - midiNote);
                    if (dist < bestDist) { bestDist = dist; targetNote = (float) held; }
                }
            }
            else
            {
                targetNote = quantizeToScale (midiNote, key, scale);
            }

            corrSemis = juce::jlimit (-12.0f, 12.0f, (targetNote - midiNote)) * amount01;
            targetHz = midiToHz (targetNote);
        }

        // Smooth the correction: speed 100 % = instant robot snap, 0 % = gentle drift.
        const float targetRatio = std::pow (2.0f, corrSemis / 12.0f);
        const float tau = voiced ? 0.25f * std::pow (0.004f, speed01) : 0.05f;
        const float alpha = 1.0f - std::exp (-(float) n / ((float) sampleRate * tau));
        smoothedRatio += (targetRatio - smoothedRatio) * alpha;

        // Re-pitch in place through the stretch engine.
        inBuf.setSize (numCh, n, false, false, true);
        for (int ch = 0; ch < numCh; ++ch)
            inBuf.copyFrom (ch, 0, buffer, ch, 0, n);

        stretch.setTransposeFactor (smoothedRatio, 8000.0f / (float) sampleRate);
        stretch.setFormantSemitones (0.0f, false);

        for (int ch = 0; ch < numCh; ++ch)
        {
            inPtrs[(size_t) ch]  = inBuf.getReadPointer (ch);
            outPtrs[(size_t) ch] = buffer.getWritePointer (ch);
        }
        stretch.process (inPtrs.data(), n, outPtrs.data(), n);

        return targetHz;
    }

    void reset()
    {
        stretch.reset();
        smoothedRatio = 1.0f;
        heldNotes.clear();
    }

    bool isOn() const { return on; }

private:
    double sampleRate = 44100.0;
    int numChannels = 2, latencySamples = 0;
    bool on = false, midiFollow = false;
    int key = 0, scale = 1;
    float speed01 = 0.7f, amount01 = 1.0f;
    float smoothedRatio = 1.0f;

    signalsmith::stretch::SignalsmithStretch<float> stretch;
    juce::AudioBuffer<float> inBuf;
    std::vector<const float*> inPtrs;
    std::vector<float*> outPtrs;
    std::vector<int> heldNotes;
};

// ---------------------------------------------------------------------------
// Turns the detected vocal pitch into MIDI notes (for driving other plugins).
// ---------------------------------------------------------------------------
struct MidiNoteTracker
{
    void process (juce::MidiBuffer& out, float detectedHz, bool voiced, float level)
    {
        const int note = voiced && detectedHz > 0.0f
                             ? juce::jlimit (0, 127, (int) std::round (hzToMidi (detectedHz)))
                             : -1;

        if (note != pendingNote) { pendingNote = note; stableBlocks = 0; }
        else ++stableBlocks;

        if (stableBlocks >= 2 && pendingNote != currentNote)
        {
            if (currentNote >= 0)
                out.addEvent (juce::MidiMessage::noteOff (1, currentNote), 0);
            if (pendingNote >= 0)
            {
                const int vel = juce::jlimit (30, 127, (int) (level * 380.0f));
                out.addEvent (juce::MidiMessage::noteOn (1, pendingNote, (juce::uint8) vel), 0);
            }
            currentNote = pendingNote;
        }
    }

    void allNotesOff (juce::MidiBuffer& out)
    {
        if (currentNote >= 0)
        {
            out.addEvent (juce::MidiMessage::noteOff (1, currentNote), 0);
            currentNote = -1; pendingNote = -1; stableBlocks = 0;
        }
    }

    int currentNote = -1, pendingNote = -1, stableBlocks = 0;
};

} // namespace vf
