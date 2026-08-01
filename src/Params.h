#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// All parameter IDs in one place so the DSP and GUI never disagree.
namespace ParamID
{
    // Global
    inline constexpr auto inGain      = "inGain";
    inline constexpr auto outGain     = "outGain";

    // Gate
    inline constexpr auto gateThresh  = "gateThresh";

    // EQ
    inline constexpr auto hpfFreq     = "hpfFreq";
    inline constexpr auto cutFreq     = "cutFreq";
    inline constexpr auto cutGain     = "cutGain";
    inline constexpr auto cutQ        = "cutQ";
    inline constexpr auto harshGain   = "harshGain";
    inline constexpr auto airGain     = "airGain";

    // De-esser
    inline constexpr auto dsAmount    = "dsAmount";

    // Compressor
    inline constexpr auto compThresh  = "compThresh";
    inline constexpr auto compRatio   = "compRatio";
    inline constexpr auto compAttack  = "compAttack";
    inline constexpr auto compRelease = "compRelease";
    inline constexpr auto compMakeup  = "compMakeup";

    // Saturation
    inline constexpr auto satDrive    = "satDrive";

    // Parallel compression
    inline constexpr auto parAmount   = "parAmount";

    // Character engine
    inline constexpr auto charMode    = "charMode";   // Natural, Deep, Robot, Harmony, EDM, Dubstep
    inline constexpr auto charAmount  = "charAmount"; // dry/wet
    inline constexpr auto charTune    = "charTune";   // mode-specific pitch-ish control
    inline constexpr auto charColor   = "charColor";  // mode-specific tone/texture control

    // Tune / MIDI
    inline constexpr auto tuneOn      = "tuneOn";
    inline constexpr auto tuneKey     = "tuneKey";    // C..B
    inline constexpr auto tuneScale   = "tuneScale";  // Chromatic, Major, Minor
    inline constexpr auto tuneSpeed   = "tuneSpeed";
    inline constexpr auto tuneAmount  = "tuneAmount";
    inline constexpr auto midiFollow  = "midiFollow"; // held notes steer tune + harmony
    inline constexpr auto midiOut     = "midiOut";    // detected pitch emitted as MIDI

    // Space
    inline constexpr auto rvbMix      = "rvbMix";
    inline constexpr auto rvbSize     = "rvbSize";
    inline constexpr auto dlyMix      = "dlyMix";
    inline constexpr auto dlySync     = "dlySync";    // 1/8, 1/8 dotted, 1/4
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using P     = juce::AudioParameterFloat;
    using Pc    = juce::AudioParameterChoice;
    using Range = juce::NormalisableRange<float>;
    using Attrs = juce::AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto freqRange = [] (float lo, float hi) { Range r (lo, hi, 1.0f); r.setSkewForCentre (std::sqrt (lo * hi)); return r; };

    const auto hz    = Attrs().withStringFromValueFunction ([] (float v, int) {
                           return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz"
                                               : juce::String ((int) std::round (v)) + " Hz"; });
    const auto db    = Attrs().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; });
    const auto pct   = Attrs().withStringFromValueFunction ([] (float v, int) { return juce::String ((int) std::round (v)) + " %"; });
    const auto ms    = Attrs().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " ms"; });
    const auto ratio = Attrs().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + ":1"; });
    const auto semi  = Attrs().withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + juce::String (v, 1) + " st"; });
    const auto plain = Attrs().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2); });

    // Global
    params.push_back (std::make_unique<P> (juce::ParameterID { ParamID::inGain, 1 },  "Input Gain",  Range (-24.0f, 24.0f, 0.1f), 0.0f, db));
    params.push_back (std::make_unique<P> (juce::ParameterID { ParamID::outGain, 1 }, "Output Gain", Range (-24.0f, 24.0f, 0.1f), 0.0f, db));

    // Gate
    params.push_back (std::make_unique<P> (juce::ParameterID { ParamID::gateThresh, 1 }, "Gate", Range (-80.0f, -20.0f, 0.5f), -80.0f, db));

    // EQ
    params.push_back (std::make_unique<P> (juce::ParameterID { ParamID::hpfFreq, 1 },  "Low Cut",   freqRange (20.0f, 400.0f),    80.0f, hz));
    params.push_back (std::make_unique<P> (juce::ParameterID { ParamID::cutFreq, 1 },  "Cut Freq",  freqRange (100.0f, 8000.0f),  300.0f, hz));
    params.push_back (std::make_unique<P> (juce::ParameterID { ParamID::cutGain, 1 },  "Cut Depth", Range (-24.0f, 0.0f, 0.1f),   0.0f, db));
    params.push_back (std::make_unique<P> (juce::ParameterID { ParamID::cutQ, 1 },     "Cut Q",     Range (0.5f, 8.0f, 0.01f),    2.5f, plain));
    params.push_back (std::make_unique<P> (juce::ParameterID { ParamID::harshGain, 1 },"Harsh",     Range (-12.0f, 0.0f, 0.1f),   0.0f, db));
    params.push_back (std::make_unique<P> (juce::ParameterID { ParamID::airGain, 1 },  "Air",       Range (0.0f, 12.0f, 0.1f),    2.0f, db));

    // De-esser
    params.push_back (std::make_unique<P> (juce::ParameterID { ParamID::dsAmount, 1 }, "De-Ess", Range (0.0f, 100.0f, 1.0f), 30.0f, pct));

    // Compressor
    params.push_back (std::make_unique<P> (juce::ParameterID { ParamID::compThresh, 1 },  "Threshold", Range (-40.0f, 0.0f, 0.1f),  -18.0f, db));
    params.push_back (std::make_unique<P> (juce::ParameterID { ParamID::compRatio, 1 },   "Ratio",     Range (1.0f, 10.0f, 0.1f),   3.0f, ratio));
    params.push_back (std::make_unique<P> (juce::ParameterID { ParamID::compAttack, 1 },  "Attack",    Range (1.0f, 50.0f, 0.1f),   10.0f, ms));
    params.push_back (std::make_unique<P> (juce::ParameterID { ParamID::compRelease, 1 }, "Release",   Range (30.0f, 300.0f, 1.0f), 100.0f, ms));
    params.push_back (std::make_unique<P> (juce::ParameterID { ParamID::compMakeup, 1 },  "Makeup",    Range (0.0f, 18.0f, 0.1f),   3.0f, db));

    // Saturation
    params.push_back (std::make_unique<P> (juce::ParameterID { ParamID::satDrive, 1 }, "Saturation", Range (0.0f, 100.0f, 1.0f), 20.0f, pct));

    // Parallel compression
    params.push_back (std::make_unique<P> (juce::ParameterID { ParamID::parAmount, 1 }, "Parallel", Range (0.0f, 100.0f, 1.0f), 25.0f, pct));

    // Character
    params.push_back (std::make_unique<Pc> (juce::ParameterID { ParamID::charMode, 1 }, "Character",
                                            juce::StringArray { "Natural", "Deep", "Robot", "Harmony", "EDM", "Dubstep" }, 0));
    params.push_back (std::make_unique<P> (juce::ParameterID { ParamID::charAmount, 1 }, "Amount", Range (0.0f, 100.0f, 1.0f),  100.0f, pct));
    params.push_back (std::make_unique<P> (juce::ParameterID { ParamID::charTune, 1 },   "Tune",   Range (-12.0f, 12.0f, 0.1f), 0.0f, semi));
    params.push_back (std::make_unique<P> (juce::ParameterID { ParamID::charColor, 1 },  "Color",  Range (0.0f, 100.0f, 1.0f),  50.0f, pct));

    // Tune / MIDI
    using Pb = juce::AudioParameterBool;
    params.push_back (std::make_unique<Pb> (juce::ParameterID { ParamID::tuneOn, 1 }, "Tune", false));
    params.push_back (std::make_unique<Pc> (juce::ParameterID { ParamID::tuneKey, 1 }, "Key",
                                            juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));
    params.push_back (std::make_unique<Pc> (juce::ParameterID { ParamID::tuneScale, 1 }, "Scale",
                                            juce::StringArray { "Chromatic", "Major", "Minor" }, 1));
    params.push_back (std::make_unique<P> (juce::ParameterID { ParamID::tuneSpeed, 1 },  "Speed",  Range (0.0f, 100.0f, 1.0f), 70.0f, pct));
    params.push_back (std::make_unique<P> (juce::ParameterID { ParamID::tuneAmount, 1 }, "Amount", Range (0.0f, 100.0f, 1.0f), 100.0f, pct));
    params.push_back (std::make_unique<Pb> (juce::ParameterID { ParamID::midiFollow, 1 }, "MIDI Follow", false));
    params.push_back (std::make_unique<Pb> (juce::ParameterID { ParamID::midiOut, 1 },    "MIDI Out",    false));

    // Space
    params.push_back (std::make_unique<P> (juce::ParameterID { ParamID::rvbMix, 1 },  "Reverb",   Range (0.0f, 100.0f, 1.0f), 15.0f, pct));
    params.push_back (std::make_unique<P> (juce::ParameterID { ParamID::rvbSize, 1 }, "Size",     Range (0.0f, 100.0f, 1.0f), 40.0f, pct));
    params.push_back (std::make_unique<P> (juce::ParameterID { ParamID::dlyMix, 1 },  "Delay",    Range (0.0f, 100.0f, 1.0f), 0.0f, pct));
    params.push_back (std::make_unique<Pc> (juce::ParameterID { ParamID::dlySync, 1 },"Delay Time",
                                            juce::StringArray { "1/8", "1/8 Dotted", "1/4" }, 1));

    return { params.begin(), params.end() };
}
