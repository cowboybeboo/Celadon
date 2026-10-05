// Celadon DSP test suite — compiles WITHOUT JUCE:
//   g++ -O2 -std=c++17 test_dsp.cpp -o celadon_test && ./celadon_test [path-to.sf2]
// Behavioral tests, not smoke tests: envelope timing against the spec's math,
// pitch accuracy in cents, loop continuity, filter stability at extremes,
// exclusive-class chokes, polyphony pressure, FX transparency-at-zero,
// reverb decay, and an integration pass over every preset of the real cart.

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include "../Source/DSP/CeladonSF2.h"
#include "../Source/DSP/CeladonDSP.h"
#include "../Source/DSP/CeladonFX.h"
#include "../Source/DSP/CeladonEngine.h"

using namespace celadon;

static int passed = 0, failed = 0;
static const char* section = "";
#define CHECK(cond) do { if (cond) ++passed; else { ++failed; \
    std::printf("  FAIL [%s] %s:%d  %s\n", section, __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_NEAR(a,b,tol) do { double _a=(a),_b=(b); if (std::fabs(_a-_b)<=(tol)) ++passed; \
    else { ++failed; std::printf("  FAIL [%s] %s:%d  %s=%.6f vs %s=%.6f (tol %.6f)\n", \
    section, __FILE__, __LINE__, #a, _a, #b, _b, (double)(tol)); } } while (0)
static void SECTION (const char* s) { section = s; std::printf ("== %s\n", s); }

static constexpr double SR = 48000.0;

// ---------- helpers ----------
static bool allFinite (const float* x, int n)
{
    for (int i = 0; i < n; ++i) if (! std::isfinite (x[i])) return false;
    return true;
}
static float peakOf (const float* x, int n)
{
    float p = 0; for (int i = 0; i < n; ++i) p = std::max (p, std::abs (x[i])); return p;
}
static double rmsOf (const float* x, int a, int b)
{
    double s = 0; for (int i = a; i < b; ++i) s += (double) x[i] * x[i];
    return std::sqrt (s / std::max (1, b - a));
}
// dominant frequency by zero-crossing counting over [a,b)
static double zcFreq (const float* x, int a, int b, double sr)
{
    int c = 0; int first = -1, last = -1;
    for (int i = a + 1; i < b; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) { ++c; if (first < 0) first = i; last = i; }
    if (c < 2 || last <= first) return 0.0;
    return (double) (c - 1) * sr / (double) (last - first);
}

// A synthetic in-memory soundfont: one sine sample with clean loop.
// period 48 samples @48k = exactly 1 kHz. rootKey 69 so key 69 -> 1 kHz.
struct TestFont
{
    SoundFont font;
    TestFont (int mode = 1, float sustainDb = 0.0f, float releaseS = 0.05f,
              float filterFcCents = 13500.0f, float filterQdB = 0.0f)
    {
        const int period = 48, cycles = 40, pre = 96;
        const int loopStart = pre, loopEnd = pre + period * cycles;
        const int end = loopEnd + period * 2;
        font.pool.assign ((size_t) end + 64, 0.0f);          // trailing guard zeros
        for (int i = 0; i < end; ++i)
            font.pool[(size_t) i] = (float) (0.8 * std::sin (2.0 * M_PI * (double) (i - pre) / (double) period));

        SampleHeader sh;
        sh.name = "sine1k"; sh.start = 0; sh.end = (uint32_t) end;
        sh.loopStart = (uint32_t) loopStart; sh.loopEnd = (uint32_t) loopEnd;
        sh.rate = 48000; sh.origPitch = 69; sh.pitchCorr = 0; sh.type = 1;
        font.samples.push_back (sh);

        Zone z;
        z.sampleIndex = 0; z.rootKey = 69; z.sampleModes = mode;
        z.attack = 0.001f; z.decay = 0.001f; z.sustainDb = sustainDb; z.release = releaseS;
        z.filterFcCents = filterFcCents; z.filterQdB = filterQdB;
        Preset p; p.name = "TEST"; p.bank = 0; p.program = 0;
        p.zones.push_back (z);
        font.presets.push_back (p);
    }
};

static std::vector<float> renderNote (Engine& e, int key, int vel, double secOn, double secTotal,
                                      std::vector<float>* right = nullptr)
{
    const int n = (int) (secTotal * SR), onN = (int) (secOn * SR);
    std::vector<float> L ((size_t) n, 0.0f), R ((size_t) n, 0.0f);
    e.noteOn (key, vel);
    int done = 0;
    while (done < n)
    {
        int len = std::min (256, n - done);
        if (done <= onN && onN < done + len && secOn < secTotal) { /* noteOff mid-block below */ }
        e.process (L.data() + done, R.data() + done, len, 120.0);
        done += len;
        if (done >= onN && secOn < secTotal) { e.noteOff (key); }
    }
    if (right) *right = R;
    return L;
}

int main (int argc, char** argv)
{
    std::printf ("Celadon DSP tests\n");

    // ---------------- unit conversions ----------------
    SECTION ("SF2 unit conversions");
    CHECK_NEAR (tc2sec (0), 1.0, 1e-9);
    CHECK_NEAR (tc2sec (-12000), 0.000977, 1e-5);
    CHECK_NEAR (tc2sec (1200), 2.0, 1e-9);
    CHECK_NEAR (cents2hz (0), 8.176, 1e-3);
    CHECK_NEAR (cents2hz (6900), 440.0, 0.5);        // A4
    CHECK_NEAR (cb2gain (0), 1.0, 1e-9);
    CHECK_NEAR (cb2gain (200), 0.1, 1e-9);           // 20 dB
    CHECK_NEAR (dbToGain (-6.0206f), 0.5, 1e-3);

    // ---------------- volume envelope ----------------
    SECTION ("volume envelope (DAHDSR)");
    {
        VolEnv e; e.setup (0.0f, 0.1f, 0.0f, 0.2f, 6.0f, 0.1f, SR);
        int aN = (int) (0.1 * SR);
        float v = 0;
        for (int i = 0; i < aN; ++i) v = e.next();
        CHECK (v > 0.98f);                            // attack completes on time
        // decay 96dB-fullscale over 0.2s -> 6 dB drop takes 0.2*6/96 = 12.5 ms
        int dN = (int) (0.0125 * SR * 1.3);
        for (int i = 0; i < dN; ++i) v = e.next();
        CHECK_NEAR (v, dbToGain (-6.0f), 0.06);       // sustain reached, right level
        for (int i = 0; i < 4800; ++i) v = e.next();
        CHECK_NEAR (v, dbToGain (-6.0f), 0.01);       // holds at sustain
        e.noteOff();
        // release: from -6 dB to -96 dB at 96dB/0.1s -> 93.75 ms
        int rN = (int) (0.09375 * SR);
        for (int i = 0; i < (int) (rN * 0.8); ++i) v = e.next();
        CHECK (v > 0.0f && ! e.done());
        for (int i = 0; i < (int) (rN * 0.4); ++i) v = e.next();
        CHECK (e.done());
        // note-off during attack releases from current level without a jump
        VolEnv e2; e2.setup (0.0f, 1.0f, 0.0f, 0.1f, 0.0f, 0.5f, SR);
        for (int i = 0; i < 4800; ++i) e2.next();     // 10% into a 1 s attack
        e2.noteOff();
        float v1 = e2.next();
        CHECK (v1 < 0.12f && v1 > 0.0f);
    }

    // ---------------- SVF ----------------
    SECTION ("state-variable filter");
    {
        SVF f; f.set (500.0f, 0.0f, SR);
        // 2 kHz sine through fc=500 12dB/oct: expect roughly -24 dB
        int n = 4800; std::vector<float> out ((size_t) n);
        for (int i = 0; i < n; ++i)
            out[(size_t) i] = f.lp ((float) std::sin (2.0 * M_PI * 2000.0 * i / SR));
        double g = rmsOf (out.data(), n / 2, n) / (std::sqrt (0.5));
        CHECK (g < 0.15 && g > 0.01);
        // resonance raises gain at fc
        SVF f2; f2.set (1000.0f, 12.0f, SR);
        SVF f3; f3.set (1000.0f, 0.0f, SR);
        std::vector<float> o2 ((size_t) n), o3 ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            float x = (float) std::sin (2.0 * M_PI * 1000.0 * i / SR);
            o2[(size_t) i] = f2.lp (x); o3[(size_t) i] = f3.lp (x);
        }
        CHECK (rmsOf (o2.data(), n/2, n) > rmsOf (o3.data(), n/2, n) * 1.5);
        // stability at extremes
        for (float fc : { 10.0f, 100.0f, 19000.0f, 30000.0f })
            for (float q : { 0.0f, 12.0f, 30.0f, 96.0f })
            {
                SVF fx; fx.set (fc, q, SR);
                float acc = 0;
                for (int i = 0; i < 9600; ++i)
                    acc = fx.lp (i == 0 ? 1.0f : 0.0f);
                CHECK (std::isfinite (acc) && std::abs (acc) < 1.0f);
            }
    }

    // ---------------- synthetic font: pitch ----------------
    SECTION ("pitch accuracy");
    {
        TestFont tf;
        Engine e; e.prepare (SR, 256); e.setFont (&tf.font);
        auto L = renderNote (e, 69, 100, 1.0, 1.0);
        CHECK (allFinite (L.data(), (int) L.size()));
        double f = zcFreq (L.data(), 4800, 43200, SR);
        CHECK_NEAR (f, 1000.0, 1.5);                  // ±~2.5 cents
        e.panic();
        auto L2 = renderNote (e, 81, 100, 0.6, 0.6);  // +1 octave
        double f2 = zcFreq (L2.data(), 4800, 24000, SR);
        CHECK_NEAR (f2, 2000.0, 3.0);
        e.panic();
        auto L3 = renderNote (e, 62, 100, 0.6, 0.6);  // -7 semitones
        double f3 = zcFreq (L3.data(), 4800, 24000, SR);
        CHECK_NEAR (f3, 1000.0 * std::pow (2.0, -7.0 / 12.0), 3.0);
        // TUNE knob
        SynthParams p; p.tuneSemis = 12.0f; e.setParams (p); e.panic();
        auto L4 = renderNote (e, 69, 100, 0.6, 0.6);
        CHECK_NEAR (zcFreq (L4.data(), 4800, 24000, SR), 2000.0, 3.0);
    }

    // ---------------- loop continuity ----------------
    SECTION ("loop continuity");
    {
        TestFont tf;
        Engine e; e.prepare (SR, 256); e.setFont (&tf.font);
        auto L = renderNote (e, 69, 100, 2.0, 2.0);   // loops many times
        CHECK (rmsOf (L.data(), 72000, 96000) > 0.05);// still sounding late
        // max step of a 1 kHz sine at 48k with our gains, generous headroom
        float maxStep = 0;
        for (size_t i = 48000; i < 96000; ++i)
            maxStep = std::max (maxStep, std::abs (L[i] - L[i - 1]));
        CHECK (maxStep < 0.20f);                      // seam would spike far above
    }

    // ---------------- loop mode 3 (until release) ----------------
    SECTION ("loop-until-release");
    {
        TestFont tf1 (1, 0.0f, 2.0f), tf3 (3, 0.0f, 2.0f);
        Engine e1; e1.prepare (SR, 256); e1.setFont (&tf1.font);
        Engine e3; e3.prepare (SR, 256); e3.setFont (&tf3.font);
        auto L1 = renderNote (e1, 69, 100, 0.3, 1.2);
        auto L3 = renderNote (e3, 69, 100, 0.3, 1.2);
        // mode 1 keeps looping through the 2 s release; mode 3 runs off the
        // sample end (2 cycles + guard zeros) almost immediately.
        // Window 0.35..0.5 s = 50..200 ms after note-off (release barely begun).
        double late1 = rmsOf (L1.data(), 16800, 24000);
        double late3 = rmsOf (L3.data(), 16800, 24000);
        CHECK (late1 > 0.05);
        CHECK (late3 < late1 * 0.1);
    }

    // ---------------- relative envelope knobs ----------------
    SECTION ("relative ADSR scaling");
    {
        TestFont tf (1, 0.0f, 0.2f);
        Engine e; e.prepare (SR, 256); e.setFont (&tf.font);
        SynthParams p; p.releaseMul = 4.0f; e.setParams (p);
        auto Lslow = renderNote (e, 69, 100, 0.2, 1.4);
        e.panic(); p.releaseMul = 0.25f; e.setParams (p);
        auto Lfast = renderNote (e, 69, 100, 0.2, 1.4);
        // slow release still audible at 0.5 s after note-off; fast one is gone
        CHECK (rmsOf (Lslow.data(), 33600, 43200) > rmsOf (Lfast.data(), 33600, 43200) * 3.0);
        // attack scaling: x4 on a 1 ms attack is subtle; use sustain offset instead
        e.panic(); SynthParams p2; p2.sustainOffDb = -24.0f; e.setParams (p2);
        TestFont tfS (1, 0.0f, 0.05f);                // file sustain at full level
        e.setFont (&tfS.font);
        auto Lquiet = renderNote (e, 69, 100, 0.8, 0.8);
        e.panic(); e.setParams (SynthParams());
        auto Lfull = renderNote (e, 69, 100, 0.8, 0.8);
        double gq = rmsOf (Lquiet.data(), 24000, 36000), gf = rmsOf (Lfull.data(), 24000, 36000);
        CHECK_NEAR (gq / std::max (1e-9, gf), dbToGain (-24.0f), 0.05);
    }

    // ---------------- filter knobs + LFO ----------------
    SECTION ("filter knobs and synth-layer LFO");
    {
        // zone filter open; CUTOFF knob pulls it down onto the 1 kHz tone
        TestFont tf;
        Engine e; e.prepare (SR, 256); e.setFont (&tf.font);
        SynthParams p; p.cutoffCents = -4800.0f; e.setParams (p);   // ~20k -> ~1.25k
        auto Ldark = renderNote (e, 81, 100, 0.5, 0.5);             // 2 kHz tone
        e.panic(); e.setParams (SynthParams());
        auto Lopen = renderNote (e, 81, 100, 0.5, 0.5);
        CHECK (rmsOf (Ldark.data(), 12000, 24000) < rmsOf (Lopen.data(), 12000, 24000) * 0.7);

        // vibrato: pitch-target LFO must spread the zero-crossing freq
        e.panic();
        SynthParams pv; pv.lfoTarget = 0; pv.lfoDepth = 1.0f; pv.lfoRateHz = 6.0f; e.setParams (pv);
        auto Lvib = renderNote (e, 69, 100, 1.0, 1.0);
        double fA = zcFreq (Lvib.data(), 10000, 14000, SR);
        double fB = zcFreq (Lvib.data(), 14000, 18000, SR);
        bool moved = std::fabs (fA - fB) > 5.0;
        if (! moved)  // windows may straddle the same LFO half — widen once
        {
            fB = zcFreq (Lvib.data(), 16000, 20000, SR);
            moved = std::fabs (fA - fB) > 5.0;
        }
        CHECK (moved);
        // depth 0 == no vibrato
        e.panic(); e.setParams (SynthParams());
        auto Lflat = renderNote (e, 69, 100, 1.0, 1.0);
        CHECK_NEAR (zcFreq (Lflat.data(), 10000, 14000, SR),
                    zcFreq (Lflat.data(), 30000, 34000, SR), 1.0);
    }

    // ---------------- glide ----------------
    SECTION ("glide");
    {
        TestFont tf;
        Engine e; e.prepare (SR, 256); e.setFont (&tf.font);
        SynthParams p; p.glideS = 0.25f; e.setParams (p);
        // establish origin note
        std::vector<float> L ((size_t) SR, 0.0f), R ((size_t) SR, 0.0f);
        e.noteOn (57, 100);                            // 500 Hz
        e.process (L.data(), R.data(), 4800, 120.0);
        e.noteOff (57);
        e.noteOn (69, 100);                            // glide up to 1 kHz
        e.process (L.data(), R.data(), 48000, 120.0);
        double early = zcFreq (L.data(), 1000, 5000, SR);
        double late  = zcFreq (L.data(), 30000, 40000, SR);
        CHECK (early < 940.0 && early > 480.0);        // starts below target
        CHECK_NEAR (late, 1000.0, 8.0);                // lands on target
    }

    // ---------------- exclusive class + polyphony ----------------
    SECTION ("exclusive class and polyphony pressure");
    {
        TestFont tf;
        tf.font.presets[0].zones[0].exclusiveClass = 1;
        Engine e; e.prepare (SR, 256); e.setFont (&tf.font);
        std::vector<float> L (9600, 0.0f), R (9600, 0.0f);
        e.noteOn (69, 100);
        e.process (L.data(), R.data(), 2400, 120.0);
        CHECK (e.activeVoices() == 1);
        e.noteOn (71, 100);                            // chokes the first
        e.process (L.data(), R.data(), 4800, 120.0);   // 100 ms > 8 ms choke fade
        CHECK (e.activeVoices() == 1);

        TestFont tf2;                                  // no exclusive class
        Engine e2; e2.prepare (SR, 256); e2.setFont (&tf2.font);
        for (int k = 40; k < 80; ++k) e2.noteOn (k, 100);
        std::vector<float> L2 (14400, 0.0f), R2 (14400, 0.0f);
        e2.process (L2.data(), R2.data(), 14400, 120.0);
        CHECK (allFinite (L2.data(), 14400));
        CHECK (e2.activeVoices() <= Engine::kMaxNotes + 4);  // fading strays allowed briefly
        e2.process (L2.data(), R2.data(), 14400, 120.0);
        CHECK (e2.activeVoices() <= Engine::kMaxNotes);
    }

    // ---------------- sustain pedal ----------------
    SECTION ("sustain pedal");
    {
        TestFont tf (1, 0.0f, 0.03f);
        Engine e; e.prepare (SR, 256); e.setFont (&tf.font);
        std::vector<float> L (48000, 0.0f), R (48000, 0.0f);
        e.sustain (true);
        e.noteOn (69, 100);
        e.process (L.data(), R.data(), 4800, 120.0);
        e.noteOff (69);                                // held by pedal
        e.process (L.data() + 4800, R.data() + 4800, 19200, 120.0);
        CHECK (rmsOf (L.data(), 19200, 24000) > 0.05); // still sounding 0.4 s later
        e.sustain (false);                             // now it releases (30 ms)
        e.process (L.data() + 24000, R.data() + 24000, 12000, 120.0);
        CHECK (rmsOf (L.data(), 31200, 36000) < 0.01);
    }

    // ---------------- FX ----------------
    SECTION ("FX: crush");
    {
        Crush c; c.prepare (SR);
        std::vector<float> L (4800), R (4800), L0 (4800), R0 (4800);
        for (int i = 0; i < 4800; ++i)
            L[(size_t)i] = R[(size_t)i] = L0[(size_t)i] = R0[(size_t)i] =
                0.5f * (float) std::sin (2.0 * M_PI * 220.0 * i / SR);
        c.set (0.0f, 0.0f); c.process (L.data(), R.data(), 4800);
        CHECK (std::memcmp (L.data(), L0.data(), sizeof (float) * 4800) == 0);  // transparent at zero
        c.set (0.8f, 0.5f); c.process (L.data(), R.data(), 4800);
        double diff = 0; for (int i = 0; i < 4800; ++i) diff += std::fabs (L[(size_t)i] - L0[(size_t)i]);
        CHECK (diff / 4800.0 > 0.01);
        CHECK (allFinite (L.data(), 4800) && peakOf (L.data(), 4800) < 1.2f);
    }
    SECTION ("FX: crush transparent at plugin defaults");
    {
        // Regression guard for the default-state artifact: "crushrate" once
        // defaulted to 30% while "crush" (bit-depth) defaulted to 0. CRUSH only
        // bypasses when BOTH amount and rate are ~0, so the downsampler ran at
        // startup and put aliasing on every note. The transparent-at-zero test
        // above missed it because it uses explicit zeros, not the factory
        // defaults. These constants mirror PluginProcessor createLayout() —
        // keep them in sync.
        const float kCrushDefault     = 0.0f;   // "crush"     default 0%  (/100)
        const float kCrushRateDefault = 0.0f;   // "crushrate" default 0%  — MUST be 0
        std::vector<float> sine (4800);
        for (int i = 0; i < 4800; ++i)
            sine[(size_t)i] = 0.5f * (float) std::sin (2.0 * M_PI * 220.0 * i / SR);

        Crush c; c.prepare (SR);
        std::vector<float> L (sine), R (sine);
        c.set (kCrushDefault, kCrushRateDefault); c.process (L.data(), R.data(), 4800);
        CHECK (std::memcmp (L.data(), sine.data(), sizeof (float) * 4800) == 0);  // clean at factory defaults

        // ...but standalone downsampling (amount 0, rate up) must still colour,
        // so this fix can't be "gate rate on amount" — that would drop a feature.
        Crush c2; c2.prepare (SR);
        std::vector<float> L2 (sine), R2 (sine);
        c2.set (0.0f, 0.5f); c2.process (L2.data(), R2.data(), 4800);
        double d = 0; for (int i = 0; i < 4800; ++i) d += std::fabs (L2[(size_t)i] - sine[(size_t)i]);
        CHECK (d / 4800.0 > 0.001);   // rate alone still audibly downsamples
    }
    SECTION ("FX: chorus");
    {
        Chorus c; c.prepare (SR);
        std::vector<float> L (4800, 0.25f), R (4800, 0.25f);
        c.set (0.0f, 1.0f); c.process (L.data(), R.data(), 4800);
        CHECK_NEAR (L[1000], 0.25f, 1e-6);
        c.set (0.8f, 1.0f); c.process (L.data(), R.data(), 4800);
        CHECK (allFinite (L.data(), 4800));
    }
    SECTION ("FX: delay timing");
    {
        Delay d; d.prepare (SR);
        d.set (1.0f, 4, 0.0f, 120.0);                  // 1/4 @120 = 0.5 s = 24000 smp
        std::vector<float> L (60000, 0.0f), R (60000, 0.0f);
        d.process (L.data(), R.data(), 24000);         // settle the time smoother
        std::vector<float> L2 (60000, 0.0f), R2 (60000, 0.0f);
        L2[0] = 1.0f; R2[0] = 1.0f;
        d.process (L2.data(), R2.data(), 60000);
        int peakAt = 1; float pk = 0;
        for (int i = 1000; i < 60000; ++i)
            if (std::abs (L2[(size_t) i]) > pk) { pk = std::abs (L2[(size_t) i]); peakAt = i; }
        CHECK (pk > 0.3f);
        CHECK_NEAR ((double) peakAt, 24000.0, 400.0);
        Delay d0; d0.prepare (SR); d0.set (0.0f, 4, 0.4f, 120.0);
        std::vector<float> L3 (4800, 0.5f), R3 (4800, 0.5f);
        d0.process (L3.data(), R3.data(), 4800);
        CHECK_NEAR (L3[2400], 0.5f, 1e-5);             // mix 0 transparent
    }
    SECTION ("FX: reverb decay");
    {
        Reverb r; r.prepare (SR);
        r.set (1.0f, 0.35f);
        std::vector<float> L (144000, 0.0f), R (144000, 0.0f);
        L[0] = R[0] = 1.0f;
        r.process (L.data(), R.data(), 144000);
        CHECK (allFinite (L.data(), 144000));
        double e0 = rmsOf (L.data(), 2400, 24000);
        double e1 = rmsOf (L.data(), 72000, 96000);
        CHECK (e0 > 1e-4);                             // there is a tail
        CHECK (e1 < e0 * 0.5);                         // and it decays
        // bigger size -> decays slower (relative energy later in the tail)
        Reverb r2; r2.prepare (SR); r2.set (1.0f, 0.9f);
        std::vector<float> L2 (144000, 0.0f), R2 (144000, 0.0f);
        L2[0] = R2[0] = 1.0f;
        r2.process (L2.data(), R2.data(), 144000);
        double f0 = rmsOf (L2.data(), 2400, 24000), f1 = rmsOf (L2.data(), 72000, 96000);
        CHECK (f1 / std::max (1e-12, f0) > e1 / std::max (1e-12, e0));
        // mix 0 transparent
        Reverb r3; r3.prepare (SR); r3.set (0.0f, 0.5f);
        std::vector<float> L4 (4800, 0.3f), R4 (4800, 0.3f);
        r3.process (L4.data(), R4.data(), 4800);
        CHECK_NEAR (L4[2000], 0.3f, 1e-6);
    }

    // ---------------- width & master ----------------
    SECTION ("width and master volume");
    {
        TestFont tf;
        tf.font.presets[0].zones[0].pan = -1.0f;       // hard left
        Engine e; e.prepare (SR, 256); e.setFont (&tf.font);
        SynthParams p; p.width = 0.0f; e.setParams (p);
        std::vector<float> R;
        auto L = renderNote (e, 69, 100, 0.4, 0.4, &R);
        double d = 0; for (size_t i = 9600; i < 19200; ++i) d = std::max (d, (double) std::fabs (L[i] - R[i]));
        CHECK (d < 1e-6);                              // width 0 collapses to mono
        e.panic(); SynthParams p2; p2.masterDb = -24.0f; e.setParams (p2);
        auto Lq = renderNote (e, 69, 100, 0.4, 0.4);
        e.panic(); e.setParams (SynthParams());
        auto Lf = renderNote (e, 69, 100, 0.4, 0.4);
        CHECK_NEAR (rmsOf (Lq.data(), 9600, 19200) / std::max (1e-12, rmsOf (Lf.data(), 9600, 19200)),
                    dbToGain (-24.0f), 0.02);
    }

    // ---------------- parser robustness ----------------
    SECTION ("parser robustness");
    {
        SoundFont sf; std::string err;
        std::vector<uint8_t> junk (4096, 0xAB);
        CHECK (! sf.load (junk.data(), junk.size(), err));
        std::memcpy (junk.data(), "RIFF", 4); std::memcpy (junk.data() + 8, "sfbk", 4);
        CHECK (! sf.load (junk.data(), junk.size(), err));   // sfbk but no chunks
        CHECK (! sf.load (junk.data(), 4, err));             // absurdly short
    }

    // ---------------- the real cart ----------------
    SECTION ("real soundfont integration");
    {
        std::string path;
        const char* candidates[] = {
            argc > 1 ? argv[1] : "",
            "../../Pokemon_RSE_HD_Soundfont_Release.sf2",
            "/mnt/user-data/uploads/Claude Plugin Work Folder/Pokemon_RSE_HD_Soundfont_Release.sf2"
        };
        for (const char* c : candidates)
            if (c && *c) { std::FILE* f = std::fopen (c, "rb"); if (f) { std::fclose (f); path = c; break; } }

        if (path.empty())
            std::printf ("  (no .sf2 found — skipping real-file tests)\n");
        else
        {
            SoundFont sf; std::string err;
            CHECK (sf.loadFile (path.c_str(), err));
            if (err.empty())
            {
                CHECK ((int) sf.presets.size() == 24);
                CHECK ((int) sf.samples.size() == 80);
                const Preset* trumpet = sf.findPreset (0, 56);
                CHECK (trumpet != nullptr && trumpet->name == "Trumpet");
                // structural sanity on every zone
                bool zonesOk = true, loopsOk = true;
                int totalZones = 0;
                for (auto& pr : sf.presets)
                    for (auto& z : pr.zones)
                    {
                        ++totalZones;
                        if (z.sampleIndex < 0 || z.sampleIndex >= (int) sf.samples.size()) zonesOk = false;
                        auto& sh = sf.samples[(size_t) z.sampleIndex];
                        if (sh.end > sf.pool.size()) zonesOk = false;
                        if (z.sampleModes == 1 && sh.loopEnd <= sh.loopStart) loopsOk = false;
                    }
                CHECK (zonesOk);
                CHECK (loopsOk);
                CHECK (totalZones >= 24);
                std::printf ("  cart: %s — %d presets, %d zones\n",
                             sf.name.c_str(), (int) sf.presets.size(), totalZones);

                // every preset must sound: pick a key inside the first zone
                Engine e; e.prepare (SR, 256);
                e.setFont (&sf);
                int silent = 0;
                for (int pi = 0; pi < (int) sf.presets.size(); ++pi)
                {
                    e.setPresetIndex (pi);
                    auto& zs = sf.presets[(size_t) pi].zones;
                    if (zs.empty()) { ++silent; continue; }
                    int key = std::clamp ((zs[0].keyLo + zs[0].keyHi) / 2, 0, 127);
                    e.panic();
                    auto L = renderNote (e, key, 100, 0.25, 0.35);
                    if (! allFinite (L.data(), (int) L.size())) { ++failed; std::printf("  FAIL preset %d NaN\n", pi); continue; }
                    if (peakOf (L.data(), (int) L.size()) < 1e-5f) ++silent;
                }
                CHECK (silent == 0);
                // stereo pairs: Trumpet note produces different L and R
                e.setPresetIndex ((int) (trumpet - &sf.presets[0]));
                e.panic();
                std::vector<float> R;
                auto L = renderNote (e, 60, 100, 0.3, 0.3, &R);
                double d = 0; for (size_t i = 4800; i < 12000; ++i) d = std::max (d, (double) std::fabs (L[i] - R[i]));
                CHECK (d > 1e-4);
            }
            else std::printf ("  load error: %s\n", err.c_str());
        }
    }

    std::printf ("\n%d assertions: %d passed, %d FAILED\n", passed + failed, passed, failed);
    return failed == 0 ? 0 : 1;
}
