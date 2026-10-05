#pragma once
// CeladonDSP.h — JUCE-free DSP primitives: SF2-correct DAHDSR envelopes,
// TPT state-variable lowpass (Zavalishin), triangle LFO, Catmull-Rom reader.
// Curve shapes follow the spec: convex attack (linear in amplitude),
// decay/release linear in dB over a 96 dB full-scale span.

#include <cmath>
#include <algorithm>

namespace celadon
{

constexpr float kSilenceDb = -96.0f;

inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g)  { return g > 1.0e-6f ? 20.0f * std::log10 (g) : kSilenceDb; }

// ---------------- volume envelope (DAHDSR, dB-domain decay) ----------------
class VolEnv
{
public:
    enum Phase { PDelay, PAttack, PHold, PDecay, PSustain, PRelease, PDone };

    void setup (float delayS, float attackS, float holdS, float decayS,
                float sustainDb, float releaseS, double sampleRate)
    {
        sr = (float) sampleRate;
        delaySmp  = (int) (std::max (0.0f, delayS) * sr);
        attackSmp = std::max (1, (int) (attackS * sr));
        holdSmp   = (int) (holdS * sr);
        // decay/release rates: full-scale span (96 dB) over the given time
        decayDbPerSmp   = 96.0f / std::max (1.0f, decayS * sr);
        releaseDbPerSmp = 96.0f / std::max (1.0f, releaseS * sr);
        susDb = std::min (0.0f, -sustainDb);          // stored as level dB (<= 0)
        if (susDb < kSilenceDb) susDb = kSilenceDb;
        phase = PDelay; n = 0; amp = 0.0f; db = 0.0f;
    }

    void noteOff()
    {
        if (phase == PDone || phase == PRelease) return;
        // release starts from the current level, wherever we are
        if (phase == PAttack) db = gainToDb (amp);
        phase = PRelease;
    }
    void kill() { phase = PDone; }
    bool done()   const { return phase == PDone; }
    bool released() const { return phase == PRelease || phase == PDone; }

    float next()
    {
        switch (phase)
        {
            case PDelay:
                if (++n > delaySmp) { phase = PAttack; n = 0; }
                return 0.0f;
            case PAttack:
                amp = (float) ++n / (float) attackSmp;         // linear in amplitude (convex in dB)
                if (n >= attackSmp) { amp = 1.0f; phase = PHold; n = 0; db = 0.0f; }
                return amp;
            case PHold:
                if (++n > holdSmp) { phase = PDecay; }
                return 1.0f;
            case PDecay:
                db -= decayDbPerSmp;                            // linear in dB
                if (db <= susDb)
                {
                    db = susDb;
                    phase = (susDb <= kSilenceDb ? PDone : PSustain);
                }
                amp = dbToGain (db);
                return amp;
            case PSustain:
                return amp;
            case PRelease:
                db -= releaseDbPerSmp;
                if (db <= kSilenceDb) { phase = PDone; amp = 0.0f; return 0.0f; }
                amp = dbToGain (db);
                return amp;
            default: return 0.0f;
        }
    }

private:
    float sr = 48000.0f;
    int   delaySmp = 0, attackSmp = 1, holdSmp = 0, n = 0;
    float decayDbPerSmp = 1.0f, releaseDbPerSmp = 1.0f;
    float susDb = 0.0f, db = 0.0f, amp = 0.0f;
    Phase phase = PDone;
};

// ---------------- modulation envelope (DAHDSR, linear 0..1) ----------------
class ModEnv
{
public:
    void setup (float delayS, float attackS, float holdS, float decayS,
                float sustainLevel, float releaseS, double sampleRate)
    {
        float sr = (float) sampleRate;
        delaySmp  = (int) (std::max (0.0f, delayS) * sr);
        attackSmp = std::max (1, (int) (attackS * sr));
        holdSmp   = (int) (holdS * sr);
        decayStep   = 1.0f / std::max (1.0f, decayS * sr);       // full-scale 1 -> 0
        releaseStep = 1.0f / std::max (1.0f, releaseS * sr);
        sus = std::clamp (sustainLevel, 0.0f, 1.0f);
        phase = 0; n = 0; v = 0.0f;
    }
    void noteOff() { if (phase < 5) phase = 5; }
    float next()
    {
        switch (phase)
        {
            case 0: if (++n > delaySmp) { phase = 1; n = 0; } return 0.0f;
            case 1: v = (float) ++n / (float) attackSmp;
                    if (n >= attackSmp) { v = 1.0f; phase = 2; n = 0; }
                    return v;
            case 2: if (++n > holdSmp) phase = 3; return 1.0f;
            case 3: v -= decayStep; if (v <= sus) { v = sus; phase = 4; } return v;
            case 4: return v;
            case 5: v -= releaseStep; if (v <= 0.0f) { v = 0.0f; phase = 6; } return v;
            default: return 0.0f;
        }
    }
    float value() const { return phase == 0 ? 0.0f : v; }

private:
    int delaySmp = 0, attackSmp = 1, holdSmp = 0, n = 0, phase = 6;
    float decayStep = 1.0f, releaseStep = 1.0f, sus = 1.0f, v = 0.0f;
};

// ---------------- triangle LFO (starts at 0, rises; spec 8.1.2) -----------
class TriLfo
{
public:
    void setup (float freqHz, float delayS, double sampleRate)
    {
        inc = std::max (0.0f, freqHz) / (float) sampleRate;
        delaySmp = (int) (std::max (0.0f, delayS) * sampleRate);
        phase = 0.0f; n = 0;
    }
    // advance by `samples`, return value in -1..1
    float tick (int samples)
    {
        if (n < delaySmp) { n += samples; if (n < delaySmp) return 0.0f; }
        phase += inc * (float) samples;
        phase -= std::floor (phase);
        float p = phase;                       // 0..1 : 0->1 over first quarter... triangle:
        if (p < 0.25f) return p * 4.0f;        // 0 -> +1
        if (p < 0.75f) return 2.0f - p * 4.0f; // +1 -> -1
        return p * 4.0f - 4.0f;                // -1 -> 0
    }
private:
    float inc = 0.0f, phase = 0.0f;
    int delaySmp = 0, n = 0;
};

// ------------- TPT state-variable lowpass (Zavalishin), per voice ----------
class SVF
{
public:
    void reset() { ic1 = ic2 = 0.0f; }

    // fcHz clamped inside; peakDb = SF2 resonance (peak height above DC gain)
    void set (float fcHz, float peakDb, double sampleRate)
    {
        float sr = (float) sampleRate;
        float fc = std::clamp (fcHz, 10.0f, sr * 0.45f);
        g = std::tan (3.14159265f * fc / sr);
        peakDb = std::clamp (peakDb, 0.0f, 30.0f);        // sanity cap well below spec's 96
        float q = std::max (0.5f, std::pow (10.0f, peakDb / 20.0f));
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        comp = dbToGain (-0.5f * peakDb);                 // spec: DC gain drops by half the Q
    }

    inline float lp (float x)
    {
        float v1 = a1 * (ic1 + g * (x - ic2));
        float v2 = ic2 + g * v1;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        // denormal guard
        ic1 += 1.0e-20f; ic1 -= 1.0e-20f;
        return v2 * comp;
    }

private:
    float g = 0.5f, k = 1.0f, a1 = 0.5f, comp = 1.0f, ic1 = 0.0f, ic2 = 0.0f;
};

// ---------------- one-pole smoother --------------------------------------
class Smoother
{
public:
    void setup (float timeS, double sampleRate, float initial = 0.0f)
    {
        a = 1.0f - std::exp (-1.0f / std::max (1.0f, (float) (timeS * sampleRate)));
        v = initial;
    }
    void snap (float x) { v = x; }
    inline float next (float target) { v += a * (target - v); return v; }
    float value() const { return v; }
private:
    float a = 1.0f, v = 0.0f;
};

// ---- 4-point Catmull-Rom interpolation (FluidSynth-default quality tier) --
inline float catmullRom (float xm1, float x0, float x1, float x2, float t)
{
    float c0 = x0;
    float c1 = 0.5f * (x1 - xm1);
    float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

} // namespace celadon
