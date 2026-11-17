// Offline behaviour tests for the Octaline engine. Each test feeds a known
// signal through the engine and checks timing and levels against the
// behaviour described in docs/behaviour.md.

#include "../src/dsp/Engine.h"
#include "../src/plugin/FactoryPresets.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>

using namespace octaline;

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...)                                                        \
    do {                                                                        \
        ++checks;                                                               \
        if (! (cond)) {                                                         \
            ++failures;                                                         \
            std::printf ("  FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond);      \
            std::printf (__VA_ARGS__);                                          \
            std::printf ("\n");                                                 \
        }                                                                       \
    } while (0)

struct Render
{
    std::vector<float> l, r;
};

static constexpr double kSr = 48000.0;

static EngineParams quietParams()
{
    EngineParams p;
    p.global.directLevel = 0.0f; // wet only, so the dry impulse does not mask echoes
    for (auto& b : p.band) b.on = false;
    return p;
}

static Render render (const EngineParams& p, const std::vector<float>& input, double sr = kSr)
{
    Engine e;
    e.prepare (sr);
    e.setParams (p);
    Render out;
    out.l.resize (input.size());
    out.r.resize (input.size());
    const int block = 256;
    for (size_t i = 0; i < input.size(); i += block)
    {
        const int n = (int) std::min<size_t> (block, input.size() - i);
        e.process (input.data() + i, input.data() + i, out.l.data() + i, out.r.data() + i, n);
    }
    return out;
}

static std::vector<float> impulse (double seconds, double sr = kSr)
{
    std::vector<float> x ((size_t) (seconds * sr), 0.0f);
    x[0] = 1.0f;
    return x;
}

// Value of the signal near an expected time (largest magnitude within +/- 2 samples).
static float near (const std::vector<float>& x, double ms, double sr = kSr)
{
    const long c = std::lround (ms * 0.001 * sr);
    float best = 0.0f;
    for (long i = c - 2; i <= c + 2; ++i)
        if (i >= 0 && i < (long) x.size() && std::fabs (x[(size_t) i]) > std::fabs (best))
            best = x[(size_t) i];
    return best;
}

// Times (ms) of local peaks above a threshold.
static std::vector<double> peaks (const std::vector<float>& x, float threshold, double sr = kSr)
{
    std::vector<double> t;
    for (size_t i = 1; i + 1 < x.size(); ++i)
    {
        const float a = std::fabs (x[i]);
        if (a > threshold && a >= std::fabs (x[i - 1]) && a > std::fabs (x[i + 1]))
            t.push_back ((double) i * 1000.0 / sr);
    }
    return t;
}

static std::string list (const std::vector<double>& v)
{
    std::string s;
    for (double d : v) { char b[32]; std::snprintf (b, sizeof b, "%.2f ", d); s += b; }
    return s;
}

static bool closeTo (double a, double b, double tol) { return std::fabs (a - b) <= tol; }

//==============================================================================
static void testSingleEcho()
{
    auto p = quietParams();
    p.band[0].on = true;
    p.band[0].timeMs = 250.0f;
    auto out = render (p, impulse (1.0));
    auto pk = peaks (out.l, 0.1f);
    CHECK (pk.size() == 1, "peaks: %s", list (pk).c_str());
    CHECK (! pk.empty() && closeTo (pk[0], 250.0, 0.05), "peak at %s", list (pk).c_str());
    CHECK (closeTo (near (out.l, 250), 1.0, 0.02), "level %f", near (out.l, 250));
    CHECK (closeTo (near (out.r, 250), 1.0, 0.02), "centre pan should be full on both sides");
}

static void testTapAndFeedback()
{
    // Manual: 360 ms, tap 66.6 %, feedback 5 -> first echo at 240 ms, then every 360 ms.
    auto p = quietParams();
    p.band[0].on = true;
    p.band[0].timeMs = 360.0f;
    p.band[0].tapPercent = 66.6667f;
    p.band[0].feedback = 5.0f;
    auto out = render (p, impulse (1.5));
    auto pk = peaks (out.l, 0.05f);
    CHECK (pk.size() >= 3, "peaks: %s", list (pk).c_str());
    if (pk.size() >= 3)
    {
        CHECK (closeTo (pk[0], 240.0, 0.1), "first %s", list (pk).c_str());
        CHECK (closeTo (pk[1], 600.0, 0.1), "second %s", list (pk).c_str());
        CHECK (closeTo (pk[2], 960.0, 0.1), "third %s", list (pk).c_str());
    }
    CHECK (closeTo (near (out.l, 600) / near (out.l, 240), 0.5, 0.02), "feedback 5 halves each repeat");
}

static void testInfiniteFeedback()
{
    auto p = quietParams();
    p.band[0].on = true;
    p.band[0].timeMs = 100.0f;
    p.band[0].feedback = 10.0f;
    auto out = render (p, impulse (3.0));
    CHECK (std::fabs (near (out.l, 2900)) > 0.9f, "feedback 10 should hold: %f", near (out.l, 2900));
}

static void testGroupTaps()
{
    // Patch "16-Beat": bands 1-4 grouped, 459 ms, taps 100/25/50/75.
    auto p = quietParams();
    const float taps[] = { 100, 25, 50, 75 };
    for (int i = 0; i < 4; ++i)
    {
        p.band[(size_t) i].on = true;
        p.band[(size_t) i].tapPercent = taps[i];
        p.band[(size_t) i].timeMs = 50.0f; // members' own times must be ignored
    }
    p.band[0].groupEnd = 4;
    p.band[0].timeMs = 459.0f;
    auto out = render (p, impulse (0.6));
    auto pk = peaks (out.l, 0.1f);
    CHECK (pk.size() == 4, "peaks: %s", list (pk).c_str());
    if (pk.size() == 4)
    {
        CHECK (closeTo (pk[0], 114.75, 0.05), "%s", list (pk).c_str());
        CHECK (closeTo (pk[1], 229.5, 0.05), "%s", list (pk).c_str());
        CHECK (closeTo (pk[2], 344.25, 0.05), "%s", list (pk).c_str());
        CHECK (closeTo (pk[3], 459.0, 0.05), "%s", list (pk).c_str());
    }
}

static void testGroupLength()
{
    auto p = quietParams();
    for (auto& b : p.band) b.on = true;
    for (size_t i = 1; i < 8; ++i) p.band[i].level = 0.0f;
    p.band[0].groupEnd = 8;
    p.band[0].timeMs = 5890.0f;
    auto out = render (p, impulse (6.0));
    auto pk = peaks (out.l, 0.1f);
    CHECK (pk.size() == 1 && closeTo (pk[0], 5890.0, 0.05), "8-band group: %s", list (pk).c_str());

    auto single = quietParams();
    single.band[0].on = true;
    single.band[0].timeMs = 5890.0f; // not grouped -> clamps to one band's maximum
    auto out2 = render (single, impulse (1.0));
    auto pk2 = peaks (out2.l, 0.1f);
    CHECK (pk2.size() == 1 && closeTo (pk2[0], 697.0, 0.05), "single band clamp: %s", list (pk2).c_str());
    CHECK (closeTo (maxTimeMsForSpan (2), 1438.0, 10.0), "two-band group about 1.43 s: %f", maxTimeMsForSpan (2));
}

static void testSeries()
{
    // Manual: band 1 600 ms from input, band 2 80 ms fed from band 1.
    auto p = quietParams();
    p.band[0].on = true; p.band[0].timeMs = 600.0f;
    p.band[1].on = true; p.band[1].timeMs = 80.0f; p.band[1].source = 1;
    auto out = render (p, impulse (1.0));
    auto pk = peaks (out.l, 0.1f);
    CHECK (pk.size() == 3, "peaks: %s", list (pk).c_str());
    if (pk.size() == 3)
    {
        CHECK (closeTo (pk[0], 80.0, 0.05), "%s", list (pk).c_str());
        CHECK (closeTo (pk[1], 600.0, 0.05), "%s", list (pk).c_str());
        CHECK (closeTo (pk[2], 680.0, 0.05), "%s", list (pk).c_str());
    }

    // A source that is not a lower band falls back to the main input.
    auto bad = quietParams();
    bad.band[0].on = true; bad.band[0].timeMs = 100.0f; bad.band[0].source = 5;
    auto out2 = render (bad, impulse (0.3));
    CHECK (closeTo (near (out2.l, 100), 1.0, 0.02), "invalid source should read input");
}

static void testPanPhaseLevel()
{
    auto p = quietParams();
    p.band[0].on = true; p.band[0].timeMs = 100.0f; p.band[0].pan = -10.0f;
    p.band[1].on = true; p.band[1].timeMs = 200.0f; p.band[1].pan = 10.0f; p.band[1].invert = true; p.band[1].level = 5.0f;
    auto out = render (p, impulse (0.5));
    CHECK (closeTo (near (out.l, 100), 1.0, 0.02) && std::fabs (near (out.r, 100)) < 1e-4, "hard left");
    CHECK (closeTo (near (out.r, 200), -0.5, 0.02) && std::fabs (near (out.l, 200)) < 1e-4, "hard right, inverted, half level: %f", near (out.r, 200));
}

static void testDirectAndEffectLevels()
{
    EngineParams p;
    p.global.directLevel = 10.0f; p.global.directPan = -10.0f; p.global.effectLevel = 5.0f;
    p.band[0].on = true; p.band[0].timeMs = 100.0f;
    auto out = render (p, impulse (0.3));
    CHECK (closeTo (out.l[0], 1.0, 1e-4) && std::fabs (out.r[0]) < 1e-4, "direct panned left");
    CHECK (closeTo (near (out.l, 100), 0.5, 0.02), "effect level 5 halves the send: %f", near (out.l, 100));

    p.global.bypass = true;
    auto byp = render (p, impulse (0.3));
    CHECK (closeTo (byp.l[0], 1.0, 1e-4) && closeTo (byp.r[0], 1.0, 1e-4), "bypass passes the dry signal unchanged");
    CHECK (std::fabs (near (byp.l, 100)) < 1e-4, "bypass mutes echoes");
}

static void testOnOffAndHold()
{
    auto p = quietParams();
    p.band[0].on = false; p.band[0].timeMs = 100.0f;
    auto out = render (p, impulse (0.3));
    CHECK (std::fabs (near (out.l, 100)) < 1e-5, "off band is silent");

    // Hold: tail keeps going but new input is ignored.
    Engine e;
    e.prepare (kSr);
    auto hp = quietParams();
    hp.band[0].on = true; hp.band[0].timeMs = 100.0f; hp.band[0].feedback = 10.0f;
    e.setParams (hp);
    std::vector<float> in (4800, 0.0f), l (4800), r (4800);
    in[0] = 1.0f;
    e.process (in.data(), in.data(), l.data(), r.data(), 4800); // 100 ms
    hp.band[0].inputHold = true;
    e.setParams (hp);
    std::vector<float> in2 (48000, 0.0f), l2 (48000), r2 (48000);
    for (size_t i = 2400; i < in2.size(); i += 997) in2[i] = 1.0f; // new hits (after the 10 ms fade) must be ignored
    e.process (in2.data(), in2.data(), l2.data(), r2.data(), 48000);
    auto pk = peaks (l2, 0.1f);
    bool everyHundred = ! pk.empty();
    for (double t : pk) everyHundred = everyHundred && std::fabs (std::fmod (t + 100.0, 100.0)) < 0.1;
    CHECK (everyHundred && pk.size() >= 9, "only the held loop repeats: %s", list (pk).c_str());
}

static void testFilters()
{
    auto sine = [] (double hz) {
        std::vector<float> x ((size_t) (kSr * 0.5));
        for (size_t i = 0; i < x.size(); ++i) x[i] = (float) std::sin (2.0 * 3.141592653589793 * hz * (double) i / kSr);
        return x;
    };
    auto rms = [] (const std::vector<float>& x, size_t from) {
        double s = 0; for (size_t i = from; i < x.size(); ++i) s += (double) x[i] * x[i];
        return std::sqrt (s / (double) (x.size() - from));
    };
    auto p = quietParams();
    p.band[0].on = true; p.band[0].timeMs = 10.0f; p.band[0].highCut = 10.0f;
    const double lowPass = rms (render (p, sine (100)).l, 4800);
    const double highCut = rms (render (p, sine (8000)).l, 4800);
    CHECK (lowPass > 0.6 && highCut < 0.15, "high cut at 10 (1 kHz): 100 Hz %f, 8 kHz %f", lowPass, highCut);

    p.band[0].highCut = 0.0f; p.band[0].lowCut = 10.0f;
    const double lowCut = rms (render (p, sine (60)).l, 4800);
    const double highPass = rms (render (p, sine (8000)).l, 4800);
    CHECK (lowCut < 0.1 && highPass > 0.6, "low cut at 10 (1 kHz): 60 Hz %f, 8 kHz %f", lowCut, highPass);

    // Filters sit inside the loop, so later repeats are darker than earlier ones.
    auto q = quietParams();
    q.band[0].on = true; q.band[0].timeMs = 50.0f; q.band[0].feedback = 9.0f; q.band[0].highCut = 5.0f;
    auto out = render (q, impulse (0.5));
    CHECK (std::fabs (near (out.l, 50)) > std::fabs (near (out.l, 400)) * 1.5f, "repeats lose highs");
    CHECK (closeTo (lowCutHz (10.0f), 1000.0, 1.0) && closeTo (highCutHz (10.0f), 1000.0, 1.0), "10 = 1 kHz");
}

static void testModulationSync()
{
    Engine e;
    e.prepare (kSr);
    auto p = quietParams();
    p.band[0].on = p.band[1].on = true;
    p.band[0].speed = 5.0f; p.band[0].depth = 5.0f;
    p.band[1].sync = 1; p.band[1].speed = 5.0f; p.band[1].depth = 5.0f; // 180 degrees behind band 1
    e.setParams (p);
    std::vector<float> z (1000, 0.0f), l (1000), r (1000);
    double maxDiff = 0.0;
    for (int k = 0; k < 96; ++k)
    {
        e.process (z.data(), z.data(), l.data(), r.data(), 1000);
        maxDiff = std::max (maxDiff, (double) std::fabs (e.currentLfo (0) + e.currentLfo (1) - 1.0f));
    }
    CHECK (maxDiff < 1e-3, "sine at 180 degrees mirrors: %f", maxDiff);

    p.band[1].speed = 0.0f; // in phase
    e.setParams (p);
    e.process (z.data(), z.data(), l.data(), r.data(), 1000);
    CHECK (std::fabs (e.currentLfo (0) - e.currentLfo (1)) < 1e-4, "sync with no offset is in phase");

    // Modulation actually moves the echo: with depth the echo arrives later than the set time at some point.
    auto m = quietParams();
    m.band[0].on = true; m.band[0].timeMs = 20.0f; m.band[0].speed = 10.0f; m.band[0].depth = 10.0f;
    auto out = render (m, impulse (0.2));
    auto pk = peaks (out.l, 0.1f);
    CHECK (! pk.empty() && pk[0] > 20.0 && pk[0] < 20.0 + Engine::kMaxDepthMs + 0.1, "modulated echo between 20 and 28 ms: %s", list (pk).c_str());
}

static void testStability()
{
    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    for (int trial = 0; trial < 6; ++trial)
    {
        EngineParams p;
        for (int i = 0; i < kNumBands; ++i)
        {
            auto& b = p.band[(size_t) i];
            b.on = true;
            b.timeMs = 1.0f + u (rng) * 690.0f;
            b.feedback = 10.0f;
            b.tapPercent = u (rng) * 100.0f;
            b.speed = u (rng) * 10.0f;
            b.depth = u (rng) * 10.0f;
            b.wave = (Wave) (int) (u (rng) * 3.99f);
            b.source = i > 0 && u (rng) > 0.5f ? 1 + (int) (u (rng) * (float) i) : 0;
            b.sync = u (rng) > 0.7f ? 1 + (int) (u (rng) * 7.99f) : 0;
            b.lowCut = u (rng) > 0.5f ? u (rng) * 10.0f : 0.0f;
            b.highCut = u (rng) > 0.5f ? u (rng) * 10.0f : 0.0f;
            b.pan = u (rng) * 20.0f - 10.0f;
        }
        if (trial % 2) p.band[0].groupEnd = 3;
        std::vector<float> noise ((size_t) (kSr * 10.0));
        std::normal_distribution<float> g (0.0f, 0.3f);
        for (auto& x : noise) x = g (rng);
        auto out = render (p, noise, trial < 3 ? 44100.0 : 96000.0);
        float peak = 0.0f; bool finite = true;
        for (size_t i = 0; i < out.l.size(); ++i)
        {
            finite = finite && std::isfinite (out.l[i]) && std::isfinite (out.r[i]);
            peak = std::max ({ peak, std::fabs (out.l[i]), std::fabs (out.r[i]) });
        }
        // 8 bands, each limited to about 2.0 inside its loop, plus series stacking.
        CHECK (finite && peak < 60.0f, "trial %d: finite %d peak %f", trial, (int) finite, peak);
    }
}

static void testSampleRates()
{
    for (double sr : { 44100.0, 88200.0, 192000.0 })
    {
        auto p = quietParams();
        p.band[0].on = true; p.band[0].timeMs = 333.0f;
        auto out = render (p, impulse (0.5, sr), sr);
        auto pk = peaks (out.l, 0.1f, sr);
        CHECK (pk.size() == 1 && closeTo (pk[0], 333.0, 0.05), "sr %.0f: %s", sr, list (pk).c_str());
    }
}

static void testFactoryPresets()
{
    const auto& presets = factoryPresets();
    CHECK (presets.size() == 87, "count %d", (int) presets.size());
    std::mt19937 rng (99);
    std::normal_distribution<float> g (0.0f, 0.25f);
    std::vector<float> noise ((size_t) (kSr * 3.0));
    for (auto& x : noise) x = g (rng);
    for (const auto& fp : presets)
    {
        auto out = render (fp.params, noise);
        float peak = 0.0f; bool finite = true; double energy = 0.0;
        for (size_t i = 0; i < out.l.size(); ++i)
        {
            finite = finite && std::isfinite (out.l[i]) && std::isfinite (out.r[i]);
            peak = std::max ({ peak, std::fabs (out.l[i]), std::fabs (out.r[i]) });
            energy += (double) out.l[i] * out.l[i] + (double) out.r[i] * out.r[i];
        }
        const bool blank = std::string (fp.name).find ("Blank") != std::string::npos;
        CHECK (finite && peak < 30.0f && (blank || energy > 1.0), "%s: finite %d peak %f energy %f", fp.name, (int) finite, peak, energy);
    }
}

int main()
{
    const std::pair<const char*, std::function<void()>> tests[] = {
        { "single echo", testSingleEcho },
        { "tap and feedback (manual p.14)", testTapAndFeedback },
        { "infinite feedback", testInfiniteFeedback },
        { "group taps (16-Beat patch)", testGroupTaps },
        { "group length", testGroupLength },
        { "series connection (manual p.15)", testSeries },
        { "pan, phase, level", testPanPhaseLevel },
        { "direct, effect level, bypass", testDirectAndEffectLevels },
        { "on/off and input hold", testOnOffAndHold },
        { "loop filters", testFilters },
        { "modulation sync", testModulationSync },
        { "stability with feedback 10", testStability },
        { "sample rates", testSampleRates },
        { "factory presets render cleanly", testFactoryPresets },
    };
    for (auto& [name, fn] : tests)
    {
        const int before = failures;
        fn();
        std::printf ("%s %s\n", failures == before ? "PASS" : "FAIL", name);
    }
    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
