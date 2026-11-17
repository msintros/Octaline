#include "PresetManager.h"
#include "Params.h"

namespace octaline
{

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state, std::function<void()> onPresetLoaded)
    : apvts (state), presetLoaded (std::move (onPresetLoaded))
{
    for (auto* p : apvts.processor.getParameters())
        if (auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            apvts.addParameterListener (wp->paramID, this);
    refresh();
}

PresetManager::~PresetManager()
{
    for (auto* p : apvts.processor.getParameters())
        if (auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            apvts.removeParameterListener (wp->paramID, this);
}

juce::File PresetManager::userFolder()
{
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Presets/Octaline");
   #elif JUCE_WINDOWS
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Octaline/Presets");
   #else
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".config/Octaline/Presets");
   #endif
}

void PresetManager::refresh()
{
    const auto previousName = currentName;
    const bool previousFactory = currentIndex >= 0 && currentIndex < entries.size() && entries[currentIndex].factory;

    entries.clear();
    const auto& factory = factoryPresets();
    for (int i = 0; i < (int) factory.size(); ++i)
        entries.add ({ factory[(size_t) i].name, true, i, {} });

    auto folder = userFolder();
    if (folder.isDirectory())
    {
        auto files = folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + kExtension);
        files.sort();
        for (auto& f : files)
            entries.add ({ f.getFileNameWithoutExtension(), false, -1, f });
    }

    currentIndex = -1;
    for (int i = 0; i < entries.size(); ++i)
        if (entries[i].name == previousName && (entries[i].factory == previousFactory || currentIndex < 0))
            currentIndex = i;
}

bool PresetManager::isCurrentUserPreset() const
{
    return currentIndex >= 0 && currentIndex < entries.size() && ! entries[currentIndex].factory;
}

bool PresetManager::isSessionParam (const juce::String& id)
{
    return id == params::kOutput || id == params::kBypass;
}

void PresetManager::setParam (const juce::String& id, float realValue)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (realValue));
}

void PresetManager::finishLoad (const juce::String& name, int index)
{
    currentName = name;
    currentIndex = index;
    loading = false;
    dirty = false;
    if (presetLoaded)
        presetLoaded();
}

void PresetManager::applyEngineParams (const EngineParams& e)
{
    using namespace params;
    for (int b = 1; b <= kNumBands; ++b)
    {
        const auto& p = e.band[(size_t) b - 1];
        setParam (bandId (b, kOn), p.on ? 1.0f : 0.0f);
        setParam (bandId (b, kHold), p.inputHold ? 1.0f : 0.0f);
        setParam (bandId (b, kSource), (float) juce::jlimit (0, b - 1, p.source));
        setParam (bandId (b, kGroup), p.groupEnd > b ? (float) (p.groupEnd - 1) : 0.0f);
        setParam (bandId (b, kSync), (float) juce::jlimit (0, kNumBands, p.sync));
        setParam (bandId (b, kWave), (float) (int) p.wave);
        setParam (bandId (b, kInvert), p.invert ? 1.0f : 0.0f);
        setParam (bandId (b, kTime), p.timeMs);
        setParam (bandId (b, kNote), 0.0f);
        setParam (bandId (b, kLowCut), p.lowCut);
        setParam (bandId (b, kHighCut), p.highCut);
        setParam (bandId (b, kFeedback), p.feedback);
        setParam (bandId (b, kTap), p.tapPercent);
        setParam (bandId (b, kSpeed), p.speed);
        setParam (bandId (b, kDepth), p.depth);
        setParam (bandId (b, kPan), p.pan);
        setParam (bandId (b, kLevel), p.level);
    }
    setParam (kEffect, e.global.effectLevel);
    setParam (kDirect, e.global.directLevel);
    setParam (kDirectPan, e.global.directPan);
}

std::unique_ptr<juce::XmlElement> PresetManager::createPresetXml (const juce::String& name) const
{
    auto xml = std::make_unique<juce::XmlElement> ("OctalinePreset");
    xml->setAttribute ("name", name);
    xml->setAttribute ("version", 1);
    for (auto* p : apvts.processor.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (rp == nullptr || isSessionParam (rp->paramID))
            continue;
        auto* e = xml->createNewChildElement ("PARAM");
        e->setAttribute ("id", rp->paramID);
        e->setAttribute ("value", (double) rp->convertFrom0to1 (rp->getValue()));
    }
    return xml;
}

void PresetManager::applyPresetXml (const juce::XmlElement& xml)
{
    std::map<juce::String, float> values;
    for (auto* e : xml.getChildWithTagNameIterator ("PARAM"))
        values[e->getStringAttribute ("id")] = (float) e->getDoubleAttribute ("value");

    for (auto* p : apvts.processor.getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (rp == nullptr || isSessionParam (rp->paramID))
            continue;
        auto it = values.find (rp->paramID);
        rp->setValueNotifyingHost (it != values.end() ? rp->convertTo0to1 (it->second) : rp->getDefaultValue());
    }
}

bool PresetManager::load (int index)
{
    if (index < 0 || index >= entries.size())
        return false;

    const auto entry = entries[index];
    loading = true;

    if (entry.factory)
    {
        applyEngineParams (factoryPresets()[(size_t) entry.factoryIndex].params);
    }
    else
    {
        auto xml = juce::XmlDocument::parse (entry.file);
        if (xml == nullptr || ! xml->hasTagName ("OctalinePreset"))
        {
            loading = false;
            return false;
        }
        applyPresetXml (*xml);
    }

    finishLoad (entry.name, index);
    return true;
}

void PresetManager::loadNext()
{
    if (entries.isEmpty()) return;
    load ((currentIndex + 1 + entries.size()) % entries.size());
}

void PresetManager::loadPrevious()
{
    if (entries.isEmpty()) return;
    load (currentIndex < 0 ? entries.size() - 1 : (currentIndex - 1 + entries.size()) % entries.size());
}

void PresetManager::loadFactory (int factoryIndex)
{
    for (int i = 0; i < entries.size(); ++i)
        if (entries[i].factory && entries[i].factoryIndex == factoryIndex)
        {
            load (i);
            return;
        }
}

void PresetManager::loadInit()
{
    loading = true;
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! isSessionParam (rp->paramID))
                rp->setValueNotifyingHost (rp->getDefaultValue());
    finishLoad ("Init", -1);
}

juce::String PresetManager::saveAs (const juce::String& rawName)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty())
        return "Please enter a name.";

    auto folder = userFolder();
    if (! folder.createDirectory())
        return "Could not create the preset folder:\n" + folder.getFullPathName();

    auto file = folder.getChildFile (name + kExtension);
    auto xml = createPresetXml (name);
    if (! xml->writeTo (file))
        return "Could not write " + file.getFullPathName();

    refresh();
    for (int i = 0; i < entries.size(); ++i)
        if (! entries[i].factory && entries[i].file == file)
            currentIndex = i;
    currentName = name;
    dirty = false;
    return {};
}

juce::String PresetManager::saveCurrent()
{
    if (! isCurrentUserPreset())
        return "Factory presets can't be overwritten. Use Save As to make your own copy.";
    return saveAs (entries[currentIndex].name);
}

juce::String PresetManager::deleteCurrent()
{
    if (! isCurrentUserPreset())
        return "Only your own presets can be deleted.";
    const auto file = entries[currentIndex].file;
    if (! file.moveToTrash() && ! file.deleteFile())
        return "Could not delete " + file.getFullPathName();
    currentName = currentName + " (deleted)";
    refresh();
    currentIndex = -1;
    return {};
}

void PresetManager::writeToState (juce::ValueTree& state) const
{
    state.setProperty ("presetName", currentName, nullptr);
    state.setProperty ("presetDirty", dirty.load(), nullptr);
}

void PresetManager::readFromState (const juce::ValueTree& state)
{
    currentName = state.getProperty ("presetName", "Init").toString();
    refresh();
    // Restoring a session sets every parameter; that is not a user edit.
    dirty = (bool) state.getProperty ("presetDirty", false);
}

void PresetManager::parameterChanged (const juce::String& id, float)
{
    if (! loading && ! isSessionParam (id))
        dirty = true;
}

} // namespace octaline
