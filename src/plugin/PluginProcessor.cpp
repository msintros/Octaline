#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Params.h"

namespace octaline
{

OctalineProcessor::OctalineProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Octaline", params::createLayout()),
      presets (apvts, [this] { requestReset(); })
{
    using namespace params;
    for (int b = 1; b <= kNumBands; ++b)
    {
        auto get = [&] (const char* s) { return apvts.getRawParameterValue (bandId (b, s)); };
        auto& p = bandParams[(size_t) b - 1];
        p.on = get (kOn); p.hold = get (kHold); p.source = get (kSource); p.group = get (kGroup);
        p.sync = get (kSync); p.wave = get (kWave); p.invert = get (kInvert); p.time = get (kTime);
        p.note = get (kNote); p.lowCut = get (kLowCut); p.highCut = get (kHighCut); p.feedback = get (kFeedback);
        p.tap = get (kTap); p.speed = get (kSpeed); p.depth = get (kDepth); p.pan = get (kPan); p.level = get (kLevel);
    }
    effect = apvts.getRawParameterValue (kEffect);
    direct = apvts.getRawParameterValue (kDirect);
    directPan = apvts.getRawParameterValue (kDirectPan);
    output = apvts.getRawParameterValue (kOutput);
    bypass = apvts.getRawParameterValue (kBypass);

    for (auto& t : effectiveTimeMs)
        t = 0.0f;
}

bool OctalineProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::stereo())
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void OctalineProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    scratch.setSize (2, juce::jmax (1, samplesPerBlock));
}

EngineParams OctalineProcessor::readParams (double bpm)
{
    EngineParams e;
    const auto& notes = params::noteValues();
    for (int i = 0; i < kNumBands; ++i)
    {
        const auto& p = bandParams[(size_t) i];
        auto& b = e.band[(size_t) i];
        b.on = p.on->load() > 0.5f;
        b.inputHold = p.hold->load() > 0.5f;
        b.source = (int) p.source->load();
        const int g = (int) p.group->load();
        b.groupEnd = g > 0 ? g + 1 : 0;
        b.sync = (int) p.sync->load();
        b.wave = (Wave) juce::jlimit (0, 3, (int) p.wave->load());
        b.invert = p.invert->load() > 0.5f;

        const int note = juce::jlimit (0, (int) notes.size() - 1, (int) p.note->load());
        b.timeMs = note > 0 ? (float) (60000.0 / bpm * notes[(size_t) note].beats) : p.time->load();

        b.lowCut = p.lowCut->load();
        b.highCut = p.highCut->load();
        b.feedback = p.feedback->load();
        b.tapPercent = p.tap->load();
        b.speed = p.speed->load();
        b.depth = p.depth->load();
        b.pan = p.pan->load();
        b.level = p.level->load();
    }
    e.global.effectLevel = effect->load();
    e.global.directLevel = direct->load();
    e.global.directPan = directPan->load();
    e.global.outputDb = output->load();
    e.global.bypass = bypass->load() > 0.5f;

    // Publish the times the engine will really use, for display.
    const auto roles = resolveRoles (e.band);
    for (int i = 0; i < kNumBands; ++i)
    {
        const int h = roles[(size_t) i].head;
        const float t = juce::jlimit (0.02f, maxTimeMsForSpan (roles[(size_t) h].span), e.band[(size_t) h].timeMs);
        effectiveTimeMs[(size_t) i] = t;
    }
    return e;
}

std::array<BandParams, kNumBands> OctalineProcessor::getEnginePreview() const
{
    std::array<BandParams, kNumBands> bands {};
    for (int i = 0; i < kNumBands; ++i)
    {
        const auto& p = bandParams[(size_t) i];
        auto& b = bands[(size_t) i];
        b.on = p.on->load() > 0.5f;
        const int g = (int) p.group->load();
        b.groupEnd = g > 0 ? g + 1 : 0;
        b.source = (int) p.source->load();
        b.timeMs = p.time->load();
    }
    return bands;
}

void OctalineProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0)
        return;

    if (resetRequested.exchange (false))
        engine.reset();

    double bpm = hostBpm.load();
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                if (*b > 1.0)
                    bpm = *b;
    hostBpm = bpm;

    engine.setParams (readParams (bpm));

    const int numIn = getTotalNumInputChannels();
    if (scratch.getNumSamples() < n)
        scratch.setSize (2, n, false, false, true);

    const float* inL = buffer.getReadPointer (0);
    const float* inR = numIn > 1 ? buffer.getReadPointer (1) : inL;
    float* outL = scratch.getWritePointer (0);
    float* outR = scratch.getWritePointer (1);
    engine.process (inL, inR, outL, outR, n);

    buffer.copyFrom (0, 0, scratch, 0, 0, n);
    if (buffer.getNumChannels() > 1)
        buffer.copyFrom (1, 0, scratch, 1, 0, n);
}

int OctalineProcessor::getNumPrograms() { return (int) factoryPresets().size(); }

int OctalineProcessor::getCurrentProgram()
{
    const int idx = presets.getCurrentIndex();
    const auto& entries = presets.getEntries();
    return (idx >= 0 && idx < entries.size() && entries[idx].factory) ? entries[idx].factoryIndex : 0;
}

void OctalineProcessor::setCurrentProgram (int index)
{
    if (index < 0 || index >= getNumPrograms())
        return;
    // Hosts may call this from any thread; preset loading belongs on the message thread.
    pendingProgram = index;
    if (juce::MessageManager::existsAndIsCurrentThread())
        handleAsyncUpdate();
    else
        triggerAsyncUpdate();
}

void OctalineProcessor::handleAsyncUpdate()
{
    const int index = pendingProgram.exchange (-1);
    if (index >= 0)
        presets.loadFactory (index);
}

const juce::String OctalineProcessor::getProgramName (int index)
{
    const auto& f = factoryPresets();
    return index >= 0 && index < (int) f.size() ? juce::String (f[(size_t) index].name) : juce::String();
}

void OctalineProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    presets.writeToState (state);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void OctalineProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;
    auto tree = juce::ValueTree::fromXml (*xml);
    presets.setRestoring (true);
    apvts.replaceState (tree);

    // replaceState skips parameters whose stored value already matches the
    // snapped value it has cached (for example a bool left at 0.49 by a host),
    // so push every saved value explicitly to restore the exact state.
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto child = tree.getChildWithProperty ("id", rp->paramID);
            if (child.isValid() && child.hasProperty ("value"))
                rp->setValueNotifyingHost (rp->convertTo0to1 ((float) (double) child.getProperty ("value")));
        }
    presets.setRestoring (false);
    presets.readFromState (tree);
    requestReset();
}

juce::AudioProcessorEditor* OctalineProcessor::createEditor()
{
    return new OctalineEditor (*this);
}

} // namespace octaline

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new octaline::OctalineProcessor();
}
