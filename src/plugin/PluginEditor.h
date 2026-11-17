#pragma once

#include "LookAndFeel.h"
#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace octaline
{

// A rotary knob with a caption above and an editable value below.
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& caption,
          const juce::String& tooltip = {});

    void resized() override;
    void setAccent (juce::Colour c);
    juce::Slider& getSlider() noexcept { return slider; }

private:
    juce::Slider slider;
    juce::Label label;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

// A small caption + combo box row.
class Choice : public juce::Component
{
public:
    Choice (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& caption,
            const juce::StringArray& items, const juce::String& tooltip = {});

    void resized() override;
    juce::ComboBox& getCombo() noexcept { return combo; }

private:
    juce::Label label;
    juce::ComboBox combo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

class BandStrip : public juce::Component
{
public:
    BandStrip (OctalineProcessor& p, int band1);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void refresh (const std::array<BandRole, kNumBands>& roles); // called from the editor's timer

private:
    void tap();
    void copyBand();
    void pasteBand();
    void setParam (int band1, const char* suffix, float realValue);
    float getParam (int band1, const char* suffix) const;

    OctalineProcessor& processor;
    juce::AudioProcessorValueTreeState& state;
    const int band;
    juce::Colour accent;
    juce::String status;

    juce::TextButton onButton { "ON" }, holdButton { "HOLD" }, tapButton { "TAP" }, phaseButton { "PHASE REV" };
    juce::TextButton copyButton { "Copy" }, pasteButton { "Paste" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttach, holdAttach, phaseAttach;

    Choice source, group, sync, wave, note;
    Knob time, lowCut, highCut, feedback, tapKnob, speed, depth, pan, level;

    std::vector<double> tapTimes;
};

class OctalineEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit OctalineEditor (OctalineProcessor&);
    ~OctalineEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showPresetMenu();
    void saveAs();
    void save();
    void deletePreset();
    void showError (const juce::String& message);

    OctalineProcessor& processor;
    OctalineLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    juce::TextButton prevButton { "<" }, nextButton { ">" }, presetButton, saveButton { "Save" },
        saveAsButton { "Save As" }, deleteButton { "Delete" }, bypassButton { "BYPASS" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttach;
    Knob effect, direct, directPan, output;
    juce::Label bpmLabel;

    juce::OwnedArray<BandStrip> strips;
    std::unique_ptr<juce::AlertWindow> dialog;
};

} // namespace octaline
