#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "DSP/CeladonEngine.h"

class CeladonProcessor : public juce::AudioProcessor
{
public:
    CeladonProcessor();
    ~CeladonProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override               { return true; }

    const juce::String getName() const override   { return "Celadon"; }
    bool acceptsMidi() const override             { return true; }
    bool producesMidi() const override            { return false; }
    double getTailLengthSeconds() const override  { return 6.0; }

    int getNumPrograms() override                 { return 1; }
    int getCurrentProgram() override              { return 0; }
    void setCurrentProgram (int) override         {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // ---- cart management (message thread) ----
    bool loadSoundFontFile (const juce::File&, juce::String& error);
    juce::String currentCartName() const;
    juce::String currentCartPath() const { return cartPath; }

    // ---- preset access (message thread; program changes go via MIDI) ----
    void  selectPreset (int index);              // clamps
    void  stepPreset (int delta);
    int   presetCount() const                    { return engine.presetCount(); }
    int   presetIndex() const                    { return engine.presetIndex(); }
    juce::String presetName() const              { return engine.presetName(); }
    juce::String presetInfo (int i) const;       // "BANK bbb PROG ppp"
    const celadon::SoundFont* font() const       { return engine.currentFont(); }

    void panic()                                 { panicFlag.store (true); }

    // ---- UI feed (audio thread writes, editor timer reads) ----
    static constexpr int kScopeSize = 512;       // power of two
    float scope[kScopeSize] {};
    std::atomic<int>   scopeWrite { 0 };
    std::atomic<int>   uiVoices { 0 };
    std::atomic<float> uiPeak { 0.0f };
    std::atomic<int>   midiActivity { 0 };       // decremented by the editor
    std::atomic<int>   fontVersion { 0 };        // bumped on every cart/preset change

    celadon::Engine engine;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void handleMidiEvent (const juce::MidiMessage&);
    celadon::SynthParams gatherParams();

    std::unique_ptr<celadon::SoundFont> fontData;
    juce::String cartPath;
    juce::AudioBuffer<float> monoScratch;
    std::atomic<bool> panicFlag { false };
    std::atomic<int>  pendingPreset { -1 };

    JUCE_DECLARE_WEAK_REFERENCEABLE (CeladonProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CeladonProcessor)
};
