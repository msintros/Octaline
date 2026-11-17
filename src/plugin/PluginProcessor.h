#pragma once

#include "../dsp/Engine.h"
#include "PresetManager.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace octaline
{

class OctalineProcessor : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    OctalineProcessor();
    ~OctalineProcessor() override { cancelPendingUpdate(); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }
    PresetManager& getPresets() noexcept { return presets; }

    // Delay time actually used by a band right now (after tempo sync and group limits), for the UI.
    float getEffectiveTimeMs (int band1) const noexcept { return effectiveTimeMs[(size_t) band1 - 1].load(); }
    double getHostBpm() const noexcept { return hostBpm.load(); }

    // Band settings as the engine would see them, without tempo sync (safe from any thread).
    std::array<BandParams, kNumBands> getEnginePreview() const;

    // Ask the audio thread to clear all delay lines (used when a preset loads).
    void requestReset() noexcept { resetRequested = true; }

private:
    struct BandParamPointers
    {
        std::atomic<float>* on {}, *hold {}, *source {}, *group {}, *sync {}, *wave {}, *invert {}, *time {}, *note {};
        std::atomic<float>* lowCut {}, *highCut {}, *feedback {}, *tap {}, *speed {}, *depth {}, *pan {}, *level {};
    };

    EngineParams readParams (double bpm);
    void handleAsyncUpdate() override;

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets;
    Engine engine;

    std::array<BandParamPointers, kNumBands> bandParams {};
    std::atomic<float>* effect {}, *direct {}, *directPan {}, *output {}, *bypass {};

    std::array<std::atomic<float>, kNumBands> effectiveTimeMs {};
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<bool> resetRequested { false };
    std::atomic<int> pendingProgram { -1 };
    juce::AudioBuffer<float> scratch;
};

} // namespace octaline
