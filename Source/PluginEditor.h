#pragma once
#include "PluginProcessor.h"

// CELADON interface — "a handheld lying on its side".
// Modern take on the DMG: warm greige shell drawn in code, a dot-matrix
// screen in the four greens as the centerpiece (live waveform from the real
// output), flat cream knobs with a single burgundy accent, A/B buttons doing
// the MAIN/ALT flip, angled GUIDE/PANIC pills, speaker slashes (one blind),
// and edge wheels for volume and screen palette. No Nintendo trade dress:
// we quote the screen, the palette and the details — the body is our own.

namespace cpal
{
    // shell
    const juce::Colour shell     { 0xffc6c0ba };
    const juce::Colour shellDark { 0xffaba49c };
    const juce::Colour shellLine { 0xff8f8a83 };
    const juce::Colour ink       { 0xff3a3834 };
    const juce::Colour dim       { 0xff6b6862 };
    const juce::Colour cream     { 0xffedeae4 };
    const juce::Colour burgundy  { 0xff9b2256 };
    const juce::Colour burgundyDk{ 0xff7a1a44 };
    const juce::Colour indigo    { 0xff484686 };
    const juce::Colour bezel     { 0xff2b2b31 };
    const juce::Colour ledRed    { 0xffe8342c };
    const juce::Colour track     { 0xffb7b1aa };

    struct ScreenTheme { juce::Colour g0, g1, g2, g3; const char* name; };
    const ScreenTheme themes[3] = {
        { juce::Colour (0xff0f380f), juce::Colour (0xff306230),
          juce::Colour (0xff8bac0f), juce::Colour (0xff9bbc0f), "DMG" },
        { juce::Colour (0xff1a1a1a), juce::Colour (0xff545454),
          juce::Colour (0xffa9a9a9), juce::Colour (0xffdedede), "POCKET" },
        { juce::Colour (0xff0b2e1e), juce::Colour (0xff1e6e46),
          juce::Colour (0xff7ed08a), juce::Colour (0xffe0f8d0), "LIT" }
    };
}

//==============================================================================
class CeladonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    CeladonLookAndFeel();
    static juce::Font pixel   (float h);   // Press Start 2P (wordmark, preset name)
    static juce::Font label   (float h);   // Silkscreen (labels, captions)
    static juce::Font readout (float h);   // VT323 (values, screen text)
    void drawPopupMenuBackground (juce::Graphics&, int, int) override;
};

//==============================================================================
// A flat DMG-style knob. Two host parameters (primary + optional ALT); the
// global ALT mode only changes which one the physical knob points at.
class DmgKnob : public juce::Component
{
public:
    struct Spec
    {
        const char* primaryId; const char* primaryName;
        const char* altId;     const char* altName;   // altId may be nullptr
        bool mini = false;                            // FX-strip variant
    };

    DmgKnob (juce::AudioProcessorValueTreeState&, const Spec&);

    void setAltMode (bool);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void resized() override;
    void refreshReadout();

    std::function<void (bool)> onAltRequest;

private:
    const char* activeId() const;
    juce::String formatValue() const;

    juce::AudioProcessorValueTreeState& apvts;
    Spec spec;
    bool altMode = false, hoverCaption = false;
    juce::Slider knob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    juce::Rectangle<int> captionArea;
    juce::String readoutText;
};

//==============================================================================
// The dot-matrix screen: live waveform from the processor's scope FIFO,
// preset identity, envelope and filter thumbnails, voice cells.
class ScreenView : public juce::Component
{
public:
    explicit ScreenView (CeladonProcessor&);
    void setTheme (int t) { theme = juce::jlimit (0, 2, t); gridCache = juce::Image(); repaint(); }
    int  getTheme() const { return theme; }
    void paint (juce::Graphics&) override;

private:
    CeladonProcessor& proc;
    int theme = 0;
    juce::Image gridCache;
};

//==============================================================================
class PillButton : public juce::Component
{
public:
    PillButton (const char* label, float angleDeg);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hover = false; repaint(); }
    std::function<void()> onClick;
private:
    juce::String text; float angle; bool hover = false;
};

//==============================================================================
// A (MAIN) and B (ALT) buttons, diagonal like the handheld's.
class ABButtons : public juce::Component
{
public:
    void setAltMode (bool alt) { altMode = alt; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    std::function<void (bool)> onSelect;
private:
    bool altMode = false;
    juce::Rectangle<float> aCircle, bCircle;
};

//==============================================================================
// Edge wheel (left = volume bound to the parameter, right = screen palette).
class EdgeWheel : public juce::Component
{
public:
    EdgeWheel (bool leftEdge, const char* caption);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    std::function<void (float)> onDelta;    // normalized drag delta
    std::function<void()> onClick;
    void nudgeVisual (float amount) { spin += amount; repaint(); }
private:
    bool left; juce::String text; float spin = 0.0f; int lastY = 0;
};

//==============================================================================
class GuideOverlay : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { setVisible (false); }
};

//==============================================================================
class CeladonEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit CeladonEditor (CeladonProcessor&);
    ~CeladonEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void setAltMode (bool);
    void openCartChooser();
    void showPresetMenu();

    CeladonProcessor& proc;
    CeladonLookAndFeel lnf;

    std::vector<std::unique_ptr<DmgKnob>> knobs;
    ScreenView screen;
    ABButtons ab;
    PillButton guideBtn { "GUIDE", -14.0f }, panicBtn { "PANIC", -14.0f };
    juce::TextButton prevBtn { "<" }, nextBtn { ">" };
    EdgeWheel volWheel { true, "VOL" }, contrastWheel { false, "CONTRAST" };
    GuideOverlay guide;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> cartArea;
    bool altMode = false;
    bool lastSyncShown = false;
    int lastFontVersion = -1;
    int ledCountdown = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CeladonEditor)
};
