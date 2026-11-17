#pragma once

// Octaline delay engine.
//
// Plain C++ with no framework dependency, so it can be unit tested on its own.
// Models eight identical delay "bands" that can be routed in parallel or in
// series, chained into one long delay line (a group), and have their
// modulation locked to each other. See docs/behaviour.md for the full spec.

#include <array>
#include <cstdint>
#include <vector>

namespace octaline
{

constexpr int kNumBands = 8;

enum class Wave : int { Sine = 0, Triangle, SawUp, SawDown };

struct BandParams
{
    bool on = false;          // band produces sound at all
    bool inputHold = false;   // stop feeding new input; the tail keeps ringing
    int source = 0;           // 0 = main input, k = output of band k (1-based, must be lower than this band)
    int groupEnd = 0;         // when > own index, this band heads a group that runs to band groupEnd (1-based)
    int sync = 0;             // 0 = free LFO, k = lock LFO rate to band k (speed becomes a phase offset)
    Wave wave = Wave::Sine;
    bool invert = false;      // output polarity

    float timeMs = 300.0f;    // 0.02 .. maxTimeMsForSpan(span)
    float lowCut = 0.0f;      // 0 = off, 0.1 .. 10 (10 = 1 kHz)
    float highCut = 0.0f;     // 0 = off, 0.1 .. 10 (10 = 1 kHz)
    float feedback = 0.0f;    // 0 .. 10 (10 = repeats forever)
    float tapPercent = 100.0f;// 0 .. 100, where in the delay time the output is read
    float speed = 0.0f;       // 0 .. 10 (rate, or phase offset when synced)
    float depth = 0.0f;       // 0 .. 10
    float pan = 0.0f;         // -10 (left) .. 10 (right)
    float level = 10.0f;      // 0 .. 10 (10 = unity)
};

struct GlobalParams
{
    float effectLevel = 10.0f; // 0 .. 10, send into the bands
    float directLevel = 10.0f; // 0 .. 10, dry signal
    float directPan = 0.0f;    // -10 .. 10
    float outputDb = 0.0f;     // master output gain in dB
    bool bypass = false;
};

struct EngineParams
{
    std::array<BandParams, kNumBands> band {};
    GlobalParams global {};
};

// How a band takes part in the routing after groups have been resolved.
struct BandRole
{
    int head = 0;   // 0-based index of the band whose delay line this band reads
    int span = 1;   // for a head: number of bands in its group (1 = not grouped)
    bool isHead() const noexcept { return span > 0; }
};

// Resolve groups from the per-band groupEnd settings. A band that already
// belongs to an earlier group cannot start a group of its own. Members get span 0.
std::array<BandRole, kNumBands> resolveRoles (const std::array<BandParams, kNumBands>& bands);

// Value mappings, shared with the UI so displayed numbers match the sound.
float maxTimeMsForSpan (int span);
float lowCutHz (float knob);   // knob > 0
float highCutHz (float knob);  // knob > 0
float lfoHz (float speedKnob);
float depthMs (float depthKnob);

class Engine
{
public:
    static constexpr float kMaxTimeMs = 5890.0f;
    static constexpr float kMaxDepthMs = 8.0f;

    void prepare (double sampleRate);
    void reset();                         // clear all delay lines and snap smoothing
    void setParams (const EngineParams& p) { params = p; }
    const EngineParams& getParams() const noexcept { return params; }

    // inR may be the same pointer as inL for mono input. Output is always stereo.
    void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples);

    double getSampleRate() const noexcept { return sampleRate; }

    // Current modulation shape value (0..1) of a band, for tests and metering.
    float currentLfo (int bandIndex) const noexcept { return lfoValue (bandIndex, {}); }

private:
    struct Smoothed
    {
        float current = 0.0f, target = 0.0f;
        void snap() noexcept { current = target; }
        float next (float coeff) noexcept { current += (target - current) * coeff; return current; }
    };

    struct Line
    {
        std::vector<float> buffer;
        uint32_t mask = 0;
        uint32_t writePos = 0;
        float lpState = 0.0f, hpState = 0.0f;
        bool cleared = true;
        void clear();
        float read (float delaySamples) const noexcept;
    };

    struct BandState
    {
        Line line;
        double lfoPhase = 0.0;      // free-running phase in [0, 1)
        Smoothed timeSamples, tapFraction, feedback, depthSamples;
        Smoothed onGain, inputGain, wetGain, panL, panR;
    };

    float lfoValue (int bandIndex, const std::array<BandRole, kNumBands>& roles) const noexcept;
    void updateTargets (const std::array<BandRole, kNumBands>& roles);

    EngineParams params {};
    std::array<BandState, kNumBands> bands {};
    Smoothed effectGain, directL, directR, outputGain, bypassMix;
    double sampleRate = 48000.0;
    float fastCoeff = 0.0f, slowCoeff = 0.0f;
    bool needsSnap = true;
};

} // namespace octaline
