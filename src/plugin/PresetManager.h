#pragma once

#include "FactoryPresets.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>

namespace octaline
{

// Factory and user presets. Lives on the message thread; the processor owns it.
//
// User presets are small XML files (*.octpreset) in a folder you can open from
// the plugin, so they are easy to back up or copy to another computer:
//   macOS:   ~/Library/Audio/Presets/Octaline
//   Windows: Documents\Octaline\Presets
//   Linux:   ~/.config/Octaline/Presets
class PresetManager : private juce::AudioProcessorValueTreeState::Listener
{
public:
    struct Entry
    {
        juce::String name;
        bool factory = false;
        int factoryIndex = -1;
        juce::File file;
    };

    static constexpr const char* kExtension = ".octpreset";

    PresetManager (juce::AudioProcessorValueTreeState& state, std::function<void()> onPresetLoaded);
    ~PresetManager() override;

    static juce::File userFolder();

    void refresh();
    const juce::Array<Entry>& getEntries() const noexcept { return entries; }

    bool load (int entryIndex);
    void loadNext();
    void loadPrevious();
    void loadFactory (int factoryIndex);
    void loadInit();

    // Returns an error message, or an empty string on success.
    juce::String saveAs (const juce::String& name);
    juce::String saveCurrent();
    juce::String deleteCurrent();

    int getCurrentIndex() const noexcept { return currentIndex; }
    juce::String getCurrentName() const { return currentName; }
    bool isCurrentUserPreset() const;
    bool isDirty() const noexcept { return dirty.load(); }

    // Session state (stored with the DAW project).
    void writeToState (juce::ValueTree& state) const;
    void readFromState (const juce::ValueTree& state);

    // Parameter snapshot as XML, without per-session controls (output, bypass).
    std::unique_ptr<juce::XmlElement> createPresetXml (const juce::String& name) const;
    void applyPresetXml (const juce::XmlElement& xml);
    void applyEngineParams (const EngineParams& p);

    // Wrap a session restore so it does not count as an edit.
    void setRestoring (bool r) noexcept { loading = r; }

private:
    void parameterChanged (const juce::String&, float) override;
    void setParam (const juce::String& id, float realValue);
    void finishLoad (const juce::String& name, int index);
    static bool isSessionParam (const juce::String& id);

    juce::AudioProcessorValueTreeState& apvts;
    std::function<void()> presetLoaded;
    juce::Array<Entry> entries;
    int currentIndex = -1;
    juce::String currentName { "Init" };
    std::atomic<bool> dirty { false };
    std::atomic<bool> loading { false };
};

} // namespace octaline
