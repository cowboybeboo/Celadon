#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (std::sqrt (lo * hi));
        return r;
    }

    auto fparam (const char* id, const char* name, juce::NormalisableRange<float> range,
                 float def, const char* label)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, range, def,
            juce::AudioParameterFloatAttributes().withLabel (label));
    }
    auto pct (const char* id, const char* name, float def)
    {
        return fparam (id, name, { 0.0f, 100.0f, 0.1f }, def, "%");
    }
}

CeladonProcessor::CeladonProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout CeladonProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    // ENVELOPE — relative to the cart's own values (centre = as the maker intended)
    p.push_back (fparam ("attack",  "Attack",  logRange (0.25f, 4.0f), 1.0f, "x"));
    p.push_back (fparam ("decay",   "Decay",   logRange (0.25f, 4.0f), 1.0f, "x"));
    p.push_back (fparam ("sustain", "Sustain", { -24.0f, 24.0f, 0.1f }, 0.0f, "dB"));
    p.push_back (fparam ("release", "Release", logRange (0.25f, 4.0f), 1.0f, "x"));

    // MOD ENV (ALT set) — the synth-layer filter envelope, absolute
    p.push_back (fparam ("menva", "Mod Env Attack",  logRange (1.0f, 2000.0f), 5.0f,   "ms"));
    p.push_back (fparam ("menvd", "Mod Env Decay",   logRange (1.0f, 4000.0f), 300.0f, "ms"));
    p.push_back (pct    ("menvs", "Mod Env Sustain", 0.0f));
    p.push_back (fparam ("menvr", "Mod Env Release", logRange (1.0f, 4000.0f), 200.0f, "ms"));

    // FILTER — offsets around the cart's values
    p.push_back (fparam ("cutoff",   "Cutoff",    { -4800.0f, 4800.0f, 1.0f }, 0.0f, "ct"));
    p.push_back (fparam ("reso",     "Resonance", { 0.0f, 24.0f, 0.1f }, 0.0f, "dB"));
    p.push_back (fparam ("envamt",   "Env Amount",{ -4800.0f, 4800.0f, 1.0f }, 0.0f, "ct"));
    p.push_back (fparam ("keytrack", "Keytrack",  { 0.0f, 100.0f, 1.0f }, 0.0f, "ct/key"));

    // LFO
    p.push_back (fparam ("lforate",  "LFO Rate",  logRange (0.02f, 20.0f), 2.0f, "Hz"));
    p.push_back (pct    ("lfodepth", "LFO Depth", 0.0f));
    p.push_back (fparam ("lfodelay", "LFO Delay", { 0.0f, 2000.0f, 1.0f }, 0.0f, "ms"));

    // SOUND
    p.push_back (fparam ("glide",   "Glide", { 0.0f, 500.0f, 1.0f }, 0.0f, "ms"));
    p.push_back (fparam ("tune",    "Tune",  { -12.0f, 12.0f, 1.0f }, 0.0f, "st"));
    p.push_back (pct    ("velsens", "Vel Sens", 100.0f));
    p.push_back (pct    ("width",   "Width",    100.0f));

    // FX
    p.push_back (pct    ("crush",      "Crush", 0.0f));
    // Crush Rate (downsampling) must default to 0: the CRUSH module only
    // bypasses when BOTH amount and rate are ~0, so any nonzero default here
    // leaves the downsampler running at startup (artifacts on all output).
    p.push_back (pct    ("crushrate",  "Crush Rate", 0.0f));
    p.push_back (pct    ("chorus",     "Chorus", 0.0f));
    p.push_back (fparam ("chorusrate", "Chorus Rate", logRange (0.05f, 5.0f), 0.8f, "Hz"));
    p.push_back (pct    ("delaymix",   "Delay", 0.0f));
    p.push_back (pct    ("delayfb",    "Delay Feedback", 45.0f));
    p.push_back (pct    ("reverbmix",  "Reverb", 0.0f));
    p.push_back (pct    ("reverbsize", "Reverb Size", 35.0f));

    p.push_back (fparam ("volume", "Volume", { -24.0f, 6.0f, 0.1f }, 0.0f, "dB"));

    // choices / switches
    p.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "lfotarget", 1 }, "LFO Target",
        juce::StringArray { "Pitch", "Filter", "Amp" }, 1));
    p.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "lfosync", 1 }, "LFO Sync", false));

    juce::StringArray divs;
    for (int i = 0; i < celadon::Delay::kNumDivs; ++i) divs.add (celadon::Delay::divName (i));
    p.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "lfodiv", 1 }, "LFO Division", divs, 4));
    p.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "delaydiv", 1 }, "Delay Division", divs, 4));

    return { p.begin(), p.end() };
}

void CeladonProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    monoScratch.setSize (1, samplesPerBlock);
}

bool CeladonProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

celadon::SynthParams CeladonProcessor::gatherParams()
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    celadon::SynthParams p;
    p.attackMul    = get ("attack");
    p.decayMul     = get ("decay");
    p.sustainOffDb = get ("sustain");
    p.releaseMul   = get ("release");
    p.mA = get ("menva") * 0.001f;
    p.mD = get ("menvd") * 0.001f;
    p.mS = get ("menvs") * 0.01f;
    p.mR = get ("menvr") * 0.001f;
    p.cutoffCents = get ("cutoff");
    p.resoDb      = get ("reso");
    p.envAmtCents = get ("envamt");
    p.keytrack    = get ("keytrack");
    p.lfoTarget   = (int) get ("lfotarget");
    p.lfoSync     = get ("lfosync") > 0.5f;
    p.lfoRateHz   = get ("lforate");
    p.lfoDivIndex = (int) get ("lfodiv");
    p.lfoDepth    = get ("lfodepth") * 0.01f;
    p.lfoDelayS   = get ("lfodelay") * 0.001f;
    p.glideS      = get ("glide") * 0.001f;
    p.tuneSemis   = get ("tune");
    p.velSens     = get ("velsens") * 0.01f;
    p.width       = get ("width") * 0.01f;
    p.crushAmt    = get ("crush") * 0.01f;
    p.crushRate   = get ("crushrate") * 0.01f;
    p.chorusMix   = get ("chorus") * 0.01f;
    p.chorusRateHz= get ("chorusrate");
    p.delayMix    = get ("delaymix") * 0.01f;
    p.delayDiv    = (int) get ("delaydiv");
    p.delayFb     = get ("delayfb") * 0.01f;
    p.reverbMix   = get ("reverbmix") * 0.01f;
    p.reverbSize  = get ("reverbsize") * 0.01f;
    p.masterDb    = get ("volume");
    return p;
}

void CeladonProcessor::handleMidiEvent (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
    {
        engine.noteOn (m.getNoteNumber(), m.getVelocity());
        midiActivity.store (6);
    }
    else if (m.isNoteOff())      engine.noteOff (m.getNoteNumber());
    else if (m.isPitchWheel())   engine.pitchBend ((float) (m.getPitchWheelValue() - 8192) / 8192.0f);
    else if (m.isProgramChange())
    {
        pendingPreset.store (m.getProgramChangeNumber());
        midiActivity.store (6);
    }
    else if (m.isController())
    {
        const int cc = m.getControllerNumber(), v = m.getControllerValue();
        if (cc == 64) { engine.sustain (v >= 64); return; }
        if (cc == 120 || cc == 123) { engine.allNotesOff(); return; }

        static const char* ccMap[] = {
            "attack", "decay", "sustain", "release",          // 20-23
            "cutoff", "reso", "envamt", "keytrack",           // 24-27
            "lforate", "lfodepth", "lfodelay",                // 28-30
            "glide", "tune", "velsens", "width",              // 31-34
            "crush", "crushrate", "chorus", "chorusrate",     // 35-38
            "delaymix", "reverbmix", "reverbsize", "volume",  // 39-42
            "menva", "menvd", "menvs", "menvr", "delayfb"     // 43-47
        };
        constexpr int nCC = (int) (sizeof (ccMap) / sizeof (ccMap[0]));
        if (cc >= 20 && cc < 20 + nCC)
        {
            if (auto* par = apvts.getParameter (ccMap[cc - 20]))
                par->setValueNotifyingHost ((float) v / 127.0f);
        }
        else if (cc >= 102 && cc <= 105)
        {
            static const char* swMap[] = { "lfotarget", "lfosync", "lfodiv", "delaydiv" };
            if (auto* par = apvts.getParameter (swMap[cc - 102]))
                par->setValueNotifyingHost ((float) v / 127.0f);
        }
    }
}

void CeladonProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    if (panicFlag.exchange (false)) engine.panic();

    const int pending = pendingPreset.exchange (-1);
    if (pending >= 0 && pending != engine.presetIndex())
    {
        engine.setPresetIndex (pending);
        fontVersion.fetch_add (1);
    }

    engine.setParams (gatherParams());

    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) bpm = *b;

    const int total = buffer.getNumSamples();
    float* L = buffer.getWritePointer (0);
    const bool monoBus = buffer.getNumChannels() < 2;
    if (monoBus) monoScratch.setSize (1, total, false, false, true);
    float* R = monoBus ? monoScratch.getWritePointer (0) : buffer.getWritePointer (1);

    // sample-accurate-ish: split the block at MIDI events
    int cursor = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, total, (int) meta.samplePosition);
        if (at > cursor)
        {
            engine.process (L + cursor, R + cursor, at - cursor, bpm);
            cursor = at;
        }
        handleMidiEvent (meta.getMessage());
    }
    if (cursor < total)
        engine.process (L + cursor, R + cursor, total - cursor, bpm);

    if (monoBus)
        for (int i = 0; i < total; ++i) L[i] = 0.5f * (L[i] + R[i]);

    // UI feed
    int w = scopeWrite.load (std::memory_order_relaxed);
    const int stride = 4;                                  // ~12 kHz scope feed
    for (int i = 0; i < total; i += stride)
    {
        scope[w & (kScopeSize - 1)] = 0.5f * (L[i] + R[i]);
        ++w;
    }
    scopeWrite.store (w, std::memory_order_release);
    uiVoices.store (engine.activeVoices(), std::memory_order_relaxed);
    uiPeak.store (engine.lastPeak(), std::memory_order_relaxed);
}

// ---------------- cart management ----------------
bool CeladonProcessor::loadSoundFontFile (const juce::File& file, juce::String& error)
{
    auto newFont = std::make_unique<celadon::SoundFont>();
    std::string err;
    if (! newFont->loadFile (file.getFullPathName().toRawUTF8(), err))
    {
        error = juce::String (err);
        return false;
    }

    suspendProcessing (true);
    engine.setFont (newFont.get());
    fontData = std::move (newFont);
    suspendProcessing (false);

    cartPath = file.getFullPathName();
    apvts.state.setProperty ("sf2Path", cartPath, nullptr);
    apvts.state.setProperty ("presetIndex", 0, nullptr);
    fontVersion.fetch_add (1);
    return true;
}

juce::String CeladonProcessor::currentCartName() const
{
    if (fontData == nullptr) return "NO CART";
    if (! fontData->name.empty()) return juce::String (fontData->name).toUpperCase();
    return juce::File (cartPath).getFileNameWithoutExtension().toUpperCase();
}

void CeladonProcessor::selectPreset (int index)
{
    pendingPreset.store (juce::jlimit (0, juce::jmax (0, presetCount() - 1), index));
    apvts.state.setProperty ("presetIndex", pendingPreset.load(), nullptr);
}

void CeladonProcessor::stepPreset (int delta)
{
    if (presetCount() == 0) return;
    selectPreset ((presetIndex() + delta + presetCount()) % presetCount());
}

juce::String CeladonProcessor::presetInfo (int i) const
{
    auto* f = font();
    if (f == nullptr || i < 0 || i >= (int) f->presets.size()) return {};
    auto& pr = f->presets[(size_t) i];
    return juce::String::formatted ("BANK %03d PROG %03d", pr.bank, pr.program);
}

// ---------------- state ----------------
void CeladonProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    // copyState() returns an independent snapshot — safe to stamp the live
    // preset index on it without touching apvts.state from this thread.
    auto snapshot = apvts.copyState();
    snapshot.setProperty ("presetIndex", presetIndex(), nullptr);
    if (auto xml = snapshot.createXml())
        copyXmlToBinary (*xml, dest);
}

void CeladonProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;

    apvts.replaceState (juce::ValueTree::fromXml (*xml));

    const juce::String path = apvts.state.getProperty ("sf2Path", "").toString();
    const int preset = (int) apvts.state.getProperty ("presetIndex", 0);

    if (path.isNotEmpty())
    {
        // Weak reference: hosts may destroy the processor (plugin scans,
        // fast project close) before this message-thread lambda ever runs.
        juce::MessageManager::callAsync (
            [weak = juce::WeakReference<CeladonProcessor> (this), path, preset]
        {
            if (auto* self = weak.get())
            {
                juce::File f (path);
                juce::String err;
                if (f.existsAsFile() && self->loadSoundFontFile (f, err))
                    self->selectPreset (preset);
            }
        });
    }
}

juce::AudioProcessorEditor* CeladonProcessor::createEditor()
{
    return new CeladonEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CeladonProcessor();
}
