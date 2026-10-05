#pragma once
// CeladonEngine.h — the JUCE-free synth core. Honors the soundfont layer
// (zones, loops, stereo pairs, file envelopes/LFOs/filter, tuning, pan,
// exclusive class) and adds the synth layer on top (relative ADSR scaling,
// user mod envelope -> filter, global LFO with target switch, glide, tune,
// velocity sensitivity, width, master FX chain).
//
// Threading contract: noteOn/noteOff/cc/process are audio-thread only.
// setFont must be called with processing suspended (the processor does).

#include <cstdint>
#include <cmath>
#include <vector>
#include <algorithm>
#include "CeladonSF2.h"
#include "CeladonDSP.h"
#include "CeladonFX.h"

namespace celadon
{

struct SynthParams
{
    // ENVELOPE — relative to the zone's own values (centre = neutral)
    float attackMul = 1.0f, decayMul = 1.0f, releaseMul = 1.0f;   // x0.25 .. x4
    float sustainOffDb = 0.0f;                                    // -24 .. +24 dB

    // user mod envelope (ALT set) — absolute
    float mA = 0.005f, mD = 0.3f, mS = 0.0f, mR = 0.2f;           // seconds / level 0..1

    // FILTER — offsets around the zone's values
    float cutoffCents = 0.0f;                                     // -4800 .. +4800
    float resoDb = 0.0f;                                          // 0 .. 24
    float envAmtCents = 0.0f;                                     // -4800 .. +4800
    float keytrack = 0.0f;                                        // cents per key, 0..100

    // LFO (synth layer)
    int   lfoTarget = 1;                                          // 0 pitch, 1 filter, 2 amp
    bool  lfoSync = false;
    float lfoRateHz = 2.0f;
    int   lfoDivIndex = 4;                                        // Delay::divName table
    float lfoDepth = 0.0f;                                        // 0..1
    float lfoDelayS = 0.0f;

    // SOUND
    float glideS = 0.0f;
    float tuneSemis = 0.0f;
    float velSens = 1.0f;                                         // 0..2
    float width = 1.0f;                                           // 0..2

    // FX
    float crushAmt = 0.0f, crushRate = 0.0f;
    float chorusMix = 0.0f, chorusRateHz = 0.8f;
    float delayMix = 0.0f;  int delayDiv = 4; float delayFb = 0.45f;
    float reverbMix = 0.0f, reverbSize = 0.35f;

    float masterDb = 0.0f;                                        // -24 .. +6
};

class Voice
{
public:
    bool active = false;
    int64_t noteId = 0;
    int key = 0, vel = 0, exclusiveClass = 0, presetTag = -1;

    void start (const Zone& zn, const SoundFont& font, int key_, int vel_,
                int64_t id, int presetTag_, double sr_, const SynthParams& p,
                float glideFromKeyCents)
    {
        zone = zn; sr = sr_; noteId = id; presetTag = presetTag_;
        key = zone.keynumOverride >= 0 ? zone.keynumOverride : key_;
        vel = zone.velocityOverride >= 0 ? zone.velocityOverride : vel_;
        vel = std::clamp (vel, 1, 127);
        exclusiveClass = zone.exclusiveClass;

        const SampleHeader& sh = font.samples[(size_t) zone.sampleIndex];
        const int64_t poolSize = (int64_t) font.pool.size();
        auto cl = [poolSize] (int64_t v) { return (double) std::clamp (v, (int64_t) 0, poolSize - 1); };
        start_ = cl ((int64_t) sh.start + zone.startOff);
        end_   = cl ((int64_t) sh.end + zone.endOff);
        loopS_ = cl ((int64_t) sh.loopStart + zone.loopStartOff);
        loopE_ = cl ((int64_t) sh.loopEnd + zone.loopEndOff);
        if (loopE_ <= loopS_ + 3.0) zoneLoops = false;
        else zoneLoops = (zone.sampleModes == 1 || zone.sampleModes == 3);
        loopS_ = std::max (loopS_, start_);
        loopE_ = std::min (loopE_, end_);
        pos = start_;
        fileRate = (double) sh.rate;
        pool = font.pool.data();

        // key-scaled envelope times (gens 39/40), then the knob scaling
        float holdK  = (float) centsRatio (zone.holdKeyScale  * (60.0 - key));
        float decayK = (float) centsRatio (zone.decayKeyScale * (60.0 - key));
        volEnv.setup (zone.delay, zone.attack * p.attackMul, zone.hold * holdK,
                      zone.decay * decayK * p.decayMul,
                      std::clamp (zone.sustainDb - p.sustainOffDb, 0.0f, 96.0f + 24.0f),
                      zone.release * p.releaseMul, sr);

        const double cr = sr / (double) kBlock;                   // control rate
        float mHoldK  = (float) centsRatio (zone.mHoldKeyScale  * (60.0 - key));
        float mDecayK = (float) centsRatio (zone.mDecayKeyScale * (60.0 - key));
        fileModEnv.setup (zone.mDelay, zone.mAttack, zone.mHold * mHoldK,
                          zone.mDecay * mDecayK, zone.mSustain, zone.mRelease, cr);
        userModEnv.setup (0.0f, p.mA, 0.0f, p.mD, p.mS, p.mR, cr);
        fileModLfo.setup (zone.modLfoFreq, zone.modLfoDelay, cr);
        fileVibLfo.setup (zone.vibLfoFreq, zone.vibLfoDelay, cr);

        // velocity -> attenuation (the default modulator), scaled by VEL SENS
        velAttDb = std::min (96.0f, 40.0f * std::log10 (127.0f / (float) vel)) * p.velSens;

        // constant-power pan from the zone
        float panT = (zone.pan + 1.0f) * 0.25f * 3.14159265f;
        panL = std::cos (panT); panR = std::sin (panT);

        // glide (in key-cents domain)
        targetKeyCents = (float) key * (float) zone.scaleTuning;
        if (p.glideS > 0.001f && glideFromKeyCents > -1.0e8f)
        {
            curKeyCents = glideFromKeyCents;
            glideStep = (targetKeyCents - curKeyCents) / std::max (1.0f, (float) (p.glideS * sr / kBlock));
        }
        else { curKeyCents = targetKeyCents; glideStep = 0.0f; }

        svf.reset();
        stealFade = 1.0f; stealStep = 0.0f;
        age = 0; releasedFlag = false;
        active = true;
    }

    void noteOff()
    {
        volEnv.noteOff(); fileModEnv.noteOff(); userModEnv.noteOff();
        releasedFlag = true;
    }
    void fastRelease()      // exclusive-class choke / soft steal (~8 ms)
    {
        if (! active) return;
        stealStep = 1.0f / (float) (0.008 * sr);
        releasedFlag = true;
    }
    bool released() const { return releasedFlag; }
    float envelopeGain() const { return lastEnvGain; }

    static constexpr int kBlock = 32;

    // render one control block (up to kBlock samples), adding into L/R.
    // Returns false when the voice has finished.
    bool render (float* L, float* R, int n, const SynthParams& p,
                 float bendCents, float userLfoVal, double engineSr)
    {
        (void) engineSr;
        if (! active) return false;

        // ---- control-rate modulation ----
        if (curKeyCents != targetKeyCents)
        {
            curKeyCents += glideStep;
            if ((glideStep > 0 && curKeyCents >= targetKeyCents) ||
                (glideStep < 0 && curKeyCents <= targetKeyCents))
                curKeyCents = targetKeyCents;
        }
        const float fEnv = fileModEnv.next();
        const float uEnv = userModEnv.next();
        const float mLfo = fileModLfo.tick (1);
        const float vLfo = fileVibLfo.tick (1);

        float uLfoRamp = 1.0f;
        if (p.lfoDelayS > 0.001f)
        {
            float t = (float) age / (float) sr - p.lfoDelayS;
            uLfoRamp = std::clamp (t / 0.2f, 0.0f, 1.0f);
        }
        const float uLfo = userLfoVal * uLfoRamp * p.lfoDepth;

        // pitch
        float cents = curKeyCents
                    - (float) zone.rootKey * (float) zone.scaleTuning
                    + 100.0f * (float) zone.coarseTune + (float) zone.fineTune
                    + (float) zone.pitchCorr
                    + 100.0f * p.tuneSemis + bendCents
                    + vLfo * zone.vibLfoToPitch
                    + mLfo * zone.modLfoToPitch
                    + fEnv * zone.modEnvToPitch
                    + (p.lfoTarget == 0 ? uLfo * 200.0f : 0.0f);
        const double inc = centsRatio (cents) * fileRate / sr;

        // filter
        float fcCents = zone.filterFcCents + p.cutoffCents
                      + p.keytrack * (float) (key - 60)
                      + uEnv * p.envAmtCents
                      + fEnv * zone.modEnvToFilterFc
                      + mLfo * zone.modLfoToFilterFc
                      + (p.lfoTarget == 1 ? uLfo * 3600.0f : 0.0f);
        const float fcHz = (float) cents2hz (std::clamp (fcCents, 1500.0f, 14500.0f));
        svf.set (fcHz, zone.filterQdB + p.resoDb, sr);

        // block gain (everything but the per-sample volume envelope)
        float volDb = -zone.attenuationDb - velAttDb
                    + mLfo * zone.modLfoToVolumeDb
                    + (p.lfoTarget == 2 ? uLfo * -8.0f : 0.0f);
        const float blockGain = dbToGain (std::max (volDb, -120.0f));

        const bool looping = zoneLoops && ! (zone.sampleModes == 3 && releasedFlag);
        const double loopLen = loopE_ - loopS_;

        // ---- audio-rate loop ----
        for (int i = 0; i < n; ++i)
        {
            if (! looping && pos >= end_ - 1.0) { active = false; break; }

            const float smp = readInterp (looping, loopLen);
            const float flt = svf.lp (smp);
            float e = volEnv.next();
            if (stealStep > 0.0f)
            {
                stealFade -= stealStep;
                if (stealFade <= 0.0f) { active = false; break; }
            }
            const float out = flt * e * blockGain * stealFade;
            L[i] += out * panL;
            R[i] += out * panR;

            pos += inc;
            if (looping && pos >= loopE_) pos -= loopLen;
        }
        age += n;
        lastEnvGain = volEnv.done() ? 0.0f : 1.0f;
        if (volEnv.done()) active = false;
        return active;
    }

    float currentKeyCents() const { return targetKeyCents; }

private:
    inline float at (int64_t idx, bool looping, double loopLen) const
    {
        if (looping && (double) idx >= loopE_)
            idx -= (int64_t) loopLen;
        if ((double) idx > end_ + 2.0) return 0.0f;               // past guard zone
        if (idx < 0) idx = 0;
        return pool[idx];
    }
    inline float readInterp (bool looping, double loopLen) const
    {
        const int64_t i0 = (int64_t) pos;
        const float t = (float) (pos - (double) i0);
        return catmullRom (at (i0 - 1, looping, loopLen), at (i0, looping, loopLen),
                           at (i0 + 1, looping, loopLen), at (i0 + 2, looping, loopLen), t);
    }

    Zone zone;                       // by value: safe across preset switches
    const float* pool = nullptr;
    double sr = 48000.0, fileRate = 48000.0;
    double pos = 0.0, start_ = 0.0, end_ = 0.0, loopS_ = 0.0, loopE_ = 0.0;
    bool zoneLoops = false, releasedFlag = false;

    VolEnv volEnv;
    ModEnv fileModEnv, userModEnv;
    TriLfo fileModLfo, fileVibLfo;
    SVF svf;

    float velAttDb = 0.0f, panL = 0.707f, panR = 0.707f;
    float curKeyCents = 0.0f, targetKeyCents = 0.0f, glideStep = 0.0f;
    float stealFade = 1.0f, stealStep = 0.0f, lastEnvGain = 1.0f;
    int64_t age = 0;
};

//==============================================================================
class Engine
{
public:
    static constexpr int kMaxStreams = 40;   // mono sample streams
    static constexpr int kMaxNotes   = 16;   // the promised polyphony

    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        scratchL.assign ((size_t) std::max (maxBlock, Voice::kBlock), 0.0f);
        scratchR.assign (scratchL.size(), 0.0f);
        crush.prepare (sr); chorus.prepare (sr); delay.prepare (sr); reverb.prepare (sr);
        masterSm.setup (0.02f, sr, 1.0f);
        panic();
    }

    // message thread, with processing suspended
    void setFont (const SoundFont* f)
    {
        font = f;
        presetIdx = 0;
        panicVoices();
    }
    const SoundFont* currentFont() const { return font; }

    void setPresetIndex (int i)
    {
        if (font == nullptr || font->presets.empty()) { presetIdx = 0; return; }
        presetIdx = std::clamp (i, 0, (int) font->presets.size() - 1);
    }
    int  presetIndex() const { return presetIdx; }
    int  presetCount() const { return font ? (int) font->presets.size() : 0; }
    const char* presetName() const
    {
        if (font == nullptr || font->presets.empty()) return "NO CART";
        return font->presets[(size_t) presetIdx].name.c_str();
    }

    void setParams (const SynthParams& p) { params = p; }

    // ---- MIDI (audio thread) ----
    void noteOn (int key, int vel)
    {
        if (font == nullptr || font->presets.empty() || vel <= 0) return;
        const Preset& preset = font->presets[(size_t) presetIdx];

        // exclusive class chokes (spec 8.1.2 gen 57)
        for (auto& z : preset.zones)
            if (z.exclusiveClass > 0 && z.keyLo <= key && key <= z.keyHi)
                for (auto& v : voices)
                    if (v.active && v.exclusiveClass == z.exclusiveClass && v.presetTag == presetIdx)
                        v.fastRelease();

        const int64_t id = ++noteCounter;
        int spawned = 0;
        float glideFrom = lastKeyCents;

        for (auto& z : preset.zones)
        {
            if (key < z.keyLo || key > z.keyHi || vel < z.velLo || vel > z.velHi) continue;
            Voice* slot = findSlot();
            if (slot == nullptr) break;
            slot->start (z, *font, key, vel, id, presetIdx, sr, params, glideFrom);
            ++spawned;
        }
        if (spawned > 0)
        {
            lastKeyCents = (float) key * 100.0f;                 // scaleTuning≈100 assumption for glide origin
            enforceNoteCap();
        }
    }

    void noteOff (int key)
    {
        for (auto& v : voices)
            if (v.active && v.key == key && ! v.released())
            {
                if (sustainPedal) pedalHeld[(size_t) (&v - voices)] = true;
                else v.noteOff();
            }
    }

    void sustain (bool down)
    {
        sustainPedal = down;
        if (! down)
            for (size_t i = 0; i < (size_t) kMaxStreams; ++i)
                if (pedalHeld[i]) { pedalHeld[i] = false; if (voices[i].active) voices[i].noteOff(); }
    }

    void pitchBend (float norm) { bendCents = std::clamp (norm, -1.0f, 1.0f) * 200.0f; }

    void allNotesOff() { for (auto& v : voices) if (v.active) v.noteOff(); }
    void panic()       { panicVoices(); crush.reset(); chorus.reset(); delay.reset(); reverb.reset(); }

    int activeVoices() const
    {
        int c = 0; for (auto& v : voices) if (v.active) ++c; return c;
    }

    // ---- audio ----
    void process (float* L, float* R, int n, double bpm)
    {
        std::fill (L, L + n, 0.0f);
        std::fill (R, R + n, 0.0f);
        if (n <= 0) return;

        // synth-layer LFO rate (free Hz or tempo division)
        float lfoHz = params.lfoRateHz;
        if (params.lfoSync)
            lfoHz = (float) (std::max (20.0, bpm) / 60.0 / (double) Delay::divBeats (params.lfoDivIndex));

        int done = 0;
        while (done < n)
        {
            const int len = std::min (Voice::kBlock, n - done);
            const float lfoVal = tickUserLfo (lfoHz, len);
            for (auto& v : voices)
                if (v.active)
                    v.render (L + done, R + done, len, params, bendCents, lfoVal, sr);
            done += len;
        }

        // width (mid/side)
        if (std::abs (params.width - 1.0f) > 0.001f)
        {
            const float w = std::clamp (params.width, 0.0f, 2.0f);
            for (int i = 0; i < n; ++i)
            {
                float m = 0.5f * (L[i] + R[i]), s = 0.5f * (L[i] - R[i]) * w;
                L[i] = m + s; R[i] = m - s;
            }
        }

        // FX chain
        crush.set (params.crushAmt, params.crushRate);
        crush.process (L, R, n);
        chorus.set (params.chorusMix, params.chorusRateHz);
        chorus.process (L, R, n);
        delay.set (params.delayMix, params.delayDiv, params.delayFb, bpm);
        delay.process (L, R, n);
        reverb.set (params.reverbMix, params.reverbSize);
        reverb.process (L, R, n);

        // master
        const float target = dbToGain (std::clamp (params.masterDb, -24.0f, 6.0f));
        float pk = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float g = masterSm.next (target);
            L[i] *= g; R[i] *= g;
            pk = std::max (pk, std::max (std::abs (L[i]), std::abs (R[i])));
        }
        outPeak = pk;
    }

    float lastPeak() const { return outPeak; }

private:
    Voice* findSlot()
    {
        for (auto& v : voices) if (! v.active) return &v;
        // all streams busy: steal the released voice closest to silence, else oldest
        Voice* best = nullptr;
        for (auto& v : voices)
            if (v.released()) if (best == nullptr || v.noteId < best->noteId) best = &v;
        if (best == nullptr)
            for (auto& v : voices)
                if (best == nullptr || v.noteId < best->noteId) best = &v;
        if (best) best->active = false;                    // hard replace (fade owned by new voice)
        return best;
    }

    void enforceNoteCap()
    {
        // count distinct sounding notes; fast-release oldest beyond the cap
        int64_t ids[kMaxStreams]; int nIds = 0;
        for (auto& v : voices)
            if (v.active && ! v.released())
            {
                bool seen = false;
                for (int i = 0; i < nIds; ++i) if (ids[i] == v.noteId) { seen = true; break; }
                if (! seen) ids[nIds++] = v.noteId;
            }
        while (nIds > kMaxNotes)
        {
            int64_t oldest = INT64_MAX;
            for (int i = 0; i < nIds; ++i) oldest = std::min (oldest, ids[i]);
            for (auto& v : voices) if (v.active && v.noteId == oldest) v.fastRelease();
            int w = 0;
            for (int i = 0; i < nIds; ++i) if (ids[i] != oldest) ids[w++] = ids[i];
            nIds = w;
        }
    }

    void panicVoices()
    {
        for (auto& v : voices) v.active = false;
        std::fill (std::begin (pedalHeld), std::end (pedalHeld), false);
        sustainPedal = false;
    }

    const SoundFont* font = nullptr;
    int presetIdx = 0;
    SynthParams params;

    Voice voices[kMaxStreams];
    bool  pedalHeld[kMaxStreams] {};
    bool  sustainPedal = false;
    int64_t noteCounter = 0;
    float bendCents = 0.0f, lastKeyCents = -1.0e9f;

    // global triangle LFO with persistent phase (advances per control block)
    float tickUserLfo (float hz, int samples)
    {
        userLfoPhase += hz * (float) samples / (float) sr;
        userLfoPhase -= std::floor (userLfoPhase);
        const float p = userLfoPhase;
        if (p < 0.25f) return p * 4.0f;
        if (p < 0.75f) return 2.0f - p * 4.0f;
        return p * 4.0f - 4.0f;
    }
    float userLfoPhase = 0.0f;

    Crush crush; Chorus chorus; Delay delay; Reverb reverb;
    Smoother masterSm;
    std::vector<float> scratchL, scratchR;
    double sr = 48000.0;
    float outPeak = 0.0f;
};

} // namespace celadon
