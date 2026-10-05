#pragma once
// CeladonFX.h — the retro FX chain, JUCE-free: CRUSH (bit depth + sample-rate
// reduction), CHORUS (2 voices), DELAY (tempo-synced stereo echo), REVERB
// (small 8-line FDN with T60-accurate decay gains — the Atrium lesson: decay
// gains must track delay lengths through every code path).

#include <cmath>
#include <vector>
#include <algorithm>
#include "CeladonDSP.h"

namespace celadon
{

// ------------------------------- CRUSH ------------------------------------
class Crush
{
public:
    void prepare (double sampleRate) { sr = sampleRate; reset(); }
    void reset() { holdL = holdR = 0.0f; acc = 1.0f; }

    // amount 0..1 morphs 16 -> 3 bits; rate 0..1 morphs sr -> sr/24
    void set (float amount_, float rate_) { amount = amount_; rate = rate_; }

    void process (float* L, float* R, int n)
    {
        if (amount < 0.001f && rate < 0.001f) return;              // transparent
        const float bits = 16.0f - 13.0f * amount;
        const float q = std::pow (2.0f, bits - 1.0f);
        const float step = 1.0f / (1.0f + 23.0f * rate);           // fraction of samples kept
        for (int i = 0; i < n; ++i)
        {
            acc += step;
            if (acc >= 1.0f)
            {
                acc -= 1.0f;
                holdL = std::round (L[i] * q) / q;
                holdR = std::round (R[i] * q) / q;
            }
            L[i] = holdL; R[i] = holdR;
        }
    }

private:
    double sr = 48000.0;
    float amount = 0.0f, rate = 0.0f, holdL = 0.0f, holdR = 0.0f, acc = 1.0f;
};

// ------------------------------- CHORUS -----------------------------------
class Chorus
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        buf.assign ((size_t) (sr * 0.06) + 8, 0.0f);
        w = 0; ph1 = 0.0f; ph2 = 0.25f;
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); }

    void set (float mix_, float rateHz_) { mix = mix_; rateHz = rateHz_; }

    void process (float* L, float* R, int n)
    {
        if (mix < 0.001f) return;
        const float base = 0.015f * (float) sr, depth = 0.006f * (float) sr;
        const float inc1 = rateHz / (float) sr, inc2 = rateHz * 1.127f / (float) sr;
        const int len = (int) buf.size();
        const float dry = std::cos (mix * 1.5707963f), wet = std::sin (mix * 1.5707963f);
        for (int i = 0; i < n; ++i)
        {
            buf[(size_t) w] = 0.5f * (L[i] + R[i]);
            ph1 += inc1; ph1 -= std::floor (ph1);
            ph2 += inc2; ph2 -= std::floor (ph2);
            float d1 = base + depth * (0.5f + 0.5f * std::sin (6.2831853f * ph1));
            float d2 = base + depth * (0.5f + 0.5f * std::sin (6.2831853f * ph2));
            L[i] = dry * L[i] + wet * read ((float) w - d1, len);
            R[i] = dry * R[i] + wet * read ((float) w - d2, len);
            if (++w >= len) w = 0;
        }
    }

private:
    float read (float pos, int len) const
    {
        while (pos < 0) pos += (float) len;
        int i0 = (int) pos; float t = pos - (float) i0;
        int i1 = i0 + 1; if (i1 >= len) i1 = 0;
        return buf[(size_t) i0] * (1.0f - t) + buf[(size_t) i1] * t;
    }
    double sr = 48000.0;
    std::vector<float> buf;
    int w = 0;
    float ph1 = 0.0f, ph2 = 0.25f, mix = 0.0f, rateHz = 0.8f;
};

// ------------------------------- DELAY ------------------------------------
class Delay
{
public:
    // division indices (UI + parameter share this table)
    static constexpr int kNumDivs = 9;
    static const char* divName (int i)
    {
        static const char* names[kNumDivs] = { "1/16", "1/8T", "1/8", "1/8D", "1/4", "1/4D", "1/2", "1/2D", "1/1" };
        return names[std::clamp (i, 0, kNumDivs - 1)];
    }
    static float divBeats (int i)
    {
        static const float beats[kNumDivs] = { 0.25f, 1.0f/3.0f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f };
        return beats[std::clamp (i, 0, kNumDivs - 1)];
    }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        bufL.assign ((size_t) (sr * 4.2) + 8, 0.0f);
        bufR.assign (bufL.size(), 0.0f);
        w = 0;
        timeSm.setup (0.08f, sr, 0.25f * (float) sr);
    }
    void reset() { std::fill (bufL.begin(), bufL.end(), 0.0f);
                   std::fill (bufR.begin(), bufR.end(), 0.0f); }

    void set (float mix_, int divIndex, float feedback_, double bpm)
    {
        mix = mix_; feedback = std::clamp (feedback_, 0.0f, 0.95f);
        double beats = (double) divBeats (divIndex);
        targetSmp = (float) std::min ((double) bufL.size() - 8.0, beats * 60.0 / std::max (20.0, bpm) * sr);
    }

    void process (float* L, float* R, int n)
    {
        const int len = (int) bufL.size();
        for (int i = 0; i < n; ++i)
        {
            float d = timeSm.next (targetSmp);                    // tape-style time slew
            float rp = (float) w - d;
            float tapL = read (bufL, rp, len), tapR = read (bufR, rp, len);
            bufL[(size_t) w] = L[i] + tapR * feedback;            // gentle ping-pong
            bufR[(size_t) w] = R[i] + tapL * feedback;
            L[i] += tapL * mix;
            R[i] += tapR * mix;
            if (++w >= len) w = 0;
        }
    }

private:
    static float read (const std::vector<float>& b, float pos, int len)
    {
        while (pos < 0) pos += (float) len;
        while (pos >= (float) len) pos -= (float) len;
        int i0 = (int) pos; float t = pos - (float) i0;
        int i1 = i0 + 1; if (i1 >= len) i1 = 0;
        return b[(size_t) i0] * (1.0f - t) + b[(size_t) i1] * t;
    }
    double sr = 48000.0;
    std::vector<float> bufL, bufR;
    int w = 0;
    float mix = 0.0f, feedback = 0.45f, targetSmp = 12000.0f;
    Smoother timeSm;
};

// ------------------------------- REVERB -----------------------------------
// Compact 8-line FDN, Householder feedback, per-line damping, 10 ms predelay.
// g_i = 10^(-3 * len_i / (T60 * sr)) recomputed wherever lengths change.
class Reverb
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        // mutually-prime-ish base lengths (seconds) for a small room
        static const float baseS[kLines] = { 0.0297f, 0.0371f, 0.0411f, 0.0437f,
                                             0.0533f, 0.0623f, 0.0797f, 0.0919f };
        for (int i = 0; i < kLines; ++i)
        {
            maxLen[i] = (int) (baseS[i] * 2.3 * sr) + 8;
            line[i].assign ((size_t) maxLen[i], 0.0f);
            wp[i] = 0; lpState[i] = 0.0f; baseSec[i] = baseS[i];
        }
        pre.assign ((size_t) (0.010 * sr) + 8, 0.0f); preW = 0;
        setSize (0.5f); computeGains (t60);
    }
    void reset()
    {
        for (int i = 0; i < kLines; ++i) { std::fill (line[i].begin(), line[i].end(), 0.0f); lpState[i] = 0.0f; }
        std::fill (pre.begin(), pre.end(), 0.0f);
    }

    // mix 0..1 ; size 0..1 (scales lengths 0.6..2.2x and T60 0.4..4 s)
    void set (float mix_, float size_)
    {
        mix = mix_;
        if (std::abs (size_ - size) > 1.0e-4f) { size = size_; setSize (size); }
    }

    void process (float* L, float* R, int n)
    {
        if (mix < 0.001f) return;
        const float dry = 1.0f, wet = mix * 0.7f;
        for (int s = 0; s < n; ++s)
        {
            float in = 0.5f * (L[s] + R[s]);
            pre[(size_t) preW] = in;
            int rp = preW + 1; if (rp >= (int) pre.size()) rp = 0;
            float x = pre[(size_t) rp];
            if (++preW >= (int) pre.size()) preW = 0;

            float out[kLines], sum = 0.0f;
            for (int i = 0; i < kLines; ++i)
            {
                out[i] = line[i][(size_t) wp[i]];
                sum += out[i];
            }
            const float h = sum * 0.25f;                          // Householder: y_i - 2/N * sum
            float wetL = 0.0f, wetR = 0.0f;
            for (int i = 0; i < kLines; ++i)
            {
                float fb = (out[i] - h) * g[i];
                lpState[i] += damp * (fb - lpState[i]);           // in-loop damping
                float v = x * inGain[i] + lpState[i];
                v += 1.0e-20f; v -= 1.0e-20f;                     // denormal guard
                line[i][(size_t) wp[i]] = v;
                if (++wp[i] >= len[i]) wp[i] = 0;
                ((i & 1) ? wetR : wetL) += out[i] * tap[i];
            }
            L[s] = dry * L[s] + wet * wetL;
            R[s] = dry * R[s] + wet * wetR;
        }
    }

private:
    static constexpr int kLines = 8;

    void setSize (float sz)
    {
        float scale = 0.6f + 1.6f * sz;
        t60 = 0.4f + 3.6f * sz * sz;
        for (int i = 0; i < kLines; ++i)
        {
            len[i] = std::min (maxLen[i] - 1, std::max (32, (int) (baseSec[i] * scale * sr)));
            if (wp[i] >= len[i]) wp[i] = 0;
        }
        computeGains (t60);                                       // gains track lengths, always
    }
    void computeGains (float T60)
    {
        for (int i = 0; i < kLines; ++i)
        {
            g[i] = std::pow (10.0f, -3.0f * (float) len[i] / (T60 * (float) sr));
            inGain[i]  = 0.25f;
            tap[i] = (i % 4 < 2 ? 0.5f : -0.5f);
        }
    }

    double sr = 48000.0;
    std::vector<float> line[kLines], pre;
    int   maxLen[kLines] {}, len[kLines] {}, wp[kLines] {}, preW = 0;
    float baseSec[kLines] {}, g[kLines] {}, inGain[kLines] {}, tap[kLines] {}, lpState[kLines] {};
    float damp = 0.55f, mix = 0.0f, size = -1.0f, t60 = 1.4f;
};

} // namespace celadon
