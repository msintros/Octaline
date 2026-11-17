#include "Engine.h"

#include <algorithm>
#include <cmath>

namespace octaline
{

namespace
{
constexpr double kPi = 3.14159265358979323846;

inline float softLimit (float x) noexcept
{
    // Transparent below 1.0, then bends smoothly towards +/-2.0 so that
    // feedback at 10 can sustain without running away.
    const float a = std::fabs (x);
    if (a <= 1.0f)
        return x;
    const float y = 1.0f + std::tanh (a - 1.0f);
    return x < 0.0f ? -y : y;
}

inline float onePoleCoeff (double fc, double sr) noexcept
{
    const double g = std::tan (kPi * std::min (fc, sr * 0.45) / sr);
    return (float) (g / (1.0 + g));
}

inline void balanceGains (float panKnob, float& l, float& r) noexcept
{
    const float p = std::clamp (panKnob / 10.0f, -1.0f, 1.0f);
    l = p > 0.0f ? 1.0f - p : 1.0f;
    r = p < 0.0f ? 1.0f + p : 1.0f;
}

inline float waveShape (Wave w, double phase) noexcept
{
    // Returns 0 .. 1. The value is added to the delay time, so a rising value
    // lowers the pitch and a falling value raises it.
    switch (w)
    {
        case Wave::Sine:     return (float) (0.5 - 0.5 * std::cos (2.0 * kPi * phase));
        case Wave::Triangle: return (float) (1.0 - std::fabs (2.0 * phase - 1.0));
        case Wave::SawUp:    return (float) (1.0 - phase); // delay shrinks -> pitch rises
        case Wave::SawDown:  return (float) phase;         // delay grows   -> pitch falls
    }
    return 0.0f;
}
} // namespace

//==============================================================================
std::array<BandRole, kNumBands> resolveRoles (const std::array<BandParams, kNumBands>& bands)
{
    std::array<BandRole, kNumBands> roles {};
    for (int i = 0; i < kNumBands; ++i)
        roles[(size_t) i] = { i, 1 };

    for (int i = 0; i < kNumBands; ++i)
    {
        if (! roles[(size_t) i].isHead())
            continue; // already inside an earlier group

        const int end = std::clamp (bands[(size_t) i].groupEnd, 0, kNumBands) - 1; // 0-based last member
        if (end <= i)
            continue;

        roles[(size_t) i].span = end - i + 1;
        for (int m = i + 1; m <= end; ++m)
            roles[(size_t) m] = { i, 0 };
    }
    return roles;
}

float maxTimeMsForSpan (int span)
{
    // One band holds 697 ms; all eight chained hold 5890 ms.
    span = std::clamp (span, 1, kNumBands);
    return 697.0f + (float) (span - 1) * ((5890.0f - 697.0f) / 7.0f);
}

float lowCutHz (float knob)  { return 20.0f * std::pow (50.0f, std::clamp (knob, 0.0f, 10.0f) / 10.0f); }
float highCutHz (float knob) { return 1000.0f * std::pow (16.0f, (10.0f - std::clamp (knob, 0.0f, 10.0f)) / 10.0f); }
float lfoHz (float speed)    { return speed <= 0.0f ? 0.0f : 0.1f * std::pow (100.0f, std::clamp (speed, 0.0f, 10.0f) / 10.0f); }
float depthMs (float depth)  { return std::clamp (depth, 0.0f, 10.0f) * (Engine::kMaxDepthMs / 10.0f); }

//==============================================================================
void Engine::Line::clear()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    lpState = hpState = 0.0f;
    cleared = true;
}

float Engine::Line::read (float delaySamples) const noexcept
{
    // 4-point Hermite interpolation. delaySamples >= 2 so every point is already written.
    const auto whole = (uint32_t) delaySamples;
    const float frac = delaySamples - (float) whole;
    const uint32_t base = writePos - whole;

    const float xm1 = buffer[(base + 1) & mask];
    const float x0  = buffer[base & mask];
    const float x1  = buffer[(base - 1) & mask];
    const float x2  = buffer[(base - 2) & mask];

    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * frac + c2) * frac + c1) * frac + x0;
}

//==============================================================================
void Engine::prepare (double sr)
{
    sampleRate = sr > 0.0 ? sr : 48000.0;

    const double maxSamples = (kMaxTimeMs + kMaxDepthMs + 10.0) * 0.001 * sampleRate + 8.0;
    uint32_t size = 1;
    while ((double) size < maxSamples)
        size <<= 1;

    for (auto& b : bands)
    {
        b.line.buffer.assign (size, 0.0f);
        b.line.mask = size - 1;
        b.line.writePos = 0;
        b.line.clear();
        b.lfoPhase = 0.0;
    }

    fastCoeff = (float) (1.0 - std::exp (-1.0 / (0.010 * sampleRate))); // ~10 ms
    slowCoeff = (float) (1.0 - std::exp (-1.0 / (0.060 * sampleRate))); // ~60 ms glide for time changes
    needsSnap = true;
}

void Engine::reset()
{
    for (auto& b : bands)
    {
        b.line.clear();
        b.lfoPhase = 0.0;
    }
    needsSnap = true;
}

void Engine::updateTargets (const std::array<BandRole, kNumBands>& roles)
{
    const auto& g = params.global;
    const float msToSamples = (float) (sampleRate * 0.001);

    effectGain.target = std::clamp (g.effectLevel, 0.0f, 10.0f) / 10.0f;
    float dl, dr;
    balanceGains (g.directPan, dl, dr);
    const float direct = std::clamp (g.directLevel, 0.0f, 10.0f) / 10.0f;
    directL.target = direct * dl;
    directR.target = direct * dr;
    outputGain.target = std::pow (10.0f, std::clamp (g.outputDb, -60.0f, 24.0f) / 20.0f);
    bypassMix.target = g.bypass ? 1.0f : 0.0f;

    for (int i = 0; i < kNumBands; ++i)
    {
        const auto& p = params.band[(size_t) i];
        const auto& role = roles[(size_t) i];
        auto& s = bands[(size_t) i];

        // Time, filters and feedback come from the head of the group.
        const auto& hp = params.band[(size_t) role.head];
        const int span = roles[(size_t) role.head].span;
        const float timeMs = std::clamp (hp.timeMs, 0.02f, maxTimeMsForSpan (span));

        s.timeSamples.target = timeMs * msToSamples;
        s.tapFraction.target = std::clamp (p.tapPercent, 0.0f, 100.0f) / 100.0f;
        s.feedback.target = std::clamp (hp.feedback, 0.0f, 10.0f) / 10.0f;
        s.depthSamples.target = depthMs (p.depth) * msToSamples;

        const bool headOn = params.band[(size_t) role.head].on;
        s.onGain.target = (p.on && headOn) ? 1.0f : 0.0f;
        s.inputGain.target = (p.on && ! p.inputHold) ? 1.0f : 0.0f;

        const float lvl = std::clamp (p.level, 0.0f, 10.0f) / 10.0f;
        s.wetGain.target = p.invert ? -lvl : lvl;
        float pl, pr;
        balanceGains (p.pan, pl, pr);
        s.panL.target = pl;
        s.panR.target = pr;
    }

    if (needsSnap)
    {
        for (auto* sm : { &effectGain, &directL, &directR, &outputGain, &bypassMix })
            sm->snap();
        for (auto& s : bands)
            for (auto* sm : { &s.timeSamples, &s.tapFraction, &s.feedback, &s.depthSamples,
                              &s.onGain, &s.inputGain, &s.wetGain, &s.panL, &s.panR })
                sm->snap();
        needsSnap = false;
    }
}

float Engine::lfoValue (int i, const std::array<BandRole, kNumBands>&) const noexcept
{
    const auto& p = params.band[(size_t) i];
    const int src = p.sync - 1;
    double phase;

    if (src >= 0 && src < kNumBands && src != i)
        phase = bands[(size_t) src].lfoPhase + std::clamp (p.speed, 0.0f, 10.0f) / 10.0; // speed = phase offset
    else
        phase = bands[(size_t) i].lfoPhase;

    phase -= std::floor (phase);
    return waveShape (p.wave, phase);
}

void Engine::process (const float* inL, const float* inR, float* outL, float* outR, int numSamples)
{
    const auto roles = resolveRoles (params.band);
    updateTargets (roles);

    // Per-block filter coefficients (from each head's settings).
    std::array<float, kNumBands> lpA {}, hpA {};
    std::array<bool, kNumBands> lpOn {}, hpOn {};
    for (int i = 0; i < kNumBands; ++i)
    {
        const auto& p = params.band[(size_t) i];
        lpOn[(size_t) i] = p.highCut > 0.0f;
        hpOn[(size_t) i] = p.lowCut > 0.0f;
        if (lpOn[(size_t) i]) lpA[(size_t) i] = onePoleCoeff (highCutHz (p.highCut), sampleRate);
        if (hpOn[(size_t) i]) hpA[(size_t) i] = onePoleCoeff (lowCutHz (p.lowCut), sampleRate);
    }

    std::array<double, kNumBands> lfoInc {};
    for (int i = 0; i < kNumBands; ++i)
        lfoInc[(size_t) i] = lfoHz (params.band[(size_t) i].speed) / sampleRate;

    std::array<float, kNumBands + 1> through {}; // through[k] = band k's input + its wet output (1-based)
    std::array<float, kNumBands> lineInput {};    // signal entering each head's line this sample

    for (int n = 0; n < numSamples; ++n)
    {
        const float dryL = inL[n];
        const float dryR = inR[n];
        const float send = 0.5f * (dryL + dryR) * effectGain.next (fastCoeff);

        float wetL = 0.0f, wetR = 0.0f;

        for (int i = 0; i < kNumBands; ++i)
        {
            auto& s = bands[(size_t) i];
            const auto& p = params.band[(size_t) i];
            const auto& role = roles[(size_t) i];
            auto& line = bands[(size_t) role.head].line;

            const float timeS   = s.timeSamples.next (slowCoeff);
            const float tapF    = s.tapFraction.next (slowCoeff);
            const float fb      = s.feedback.next (fastCoeff);
            const float depthS  = s.depthSamples.next (fastCoeff);
            const float onG     = s.onGain.next (fastCoeff);
            const float inG     = s.inputGain.next (fastCoeff);
            const float wetG    = s.wetGain.next (fastCoeff);
            const float pL      = s.panL.next (fastCoeff);
            const float pR      = s.panR.next (fastCoeff);

            const float mod = depthS > 0.0f ? depthS * lfoValue (i, roles) : 0.0f;

            float bandInput;
            if (role.isHead())
            {
                const int src = p.source;
                bandInput = (src >= 1 && src <= i) ? through[(size_t) src] : send;

                // Feedback path reads the full delay time (plus this band's modulation).
                const float loopDelay = std::max (2.0f, timeS + mod);
                const float recirc = line.read (loopDelay);

                float x = bandInput * inG * onG + fb * recirc;
                if (hpOn[(size_t) i])
                {
                    const float v = (x - line.hpState) * hpA[(size_t) i];
                    const float lp = v + line.hpState;
                    line.hpState = lp + v;
                    x -= lp;
                }
                if (lpOn[(size_t) i])
                {
                    const float v = (x - line.lpState) * lpA[(size_t) i];
                    const float lp = v + line.lpState;
                    line.lpState = lp + v;
                    x = lp;
                }
                lineInput[(size_t) i] = softLimit (x);
            }
            else
            {
                // Members share the head's input for series routing purposes.
                const int h = role.head;
                const int src = params.band[(size_t) h].source;
                bandInput = (src >= 1 && src <= h) ? through[(size_t) src] : send;
            }

            float wet = 0.0f;
            if (onG > 1.0e-5f)
            {
                const float tapDelay = std::max (2.0f, timeS * tapF + mod);
                wet = line.read (tapDelay) * wetG * onG;
            }

            through[(size_t) i + 1] = bandInput + wet;
            wetL += wet * pL;
            wetR += wet * pR;
        }

        // Commit this sample to every head's line and advance.
        for (int i = 0; i < kNumBands; ++i)
        {
            auto& s = bands[(size_t) i];
            if (roles[(size_t) i].isHead())
            {
                const float v = lineInput[(size_t) i];
                s.line.buffer[s.line.writePos & s.line.mask] = v;
                if (v != 0.0f)
                    s.line.cleared = false;
            }
            s.line.writePos++;
            s.lfoPhase += lfoInc[(size_t) i];
            if (s.lfoPhase >= 1.0)
                s.lfoPhase -= 1.0;
        }

        const float dL = dryL * directL.next (fastCoeff);
        const float dR = dryR * directR.next (fastCoeff);
        const float og = outputGain.next (fastCoeff);
        const float byp = bypassMix.next (fastCoeff);

        const float procL = (dL + wetL) * og;
        const float procR = (dR + wetR) * og;
        outL[n] = procL + (dryL - procL) * byp;
        outR[n] = procR + (dryR - procR) * byp;
    }

    // Free memory bandwidth: silence lines of bands that are fully off.
    for (int i = 0; i < kNumBands; ++i)
    {
        auto& s = bands[(size_t) i];
        if (roles[(size_t) i].isHead() && s.onGain.target == 0.0f && s.onGain.current < 1.0e-5f && ! s.line.cleared)
            s.line.clear();
    }
}

} // namespace octaline
