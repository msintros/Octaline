// Headless checks of the full plugin (processor, presets, state, editor).
// Usage: octaline_harness [screenshot-dir]

#include "../src/plugin/PluginProcessor.h"
#include "../src/plugin/PluginEditor.h"
#include "../src/plugin/Params.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>
#include <random>

using namespace octaline;

static int failures = 0;
#define EXPECT(cond, msg)                                              \
    do { if (! (cond)) { ++failures; std::printf ("  FAIL: %s (%s)\n", msg, #cond); } } while (0)

static float runBlocks (OctalineProcessor& p, int blocks, float noiseLevel = 0.2f)
{
    std::mt19937 rng (7);
    std::normal_distribution<float> g (0.0f, noiseLevel);
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    float peak = 0.0f;
    for (int b = 0; b < blocks; ++b)
    {
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
                buf.setSample (c, i, g (rng));
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
            {
                const float v = buf.getSample (c, i);
                if (! std::isfinite (v)) return INFINITY;
                peak = std::max (peak, std::abs (v));
            }
    }
    return peak;
}

static std::map<juce::String, float> snapshot (OctalineProcessor& p)
{
    std::map<juce::String, float> m;
    for (auto* param : p.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param))
            m[rp->paramID] = rp->getValue();
    return m;
}

static void savePng (juce::Component& c, const juce::File& file)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f);
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat().writeImageToStream (img, out);
    std::printf ("  wrote %s\n", file.getFullPathName().toRawUTF8());
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File shots = argc > 1 ? juce::File (juce::String (argv[1])) : juce::File();

    auto proc = std::make_unique<OctalineProcessor>();
    proc->setPlayConfigDetails (2, 2, 48000.0, 512);
    proc->prepareToPlay (48000.0, 512);

    std::printf ("parameters: %d, factory programs: %d\n", (int) proc->getParameters().size(), proc->getNumPrograms());
    EXPECT (proc->getParameters().size() == 8 * 17 + 5, "parameter count");
    EXPECT (proc->getNumPrograms() == 87, "factory program count");

    EXPECT (runBlocks (*proc, 200) < 10.0f, "default settings stay bounded");

    // Every factory program loads and runs cleanly.
    for (int i = 0; i < proc->getNumPrograms(); ++i)
    {
        proc->setCurrentProgram (i);
        EXPECT (proc->getPresets().getCurrentName() == proc->getProgramName (i), "program name tracks");
        const float peak = runBlocks (*proc, 120);
        if (! (peak < 30.0f))
            std::printf ("  %s peak %f\n", proc->getProgramName (i).toRawUTF8(), peak);
        EXPECT (peak < 30.0f, "factory program bounded");
        EXPECT (! proc->getPresets().isDirty(), "loading a preset is not an edit");
    }

    // Session state round trip (what the DAW saves with a project).
    proc->setCurrentProgram (60);
    if (auto* t = proc->getState().getParameter ("b3_time")) t->setValueNotifyingHost (0.42f);
    EXPECT (proc->getPresets().isDirty(), "edit marks preset dirty");
    juce::MemoryBlock block;
    proc->getStateInformation (block);
    const auto before = snapshot (*proc);

    auto proc2 = std::make_unique<OctalineProcessor>();
    proc2->setStateInformation (block.getData(), (int) block.getSize());
    EXPECT (snapshot (*proc2) == before, "state restores every parameter");
    EXPECT (proc2->getPresets().getCurrentName() == proc->getPresets().getCurrentName(), "state restores preset name");
    EXPECT (proc2->getPresets().isDirty(), "state restores edited flag");

    // User preset save / load / delete.
    auto& pm = proc->getPresets();
    const auto err = pm.saveAs ("Harness Test");
    EXPECT (err.isEmpty(), "save as");
    EXPECT (PresetManager::userFolder().getChildFile ("Harness Test.octpreset").existsAsFile(), "preset file written");
    const auto saved = snapshot (*proc);
    if (auto* out = proc->getState().getParameter ("output")) out->setValueNotifyingHost (0.9f);
    proc->setCurrentProgram (3);
    EXPECT (snapshot (*proc) != saved, "another preset changes settings");
    int idx = -1;
    pm.refresh();
    for (int i = 0; i < pm.getEntries().size(); ++i)
        if (pm.getEntries()[i].name == "Harness Test") idx = i;
    EXPECT (idx >= 0 && pm.load (idx), "user preset listed and loads");
    auto after = snapshot (*proc);
    after.erase ("output");
    auto savedNoOut = saved;
    savedNoOut.erase ("output");
    EXPECT (after == savedNoOut, "user preset restores settings (output excluded)");
    EXPECT (pm.isCurrentUserPreset() && pm.deleteCurrent().isEmpty(), "delete user preset");
    EXPECT (! PresetManager::userFolder().getChildFile ("Harness Test.octpreset").existsAsFile(), "preset file removed");

    // Mono input layout.
    juce::AudioProcessor::BusesLayout mono;
    mono.inputBuses.add (juce::AudioChannelSet::mono());
    mono.outputBuses.add (juce::AudioChannelSet::stereo());
    EXPECT (proc->checkBusesLayoutSupported (mono), "mono in, stereo out supported");

    // Editor opens; optionally render screenshots.
    for (const char* name : { "921 16-Beat", "011 Chorus 1", "432 Multi Tap Ping-Pong Delay" })
    {
        for (int i = 0; i < proc->getNumPrograms(); ++i)
            if (proc->getProgramName (i) == name) proc->setCurrentProgram (i);
        runBlocks (*proc, 4);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
        EXPECT (ed != nullptr && ed->getWidth() > 1000, "editor created");
        if (shots != juce::File() && ed != nullptr)
        {
            shots.createDirectory();
            ed->setVisible (true);
            ed->resized();
            savePng (*ed, shots.getChildFile (juce::String (name).substring (0, 3) + ".png"));
        }
    }

    proc->releaseResources();
    std::printf (failures == 0 ? "harness: all checks passed\n" : "harness: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
