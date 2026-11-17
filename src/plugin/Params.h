#pragma once

#include "../dsp/Engine.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace octaline::params
{

// Per-band parameter suffixes. Full id is "b<n>_<suffix>", n = 1..8.
inline constexpr const char* kOn      = "on";
inline constexpr const char* kHold    = "hold";
inline constexpr const char* kSource  = "src";
inline constexpr const char* kGroup   = "group";
inline constexpr const char* kSync    = "sync";
inline constexpr const char* kWave    = "wave";
inline constexpr const char* kInvert  = "rev";
inline constexpr const char* kTime    = "time";
inline constexpr const char* kNote    = "note";
inline constexpr const char* kLowCut  = "lcf";
inline constexpr const char* kHighCut = "hcf";
inline constexpr const char* kFeedback= "fb";
inline constexpr const char* kTap     = "tap";
inline constexpr const char* kSpeed   = "speed";
inline constexpr const char* kDepth   = "depth";
inline constexpr const char* kPan     = "pan";
inline constexpr const char* kLevel   = "level";

inline const juce::StringArray& bandSuffixes()
{
    static const juce::StringArray s { kOn, kHold, kSource, kGroup, kSync, kWave, kInvert, kTime, kNote,
                                       kLowCut, kHighCut, kFeedback, kTap, kSpeed, kDepth, kPan, kLevel };
    return s;
}

// Global parameter ids.
inline constexpr const char* kEffect    = "effect";
inline constexpr const char* kDirect    = "direct";
inline constexpr const char* kDirectPan = "dpan";
inline constexpr const char* kOutput    = "output";
inline constexpr const char* kBypass    = "bypass";

inline juce::String bandId (int band1, const char* suffix) { return "b" + juce::String (band1) + "_" + suffix; }

// Tempo-sync note values. Index 0 = free time in ms.
struct NoteValue { const char* name; double beats; };
inline const std::vector<NoteValue>& noteValues()
{
    static const std::vector<NoteValue> v {
        { "Free", 0.0 },
        { "1/1", 4.0 }, { "1/2 D", 3.0 }, { "1/2", 2.0 }, { "1/2 T", 4.0 / 3.0 },
        { "1/4 D", 1.5 }, { "1/4", 1.0 }, { "1/4 T", 2.0 / 3.0 },
        { "1/8 D", 0.75 }, { "1/8", 0.5 }, { "1/8 T", 1.0 / 3.0 },
        { "1/16 D", 0.375 }, { "1/16", 0.25 }, { "1/16 T", 1.0 / 6.0 },
        { "1/32", 0.125 },
    };
    return v;
}

inline juce::String formatPan (float v)
{
    if (std::abs (v) < 0.05f) return "C";
    return (v < 0 ? "L" : "R") + juce::String (std::abs (v), 1);
}

inline juce::String formatOffOr (float v, int decimals = 1)
{
    return v <= 0.0f ? juce::String ("OFF") : juce::String (v, decimals);
}

inline juce::String formatMs (float ms)
{
    if (ms >= 1000.0f) return juce::String (ms / 1000.0f, 3) + " s";
    if (ms >= 100.0f)  return juce::String (ms, 0) + " ms";
    if (ms >= 10.0f)   return juce::String (ms, 1) + " ms";
    return juce::String (ms, 2) + " ms";
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto tenth = [] (float v, int) { return String (v, 1); };
    auto knob = [] (float lo, float hi) { return NormalisableRange<float> (lo, hi, 0.1f); };

    for (int b = 1; b <= kNumBands; ++b)
    {
        const String prefix = "Band " + String (b) + " ";
        auto group = std::make_unique<AudioProcessorParameterGroup> ("band" + String (b), "Band " + String (b), "|");
        auto pid = [b] (const char* s) { return ParameterID { bandId (b, s), 1 }; };

        group->addChild (std::make_unique<AudioParameterBool> (pid (kOn), prefix + "On", b == 1));
        group->addChild (std::make_unique<AudioParameterBool> (pid (kHold), prefix + "Input Hold", false));

        StringArray sources { "IN" };
        for (int k = 1; k < kNumBands; ++k) sources.add ("Band " + String (k));
        group->addChild (std::make_unique<AudioParameterChoice> (pid (kSource), prefix + "Input", sources, 0));

        StringArray groupEnds { "None" };
        for (int k = 2; k <= kNumBands; ++k) groupEnds.add ("To " + String (k));
        group->addChild (std::make_unique<AudioParameterChoice> (pid (kGroup), prefix + "Group", groupEnds, 0));

        StringArray syncs { "Off" };
        for (int k = 1; k <= kNumBands; ++k) syncs.add ("Band " + String (k));
        group->addChild (std::make_unique<AudioParameterChoice> (pid (kSync), prefix + "Sync", syncs, 0));

        group->addChild (std::make_unique<AudioParameterChoice> (pid (kWave), prefix + "Wave",
                                                                 StringArray { "Sine", "Triangle", "Saw Up", "Saw Down" }, 0));
        group->addChild (std::make_unique<AudioParameterBool> (pid (kInvert), prefix + "Phase Reverse", false));

        NormalisableRange<float> timeRange (0.02f, Engine::kMaxTimeMs, 0.01f);
        timeRange.setSkewForCentre (350.0f);
        group->addChild (std::make_unique<AudioParameterFloat> (
            pid (kTime), prefix + "Time", timeRange, 80.0f * (float) b,
            AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction ([] (float v, int) { return formatMs (v); })));

        StringArray notes;
        for (auto& n : noteValues()) notes.add (n.name);
        group->addChild (std::make_unique<AudioParameterChoice> (pid (kNote), prefix + "Tempo Sync", notes, 0));

        auto offAttr = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return formatOffOr (v); });
        group->addChild (std::make_unique<AudioParameterFloat> (pid (kLowCut), prefix + "Low Cut", knob (0, 10), 0.0f, offAttr));
        group->addChild (std::make_unique<AudioParameterFloat> (pid (kHighCut), prefix + "High Cut", knob (0, 10), 0.0f, offAttr));

        auto tenthAttr = AudioParameterFloatAttributes().withStringFromValueFunction (tenth);
        group->addChild (std::make_unique<AudioParameterFloat> (pid (kFeedback), prefix + "Feedback", knob (0, 10), 3.0f, tenthAttr));
        group->addChild (std::make_unique<AudioParameterFloat> (
            pid (kTap), prefix + "Tap", knob (0, 100), 100.0f,
            AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return String (v, 1) + "%"; })));
        group->addChild (std::make_unique<AudioParameterFloat> (pid (kSpeed), prefix + "Speed", knob (0, 10), 0.0f, tenthAttr));
        group->addChild (std::make_unique<AudioParameterFloat> (pid (kDepth), prefix + "Depth", knob (0, 10), 0.0f, tenthAttr));
        group->addChild (std::make_unique<AudioParameterFloat> (
            pid (kPan), prefix + "Pan", knob (-10, 10), 0.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return formatPan (v); })));
        group->addChild (std::make_unique<AudioParameterFloat> (pid (kLevel), prefix + "Level", knob (0, 10), 10.0f, tenthAttr));

        layout.add (std::move (group));
    }

    auto tenthAttr = AudioParameterFloatAttributes().withStringFromValueFunction (tenth);
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { kEffect, 1 }, "Effect Level", knob (0, 10), 10.0f, tenthAttr));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { kDirect, 1 }, "Direct Level", knob (0, 10), 10.0f, tenthAttr));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { kDirectPan, 1 }, "Direct Pan", knob (-10, 10), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return formatPan (v); })));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { kOutput, 1 }, "Output", knob (-24, 12), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { kBypass, 1 }, "Bypass", false));

    return layout;
}

} // namespace octaline::params
