#pragma once
// CeladonSF2.h — SoundFont 2 parser and data model. JUCE-free, tested in the
// sandbox against the real cart. Implements the SF2 2.04 spec's hydra
// (phdr/pbag/pmod/pgen/inst/ibag/imod/igen/shdr), zone resolution, and the
// absolute-(instrument) / relative-(preset) generator merge. Modulators
// (pmod/imod) are intentionally not interpreted in v1 — the default
// velocity->attenuation modulator is applied in the engine.
//
// References: SoundFont Technical Specification 2.04 (synthfont.com/sfspec24.pdf),
// cross-checked against FluidSynth's fluid_gen.c. See docs/RESEARCH.md.

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

namespace celadon
{

// ---------- SF2 unit conversions (spec section 2 / 9.3) ----------
inline double tc2sec  (double tc)    { return std::pow (2.0, tc / 1200.0); }          // timecents -> seconds
inline double cents2hz (double c)    { return 8.176 * std::pow (2.0, c / 1200.0); }   // absolute cents -> Hz
inline double cb2gain (double cb)    { return std::pow (10.0, -cb / 200.0); }         // centibels attenuation -> gain
inline double centsRatio (double c)  { return std::pow (2.0, c / 1200.0); }

// ---------- generator enumerators we honor ----------
enum Gen : int
{
    GenStartAddrsOffset = 0,  GenEndAddrsOffset = 1,
    GenStartloopAddrsOffset = 2, GenEndloopAddrsOffset = 3,
    GenStartAddrsCoarseOffset = 4,
    GenModLfoToPitch = 5,     GenVibLfoToPitch = 6,     GenModEnvToPitch = 7,
    GenInitialFilterFc = 8,   GenInitialFilterQ = 9,
    GenModLfoToFilterFc = 10, GenModEnvToFilterFc = 11,
    GenEndAddrsCoarseOffset = 12,
    GenModLfoToVolume = 13,
    GenChorusSend = 15,       GenReverbSend = 16,       GenPan = 17,
    GenDelayModLFO = 21,      GenFreqModLFO = 22,
    GenDelayVibLFO = 23,      GenFreqVibLFO = 24,
    GenDelayModEnv = 25,      GenAttackModEnv = 26,     GenHoldModEnv = 27,
    GenDecayModEnv = 28,      GenSustainModEnv = 29,    GenReleaseModEnv = 30,
    GenKeynumToModEnvHold = 31, GenKeynumToModEnvDecay = 32,
    GenDelayVolEnv = 33,      GenAttackVolEnv = 34,     GenHoldVolEnv = 35,
    GenDecayVolEnv = 36,      GenSustainVolEnv = 37,    GenReleaseVolEnv = 38,
    GenKeynumToVolEnvHold = 39, GenKeynumToVolEnvDecay = 40,
    GenInstrument = 41,
    GenKeyRange = 43,         GenVelRange = 44,
    GenStartloopAddrsCoarseOffset = 45,
    GenKeynum = 46,           GenVelocity = 47,
    GenInitialAttenuation = 48,
    GenEndloopAddrsCoarseOffset = 50,
    GenCoarseTune = 51,       GenFineTune = 52,
    GenSampleID = 53,         GenSampleModes = 54,
    GenScaleTuning = 56,      GenExclusiveClass = 57,   GenOverridingRootKey = 58,
    GenCount = 61
};

// ---------- data model ----------
struct SampleHeader
{
    std::string name;
    uint32_t start = 0, end = 0, loopStart = 0, loopEnd = 0; // absolute points into the pool
    uint32_t rate = 44100;
    uint8_t  origPitch = 60;
    int8_t   pitchCorr = 0;
    uint16_t link = 0, type = 1;
};

// One fully-resolved playable zone: instrument absolute values + preset
// relative offsets already merged, everything converted to engineering units.
struct Zone
{
    int keyLo = 0, keyHi = 127, velLo = 0, velHi = 127;
    int sampleIndex = -1;

    // volume envelope (seconds / dB of attenuation below peak)
    float delay = 0.001f, attack = 0.001f, hold = 0.001f,
          decay = 0.001f, sustainDb = 0.0f,  release = 0.001f;
    float holdKeyScale = 0.0f, decayKeyScale = 0.0f;      // timecents per key (gens 39/40)

    // modulation envelope (seconds / level 0..1), file-defined routing in cents
    float mDelay = 0.001f, mAttack = 0.001f, mHold = 0.001f,
          mDecay = 0.001f, mSustain = 1.0f,  mRelease = 0.001f;
    float mHoldKeyScale = 0.0f, mDecayKeyScale = 0.0f;
    float modEnvToPitch = 0.0f, modEnvToFilterFc = 0.0f;  // cents at full scale

    // lowpass filter
    float filterFcCents = 13500.0f;                        // absolute cents (~20 kHz = open)
    float filterQdB = 0.0f;                                // resonance peak height

    // the file's two LFOs (triangle), routing amounts in cents / dB
    float modLfoDelay = 0.001f, modLfoFreq = 8.176f;
    float modLfoToPitch = 0.0f, modLfoToFilterFc = 0.0f, modLfoToVolumeDb = 0.0f;
    float vibLfoDelay = 0.001f, vibLfoFreq = 8.176f, vibLfoToPitch = 0.0f;

    // pitch
    int rootKey = 60;
    int coarseTune = 0, fineTune = 0, scaleTuning = 100;
    int pitchCorr = 0;

    int   sampleModes = 0;      // 0 none, 1 continuous, 3 loop-until-release
    float pan = 0.0f;           // -1 .. +1
    float attenuationDb = 0.0f; // gen 48, spec-literal (no EMU 0.4x)
    int   exclusiveClass = 0;
    int   keynumOverride = -1, velocityOverride = -1;
    int32_t startOff = 0, endOff = 0, loopStartOff = 0, loopEndOff = 0;
    float chorusSend = 0.0f, reverbSend = 0.0f;            // 0..1
};

struct Preset
{
    std::string name;
    int bank = 0, program = 0;
    std::vector<Zone> zones;
};

// ---------- raw hydra records ----------
namespace hydra
{
    struct Bag  { uint16_t genNdx, modNdx; };
    struct GenRec { uint16_t oper; uint16_t amount; };   // amount reinterpreted per-oper
    struct PresetHdr { char name[21]; uint16_t preset, bank, bagNdx; };
    struct InstHdr   { char name[21]; uint16_t bagNdx; };
}

class SoundFont
{
public:
    std::string name, engineer, tool, comment;
    std::vector<float>        pool;      // whole smpl chunk as float, incl. guard zeros
    std::vector<SampleHeader> samples;
    std::vector<Preset>       presets;   // sorted by (bank, program)

    bool loadFile (const char* path, std::string& err)
    {
        std::FILE* f = std::fopen (path, "rb");
        if (! f) { err = "could not open file"; return false; }
        std::fseek (f, 0, SEEK_END);
        long sz = std::ftell (f);
        std::fseek (f, 0, SEEK_SET);
        if (sz <= 0) { std::fclose (f); err = "empty file"; return false; }
        std::vector<uint8_t> data ((size_t) sz);
        size_t got = std::fread (data.data(), 1, (size_t) sz, f);
        std::fclose (f);
        if (got != (size_t) sz) { err = "short read"; return false; }
        return load (data.data(), data.size(), err);
    }

    bool load (const uint8_t* d, size_t n, std::string& err)
    {
        pool.clear(); samples.clear(); presets.clear();
        name.clear(); engineer.clear(); tool.clear(); comment.clear();

        auto fail = [&err] (const char* m) { err = m; return false; };
        if (n < 12 || std::memcmp (d, "RIFF", 4) != 0 || std::memcmp (d + 8, "sfbk", 4) != 0)
            return fail ("not a SoundFont (RIFF sfbk) file");

        uint32_t riffSize = rd32 (d + 4);
        size_t end = std::min (n, (size_t) riffSize + 8);

        // raw hydra staging
        std::vector<hydra::PresetHdr> phdr;
        std::vector<hydra::InstHdr>   inst;
        std::vector<hydra::Bag>       pbag, ibag;
        std::vector<hydra::GenRec>    pgen, igen;

        size_t pos = 12;
        while (pos + 8 <= end)
        {
            const uint8_t* cid = d + pos;
            uint32_t csz = rd32 (d + pos + 4);
            size_t body = pos + 8;
            if (body + csz > end) return fail ("truncated chunk");

            if (std::memcmp (cid, "LIST", 4) == 0 && csz >= 4)
            {
                const uint8_t* lt = d + body;
                size_t sub = body + 4, subEnd = body + csz;
                while (sub + 8 <= subEnd)
                {
                    const uint8_t* sid = d + sub;
                    uint32_t ssz = rd32 (d + sub + 4);
                    const uint8_t* sb = d + sub + 8;
                    if (sub + 8 + ssz > subEnd) return fail ("truncated sub-chunk");

                    if (std::memcmp (lt, "INFO", 4) == 0)
                    {
                        if      (! std::memcmp (sid, "INAM", 4)) name     = zstr (sb, ssz);
                        else if (! std::memcmp (sid, "IENG", 4)) engineer = zstr (sb, ssz);
                        else if (! std::memcmp (sid, "ISFT", 4)) tool     = zstr (sb, ssz);
                        else if (! std::memcmp (sid, "ICMT", 4)) comment  = zstr (sb, ssz);
                    }
                    else if (std::memcmp (lt, "sdta", 4) == 0)
                    {
                        if (! std::memcmp (sid, "smpl", 4))
                        {
                            size_t pts = ssz / 2;
                            pool.resize (pts);
                            const float k = 1.0f / 32768.0f;
                            for (size_t i = 0; i < pts; ++i)
                                pool[i] = (float) (int16_t) rd16 (sb + i * 2) * k;
                        }
                        // sm24 ignored (ifil < 2.04 in practice; 16-bit is plenty)
                    }
                    else if (std::memcmp (lt, "pdta", 4) == 0)
                    {
                        if      (! std::memcmp (sid, "phdr", 4)) readPhdr (sb, ssz, phdr);
                        else if (! std::memcmp (sid, "inst", 4)) readInst (sb, ssz, inst);
                        else if (! std::memcmp (sid, "pbag", 4)) readBags (sb, ssz, pbag);
                        else if (! std::memcmp (sid, "ibag", 4)) readBags (sb, ssz, ibag);
                        else if (! std::memcmp (sid, "pgen", 4)) readGens (sb, ssz, pgen);
                        else if (! std::memcmp (sid, "igen", 4)) readGens (sb, ssz, igen);
                        else if (! std::memcmp (sid, "shdr", 4)) readShdr (sb, ssz);
                    }
                    sub += 8 + ssz + (ssz & 1);
                }
            }
            pos = body + csz + (csz & 1);
        }

        if (pool.empty())                       return fail ("no sample data (smpl)");
        if (phdr.size() < 2 || inst.size() < 2) return fail ("no presets/instruments");
        if (pbag.size() < 2 || ibag.size() < 2) return fail ("missing bag chunks");
        if (samples.size() < 1)                 return fail ("no sample headers");

        // index sanity (terminal records delimit the arrays)
        for (auto& h : phdr) if (h.bagNdx >= pbag.size()) return fail ("phdr bag index out of range");
        for (auto& h : inst) if (h.bagNdx >= ibag.size()) return fail ("inst bag index out of range");
        for (auto& b : pbag) if (b.genNdx >  pgen.size()) return fail ("pbag gen index out of range");
        for (auto& b : ibag) if (b.genNdx >  igen.size()) return fail ("ibag gen index out of range");

        buildPresets (phdr, pbag, pgen, inst, ibag, igen);
        if (presets.empty()) return fail ("no playable presets");
        return true;
    }

    const Preset* findPreset (int bank, int program) const
    {
        for (auto& p : presets)
            if (p.bank == bank && p.program == program) return &p;
        return nullptr;
    }

private:
    static uint16_t rd16 (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
    static uint32_t rd32 (const uint8_t* p) { return (uint32_t) (p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24)); }
    static std::string zstr (const uint8_t* p, size_t n)
    {
        size_t len = 0; while (len < n && p[len] != 0) ++len;
        return std::string ((const char*) p, len);
    }

    static void readBags (const uint8_t* p, size_t n, std::vector<hydra::Bag>& out)
    {
        for (size_t i = 0; i + 4 <= n; i += 4) out.push_back ({ rd16 (p + i), rd16 (p + i + 2) });
    }
    static void readGens (const uint8_t* p, size_t n, std::vector<hydra::GenRec>& out)
    {
        for (size_t i = 0; i + 4 <= n; i += 4) out.push_back ({ rd16 (p + i), rd16 (p + i + 2) });
    }
    static void readPhdr (const uint8_t* p, size_t n, std::vector<hydra::PresetHdr>& out)
    {
        for (size_t i = 0; i + 38 <= n; i += 38)
        {
            hydra::PresetHdr h {};
            std::memcpy (h.name, p + i, 20); h.name[20] = 0;
            h.preset = rd16 (p + i + 20); h.bank = rd16 (p + i + 22); h.bagNdx = rd16 (p + i + 24);
            out.push_back (h);
        }
    }
    static void readInst (const uint8_t* p, size_t n, std::vector<hydra::InstHdr>& out)
    {
        for (size_t i = 0; i + 22 <= n; i += 22)
        {
            hydra::InstHdr h {};
            std::memcpy (h.name, p + i, 20); h.name[20] = 0;
            h.bagNdx = rd16 (p + i + 20);
            out.push_back (h);
        }
    }
    void readShdr (const uint8_t* p, size_t n)
    {
        size_t count = n / 46;
        if (count > 0) --count;                    // drop terminal EOS record
        for (size_t i = 0; i < count; ++i)
        {
            const uint8_t* r = p + i * 46;
            SampleHeader s;
            s.name      = zstr (r, 20);
            s.start     = rd32 (r + 20); s.end     = rd32 (r + 24);
            s.loopStart = rd32 (r + 28); s.loopEnd = rd32 (r + 32);
            s.rate      = rd32 (r + 36);
            s.origPitch = r[40]; s.pitchCorr = (int8_t) r[41];
            s.link      = rd16 (r + 42); s.type = rd16 (r + 44);
            samples.push_back (s);
        }
    }

    // ---- generator merge ------------------------------------------------
    struct GenSet
    {
        int     val[GenCount] {};
        bool    set[GenCount] {};
        void put (int g, int v) { if (g >= 0 && g < GenCount) { val[g] = v; set[g] = true; } }
        int  get (int g, int dflt) const { return set[g] ? val[g] : dflt; }
    };

    // spec defaults (table 8.1.3, confirmed against fluid_gen.c)
    static void defaults (GenSet& g)
    {
        auto D = [&g] (int id, int v) { g.val[id] = v; g.set[id] = false; };
        D (GenInitialFilterFc, 13500); D (GenInitialFilterQ, 0);
        D (GenDelayModLFO, -12000); D (GenFreqModLFO, 0);
        D (GenDelayVibLFO, -12000); D (GenFreqVibLFO, 0);
        D (GenDelayModEnv, -12000); D (GenAttackModEnv, -12000);
        D (GenHoldModEnv, -12000);  D (GenDecayModEnv, -12000);
        D (GenSustainModEnv, 0);    D (GenReleaseModEnv, -12000);
        D (GenDelayVolEnv, -12000); D (GenAttackVolEnv, -12000);
        D (GenHoldVolEnv, -12000);  D (GenDecayVolEnv, -12000);
        D (GenSustainVolEnv, 0);    D (GenReleaseVolEnv, -12000);
        D (GenScaleTuning, 100);    D (GenOverridingRootKey, -1);
        D (GenKeynum, -1);          D (GenVelocity, -1);
    }

    // generators only legal at instrument level — never added from preset zones
    static bool instrumentOnly (int g)
    {
        switch (g)
        {
            case GenStartAddrsOffset: case GenEndAddrsOffset:
            case GenStartloopAddrsOffset: case GenEndloopAddrsOffset:
            case GenStartAddrsCoarseOffset: case GenEndAddrsCoarseOffset:
            case GenStartloopAddrsCoarseOffset: case GenEndloopAddrsCoarseOffset:
            case GenKeynum: case GenVelocity: case GenSampleModes:
            case GenExclusiveClass: case GenOverridingRootKey:
                return true;
            default: return false;
        }
    }

    void buildPresets (const std::vector<hydra::PresetHdr>& phdr,
                       const std::vector<hydra::Bag>& pbag,
                       const std::vector<hydra::GenRec>& pgen,
                       const std::vector<hydra::InstHdr>& inst,
                       const std::vector<hydra::Bag>& ibag,
                       const std::vector<hydra::GenRec>& igen)
    {
        const size_t nPresets = phdr.size() - 1;   // terminal EOP
        const size_t nInsts   = inst.size() - 1;   // terminal EOI

        for (size_t pi = 0; pi < nPresets; ++pi)
        {
            Preset preset;
            preset.name    = phdr[pi].name;
            preset.bank    = phdr[pi].bank;
            preset.program = phdr[pi].preset;

            const int pzFirst = phdr[pi].bagNdx, pzLast = phdr[pi + 1].bagNdx;

            GenSet presetGlobal; bool haveGlobal = false;

            for (int pz = pzFirst; pz < pzLast && pz + 1 < (int) pbag.size(); ++pz)
            {
                GenSet pzs;
                int instIdx = -1;
                for (int gi = pbag[pz].genNdx; gi < pbag[pz + 1].genNdx && gi < (int) pgen.size(); ++gi)
                {
                    const auto& g = pgen[gi];
                    if (g.oper == GenInstrument) instIdx = (int) g.amount;
                    else if (g.oper < GenCount)  pzs.put (g.oper, (int16_t) g.amount);
                }
                if (instIdx < 0)
                {
                    if (pz == pzFirst) { presetGlobal = pzs; haveGlobal = true; }
                    continue;                                        // global (or stray) zone
                }
                if (instIdx >= (int) nInsts) continue;

                // effective preset-level gens: global overridden by local
                GenSet pEff = haveGlobal ? presetGlobal : GenSet();
                for (int g = 0; g < GenCount; ++g)
                    if (pzs.set[g]) pEff.put (g, pzs.val[g]);

                emitInstrumentZones (preset, pEff, instIdx, inst, ibag, igen);
            }

            std::sort (preset.zones.begin(), preset.zones.end(),
                       [] (const Zone& a, const Zone& b) { return a.keyLo < b.keyLo; });
            presets.push_back (std::move (preset));
        }

        std::sort (presets.begin(), presets.end(), [] (const Preset& a, const Preset& b)
                   { return a.bank != b.bank ? a.bank < b.bank : a.program < b.program; });
    }

    void emitInstrumentZones (Preset& preset, const GenSet& pEff, int instIdx,
                              const std::vector<hydra::InstHdr>& inst,
                              const std::vector<hydra::Bag>& ibag,
                              const std::vector<hydra::GenRec>& igen)
    {
        const int izFirst = inst[instIdx].bagNdx, izLast = inst[instIdx + 1].bagNdx;

        GenSet instGlobal; bool haveIGlobal = false;

        for (int iz = izFirst; iz < izLast && iz + 1 < (int) ibag.size(); ++iz)
        {
            GenSet izs;
            int sampleIdx = -1;
            for (int gi = ibag[iz].genNdx; gi < ibag[iz + 1].genNdx && gi < (int) igen.size(); ++gi)
            {
                const auto& g = igen[gi];
                if (g.oper == GenSampleID) sampleIdx = (int) g.amount;
                else if (g.oper < GenCount) izs.put (g.oper, decodeAmount (g.oper, g.amount));
            }
            if (sampleIdx < 0)
            {
                if (iz == izFirst) { instGlobal = izs; haveIGlobal = true; }
                continue;
            }
            if (sampleIdx >= (int) samples.size()) continue;
            if (samples[(size_t) sampleIdx].type & 0x8000) continue;      // ROM sample

            // final generator values: defaults <- inst global <- inst local, + preset offsets
            GenSet fin; defaults (fin);
            if (haveIGlobal)
                for (int g = 0; g < GenCount; ++g) if (instGlobal.set[g]) fin.put (g, instGlobal.val[g]);
            for (int g = 0; g < GenCount; ++g)     if (izs.set[g])        fin.put (g, izs.val[g]);
            for (int g = 0; g < GenCount; ++g)
                if (pEff.set[g] && ! instrumentOnly (g) && g != GenKeyRange && g != GenVelRange)
                    fin.val[g] += pEff.val[g];                            // relative add

            // ranges intersect (preset range restricts instrument range)
            auto range = [] (const GenSet& s, int gen, int& lo, int& hi)
            {
                if (! s.set[gen]) return;
                int v = s.val[gen];
                lo = v & 0xFF; hi = (v >> 8) & 0xFF;
                if (lo > hi) std::swap (lo, hi);
            };
            int kLo = 0, kHi = 127, vLo = 0, vHi = 127;
            range (fin, GenKeyRange, kLo, kHi); range (fin, GenVelRange, vLo, vHi);
            int pkLo = 0, pkHi = 127, pvLo = 0, pvHi = 127;
            range (pEff, GenKeyRange, pkLo, pkHi); range (pEff, GenVelRange, pvLo, pvHi);
            kLo = std::max (kLo, pkLo); kHi = std::min (kHi, pkHi);
            vLo = std::max (vLo, pvLo); vHi = std::min (vHi, pvHi);
            if (kLo > kHi || vLo > vHi) return;                           // empty intersection

            preset.zones.push_back (makeZone (fin, sampleIdx, kLo, kHi, vLo, vHi));
        }
    }

    // range generators arrive as {lo, hi} byte pairs — keep raw; others signed
    static int decodeAmount (int oper, uint16_t raw)
    {
        if (oper == GenKeyRange || oper == GenVelRange) return (int) raw;
        return (int) (int16_t) raw;
    }

    Zone makeZone (const GenSet& g, int sampleIdx, int kLo, int kHi, int vLo, int vHi) const
    {
        auto clampi = [] (int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
        auto tc = [&] (int gen, int lo, int hi)
        { return (float) tc2sec ((double) clampi (g.get (gen, -12000), lo, hi)); };

        const SampleHeader& sh = samples[(size_t) sampleIdx];
        Zone z;
        z.keyLo = kLo; z.keyHi = kHi; z.velLo = vLo; z.velHi = vHi;
        z.sampleIndex = sampleIdx;

        z.delay   = tc (GenDelayVolEnv,  -12000, 5000);
        z.attack  = tc (GenAttackVolEnv, -12000, 8000);
        z.hold    = tc (GenHoldVolEnv,   -12000, 5000);
        z.decay   = tc (GenDecayVolEnv,  -12000, 8000);
        z.release = tc (GenReleaseVolEnv,-12000, 8000);
        z.sustainDb = (float) clampi (g.get (GenSustainVolEnv, 0), 0, 1440) * 0.1f;
        z.holdKeyScale  = (float) clampi (g.get (GenKeynumToVolEnvHold, 0),  -1200, 1200);
        z.decayKeyScale = (float) clampi (g.get (GenKeynumToVolEnvDecay, 0), -1200, 1200);

        z.mDelay   = tc (GenDelayModEnv,  -12000, 5000);
        z.mAttack  = tc (GenAttackModEnv, -12000, 8000);
        z.mHold    = tc (GenHoldModEnv,   -12000, 5000);
        z.mDecay   = tc (GenDecayModEnv,  -12000, 8000);
        z.mRelease = tc (GenReleaseModEnv,-12000, 8000);
        z.mSustain = 1.0f - (float) clampi (g.get (GenSustainModEnv, 0), 0, 1000) / 1000.0f;
        z.mHoldKeyScale  = (float) clampi (g.get (GenKeynumToModEnvHold, 0),  -1200, 1200);
        z.mDecayKeyScale = (float) clampi (g.get (GenKeynumToModEnvDecay, 0), -1200, 1200);
        z.modEnvToPitch    = (float) clampi (g.get (GenModEnvToPitch, 0),    -12000, 12000);
        z.modEnvToFilterFc = (float) clampi (g.get (GenModEnvToFilterFc, 0), -12000, 12000);

        z.filterFcCents = (float) clampi (g.get (GenInitialFilterFc, 13500), 1500, 13500);
        z.filterQdB     = (float) clampi (g.get (GenInitialFilterQ, 0), 0, 960) * 0.1f;

        z.modLfoDelay = tc (GenDelayModLFO, -12000, 5000);
        z.modLfoFreq  = (float) cents2hz ((double) clampi (g.get (GenFreqModLFO, 0), -16000, 4500));
        z.modLfoToPitch    = (float) clampi (g.get (GenModLfoToPitch, 0),    -12000, 12000);
        z.modLfoToFilterFc = (float) clampi (g.get (GenModLfoToFilterFc, 0), -12000, 12000);
        z.modLfoToVolumeDb = (float) clampi (g.get (GenModLfoToVolume, 0), -960, 960) * 0.1f;
        z.vibLfoDelay = tc (GenDelayVibLFO, -12000, 5000);
        z.vibLfoFreq  = (float) cents2hz ((double) clampi (g.get (GenFreqVibLFO, 0), -16000, 4500));
        z.vibLfoToPitch = (float) clampi (g.get (GenVibLfoToPitch, 0), -12000, 12000);

        int rk = g.get (GenOverridingRootKey, -1);
        z.rootKey = (rk >= 0 && rk <= 127) ? rk : (sh.origPitch <= 127 ? sh.origPitch : 60);
        if (sh.origPitch == 255) z.rootKey = 60;
        z.coarseTune  = clampi (g.get (GenCoarseTune, 0), -120, 120);
        z.fineTune    = clampi (g.get (GenFineTune, 0), -99, 99);
        z.scaleTuning = clampi (g.get (GenScaleTuning, 100), 0, 1200);
        z.pitchCorr   = sh.pitchCorr;

        int sm = g.get (GenSampleModes, 0) & 3;
        z.sampleModes = (sm == 2) ? 0 : sm;                    // mode 2 reserved -> no loop
        z.pan = (float) clampi (g.get (GenPan, 0), -500, 500) / 500.0f;
        z.attenuationDb = (float) clampi (g.get (GenInitialAttenuation, 0), 0, 1440) * 0.1f;
        z.exclusiveClass = clampi (g.get (GenExclusiveClass, 0), 0, 127);
        z.keynumOverride   = g.get (GenKeynum, -1);
        z.velocityOverride = g.get (GenVelocity, -1);
        z.chorusSend = (float) clampi (g.get (GenChorusSend, 0), 0, 1000) / 1000.0f;
        z.reverbSend = (float) clampi (g.get (GenReverbSend, 0), 0, 1000) / 1000.0f;

        z.startOff     = g.get (GenStartAddrsOffset, 0)     + 32768 * g.get (GenStartAddrsCoarseOffset, 0);
        z.endOff       = g.get (GenEndAddrsOffset, 0)       + 32768 * g.get (GenEndAddrsCoarseOffset, 0);
        z.loopStartOff = g.get (GenStartloopAddrsOffset, 0) + 32768 * g.get (GenStartloopAddrsCoarseOffset, 0);
        z.loopEndOff   = g.get (GenEndloopAddrsOffset, 0)   + 32768 * g.get (GenEndloopAddrsCoarseOffset, 0);
        return z;
    }
};

} // namespace celadon
