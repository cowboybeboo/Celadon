#include "PluginEditor.h"

#if CELADON_HAVE_FONTS
 #include "BinaryData.h"
#endif

//==============================================================================
// fonts
static juce::Typeface::Ptr loadFace (const void* data, size_t size)
{
    return juce::Typeface::createSystemTypefaceFor (data, size);
}

juce::Font CeladonLookAndFeel::pixel (float h)
{
#if CELADON_HAVE_FONTS
    static juce::Typeface::Ptr tf = loadFace (BinaryData::PressStart2PRegular_ttf,
                                              (size_t) BinaryData::PressStart2PRegular_ttfSize);
    if (tf != nullptr) return juce::Font (juce::FontOptions (tf)).withHeight (h);
#endif
    return juce::Font (juce::FontOptions (h, juce::Font::bold));
}

juce::Font CeladonLookAndFeel::label (float h)
{
#if CELADON_HAVE_FONTS
    static juce::Typeface::Ptr tf = loadFace (BinaryData::SilkscreenRegular_ttf,
                                              (size_t) BinaryData::SilkscreenRegular_ttfSize);
    if (tf != nullptr) return juce::Font (juce::FontOptions (tf)).withHeight (h);
#endif
    return juce::Font (juce::FontOptions (h * 0.85f, juce::Font::bold)).withExtraKerningFactor (0.12f);
}

juce::Font CeladonLookAndFeel::readout (float h)
{
#if CELADON_HAVE_FONTS
    static juce::Typeface::Ptr tf = loadFace (BinaryData::VT323Regular_ttf,
                                              (size_t) BinaryData::VT323Regular_ttfSize);
    if (tf != nullptr) return juce::Font (juce::FontOptions (tf)).withHeight (h);
#endif
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h * 0.8f, juce::Font::plain));
}

CeladonLookAndFeel::CeladonLookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, cpal::bezel);
    setColour (juce::PopupMenu::textColourId, cpal::cream);
    setColour (juce::PopupMenu::headerTextColourId, cpal::track);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, cpal::burgundy);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::TextButton::buttonColourId, cpal::track);
    setColour (juce::TextButton::textColourOffId, cpal::ink);
    setColour (juce::AlertWindow::backgroundColourId, cpal::shell);
    setColour (juce::AlertWindow::textColourId, cpal::ink);
}

void CeladonLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (cpal::bezel);
    g.setColour (cpal::shellLine);
    g.drawRect (0, 0, w, h);
}

//==============================================================================
// DmgKnob
DmgKnob::DmgKnob (juce::AudioProcessorValueTreeState& s, const Spec& sp)
    : apvts (s), spec (sp)
{
    knob.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    knob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    knob.setRotaryParameters (juce::degreesToRadians (-135.0f) + juce::MathConstants<float>::twoPi,
                              juce::degreesToRadians (135.0f)  + juce::MathConstants<float>::twoPi, true);
    knob.setAlpha (0.0f);                      // invisible: we paint everything
    knob.setName ("knobDrag");
    addAndMakeVisible (knob);
    attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, spec.primaryId, knob);
    knob.onValueChange = [this] { refreshReadout(); repaint(); };
    refreshReadout();
}

const char* DmgKnob::activeId() const
{
    return (altMode && spec.altId != nullptr) ? spec.altId : spec.primaryId;
}

void DmgKnob::setAltMode (bool alt)
{
    if (spec.altId == nullptr) alt = false;
    if (alt == altMode) return;
    altMode = alt;
    attach.reset();
    attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, activeId(), knob);
    refreshReadout();
    repaint();
}

void DmgKnob::resized()
{
    if (spec.mini)
        knob.setBounds (6, getHeight() / 2 - 20, 40, 40);
    else
        knob.setBounds (getWidth() / 2 - 38, 0, 76, 76);
}

juce::String DmgKnob::formatValue() const
{
    auto* par = apvts.getParameter (activeId());
    if (par == nullptr) return {};
    const float norm = par->getValue();

    if (auto* ch = dynamic_cast<juce::AudioParameterChoice*> (par))
        return ch->getCurrentChoiceName().toUpperCase();
    if (dynamic_cast<juce::AudioParameterBool*> (par) != nullptr)
        return norm > 0.5f ? "ON" : "OFF";

    auto* fp = dynamic_cast<juce::AudioParameterFloat*> (par);
    if (fp == nullptr) return par->getCurrentValueAsText().toUpperCase();
    const float v = fp->get();
    const juce::String id (activeId());

    if (id == "attack" || id == "decay" || id == "release")
        return juce::String::formatted ("x%.2f", v);
    if (id == "sustain")               return juce::String::formatted ("%+.1f DB", v);
    if (id == "cutoff" || id == "envamt")
        return juce::String::formatted ("%+.1f OCT", v / 1200.0f);
    if (id == "reso")                  return juce::String::formatted ("%.1f DB", v);
    if (id == "keytrack")              return juce::String::formatted ("%.0f CT/KEY", v);
    if (id == "lforate" || id == "chorusrate")
        return juce::String::formatted ("%.2f HZ", v);
    if (id == "glide")
        return v < 1.0f ? juce::String ("OFF") : juce::String::formatted ("%.0f MS", v);
    if (id == "lfodelay" || id == "menva" || id == "menvd" || id == "menvr")
        return v < 1000.0f ? juce::String::formatted ("%.0f MS", v)
                           : juce::String::formatted ("%.2f S", v * 0.001f);
    if (id == "tune")                  return juce::String::formatted ("%+.0f ST", v);
    if (id == "volume")                return juce::String::formatted ("%+.1f DB", v);
    return juce::String::formatted ("%.0f %%", v);
}

void DmgKnob::refreshReadout()
{
    auto t = formatValue();
    if (t != readoutText) { readoutText = t; repaint(); }
}

void DmgKnob::paint (juce::Graphics& g)
{
    auto* par = apvts.getParameter (activeId());
    const float norm = par != nullptr ? par->getValue() : 0.0f;
    const bool alt = altMode && spec.altId != nullptr;
    const juce::Colour accent = cpal::burgundy;

    const float cx = spec.mini ? 26.0f : getWidth() * 0.5f;
    const float cy = spec.mini ? getHeight() * 0.5f : 38.0f;
    const float r  = spec.mini ? 20.0f : 32.0f;

    // drop shadow + disc
    g.setColour (juce::Colours::black.withAlpha (0.16f));
    g.fillEllipse (cx - r, cy - r + 3.0f, r * 2, r * 2);
    g.setColour (cpal::cream);
    g.fillEllipse (cx - r, cy - r, r * 2, r * 2);
    g.setColour (cpal::shellLine);
    g.drawEllipse (cx - r, cy - r, r * 2, r * 2, 1.5f);
    g.setColour (cpal::shellDark.withAlpha (0.6f));
    g.drawEllipse (cx - r + 7, cy - r + 7, (r - 7) * 2, (r - 7) * 2, 1.0f);

    // arcs (angles from 12 o'clock)
    const float a0 = juce::degreesToRadians (-135.0f);
    const float a1 = juce::degreesToRadians (135.0f);
    const float av = a0 + norm * (a1 - a0);
    juce::Path track, arc;
    track.addCentredArc (cx, cy, r + 6, r + 6, 0, a0, a1, true);
    g.setColour (cpal::track);
    g.strokePath (track, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    arc.addCentredArc (cx, cy, r + 6, r + 6, 0, a0, av, true);
    g.setColour (accent);
    g.strokePath (arc, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // pointer
    const float px = std::sin (av), py = -std::cos (av);
    g.drawLine (cx + px * 7, cy + py * 7, cx + px * (r - 4), cy + py * (r - 4), 3.5f);

    if (spec.mini)
    {
        g.setColour (cpal::ink);
        g.setFont (CeladonLookAndFeel::label (10.0f));
        g.drawText (juce::String (alt ? spec.altName : spec.primaryName).toUpperCase(),
                    52, getHeight() / 2 - 18, getWidth() - 54, 14, juce::Justification::centredLeft);
        g.setColour (alt ? accent : juce::Colour (0xff1e1d1b));
        g.setFont (CeladonLookAndFeel::readout (17.0f));
        g.drawText (readoutText, 52, getHeight() / 2, getWidth() - 54, 16, juce::Justification::centredLeft);
        return;
    }

    const int nameY = 84, readY = 100, altY = 118;
    g.setColour (cpal::ink);
    g.setFont (CeladonLookAndFeel::label (11.0f));
    g.drawText (juce::String (alt ? spec.altName : spec.primaryName).toUpperCase(),
                0, nameY, getWidth(), 14, juce::Justification::centred);
    g.setColour (alt ? accent : juce::Colour (0xff1e1d1b));
    g.setFont (CeladonLookAndFeel::readout (18.0f));
    g.drawText (readoutText, 0, readY, getWidth(), 17, juce::Justification::centred);

    if (spec.altId != nullptr)
    {
        g.setColour (alt ? cpal::dim : accent.withAlpha (hoverCaption ? 1.0f : 0.75f));
        g.setFont (CeladonLookAndFeel::label (8.0f));
        g.drawText (alt ? juce::String ("MAIN · ") + spec.primaryName
                        : juce::String ("ALT · ")  + spec.altName,
                    0, altY, getWidth(), 11, juce::Justification::centred);
    }
    captionArea = { 0, nameY, getWidth(), altY + 12 - nameY };
}

void DmgKnob::mouseDown (const juce::MouseEvent& e)
{
    if (spec.altId != nullptr && ! spec.mini && captionArea.contains (e.getPosition()))
        if (onAltRequest) onAltRequest (! altMode);
}
void DmgKnob::mouseMove (const juce::MouseEvent& e)
{
    bool h = spec.altId != nullptr && ! spec.mini && captionArea.contains (e.getPosition());
    if (h != hoverCaption)
    {
        hoverCaption = h;
        setMouseCursor (h ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}
void DmgKnob::mouseExit (const juce::MouseEvent&)
{
    if (hoverCaption) { hoverCaption = false; repaint(); }
}

//==============================================================================
// ScreenView
ScreenView::ScreenView (CeladonProcessor& p) : proc (p)
{
    setInterceptsMouseClicks (true, false);
}

void ScreenView::paint (juce::Graphics& g)
{
    const auto& th = cpal::themes[theme];
    const int W = getWidth(), H = getHeight();

    g.fillAll (th.g3);
    g.setColour (th.g2);
    g.drawRect (1, 1, W - 2, H - 2, 2);

    auto get = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };

    // header
    g.setColour (th.g1);
    g.setFont (CeladonLookAndFeel::readout (16.0f));
    g.drawText (proc.currentCartName().substring (0, 24), 16, 12, W - 200, 18, juce::Justification::left);
    g.drawText (juce::String::formatted ("PRESET %d/%d",
                    proc.presetCount() > 0 ? proc.presetIndex() + 1 : 0, proc.presetCount()),
                W - 190, 12, 174, 18, juce::Justification::right);

    // voice cells
    const int voices = proc.uiVoices.load();
    const int lit = juce::jlimit (0, 16, voices);
    for (int i = 0; i < 16; ++i)
    {
        juce::Rectangle<int> c (16 + i * 12, 36, 9, 9);
        g.setColour (th.g1);
        g.drawRect (c, 1);
        if (i < lit) { g.setColour (th.g0); g.fillRect (c.reduced (1)); }
    }
    g.setColour (th.g1);
    g.setFont (CeladonLookAndFeel::readout (14.0f));
    g.drawText (juce::String (voices) + " VOICES", W - 190, 32, 174, 15, juce::Justification::right);

    // preset identity
    g.setColour (th.g0);
    g.setFont (CeladonLookAndFeel::pixel (19.0f));
    g.drawText (proc.presetName().toUpperCase().substring (0, 18), 16, 64, W - 32, 24, juce::Justification::left);
    g.setColour (th.g1);
    g.setFont (CeladonLookAndFeel::readout (15.0f));
    g.drawText (proc.presetInfo (proc.presetIndex()) + " · CLICK FOR LIST", 16, 92, W - 32, 16, juce::Justification::left);

    // live waveform (quantized to the pixel grid)
    {
        const int top = 128, bottom = 288, midY = (top + bottom) / 2;
        g.setColour (th.g1.withAlpha (0.7f));
        g.drawHorizontalLine (midY, 16.0f, (float) (W - 16));
        const int wr = proc.scopeWrite.load (std::memory_order_acquire);
        const int span = W - 32;
        juce::Path p;
        bool started = false;
        for (int x = 0; x < span; ++x)
        {
            const int idx = wr - span + x;
            float v = proc.scope[idx & (CeladonProcessor::kScopeSize - 1)];
            v = juce::jlimit (-1.0f, 1.0f, v * 1.6f);
            float y = (float) midY - v * (float) (bottom - top) * 0.5f;
            y = std::round (y * 0.25f) * 4.0f;                      // chunky 4-px steps
            if (! started) { p.startNewSubPath (16.0f + (float) x, y); started = true; }
            else            p.lineTo (16.0f + (float) x, y);
        }
        g.setColour (th.g0);
        g.strokePath (p, juce::PathStrokeType (3.0f));
    }

    // envelope thumbnail (knob multipliers on a nominal shape)
    {
        juce::Rectangle<float> box (16.0f, 306.0f, 190.0f, 58.0f);
        g.setColour (th.g1.withAlpha (0.6f));
        g.drawRect (box, 1.0f);
        g.setFont (CeladonLookAndFeel::readout (14.0f));
        g.drawText ("ENV", (int) box.getX(), (int) box.getY() - 16, 60, 14, juce::Justification::left);
        auto n = [] (float v) { return (std::log (v / 0.25f) / std::log (16.0f)); }; // 0.25..4 -> 0..1
        const float a = n (get ("attack")), d = n (get ("decay")), r = n (get ("release"));
        const float sDb = get ("sustain");
        const float sus = juce::jlimit (0.06f, 1.0f, juce::Decibels::decibelsToGain (juce::jlimit (-24.0f, 0.0f, -6.0f + sDb)));
        const float x0 = box.getX() + 4, y1 = box.getBottom() - 5, yTop = box.getY() + 6;
        const float wA = 12 + 38 * a, wD = 12 + 38 * d, wR = 14 + 44 * r;
        juce::Path p;
        p.startNewSubPath (x0, y1);
        p.lineTo (x0 + wA, yTop);
        p.lineTo (x0 + wA + wD, y1 - (y1 - yTop) * sus);
        p.lineTo (box.getRight() - 6 - wR, y1 - (y1 - yTop) * sus);
        p.lineTo (box.getRight() - 6, y1);
        g.setColour (th.g0);
        g.strokePath (p, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved));
    }

    // filter thumbnail
    {
        juce::Rectangle<float> box (242.0f, 306.0f, 190.0f, 58.0f);
        g.setColour (th.g1.withAlpha (0.6f));
        g.drawRect (box, 1.0f);
        g.setFont (CeladonLookAndFeel::readout (14.0f));
        g.drawText ("FILTER", (int) box.getX(), (int) box.getY() - 16, 80, 14, juce::Justification::left);
        const float cut = (get ("cutoff") + 4800.0f) / 9600.0f;      // 0..1
        const float res = get ("reso") / 24.0f;
        const float kneeX = box.getX() + 8 + cut * (box.getWidth() - 30);
        const float flatY = box.getY() + 18 + 24 * (1.0f - 0.4f);
        juce::Path p;
        p.startNewSubPath (box.getX() + 4, flatY);
        p.lineTo (kneeX - 12, flatY);
        p.quadraticTo (kneeX, flatY - res * 22.0f, kneeX + 6, flatY + 8);
        p.quadraticTo (kneeX + 14, box.getBottom() - 4, juce::jmin (kneeX + 26, box.getRight() - 4), box.getBottom() - 4);
        g.setColour (th.g0);
        g.strokePath (p, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved));
    }

    // bottom status line
    g.setColour (th.g1);
    g.setFont (CeladonLookAndFeel::readout (15.0f));
    g.drawText (juce::String::formatted ("A x%.2f  D x%.2f  S %+.0fDB  R x%.2f",
                                         get ("attack"), get ("decay"), get ("sustain"), get ("release")),
                16, H - 26, W / 2 + 40, 16, juce::Justification::left);
    g.drawText (juce::String::formatted ("CUT %+.1fOCT RES %.0fDB",
                                         get ("cutoff") / 1200.0f, get ("reso")),
                W / 2, H - 26, W / 2 - 16, 16, juce::Justification::right);

    // dot-matrix grid overlay (cached)
    if (gridCache.isNull() || gridCache.getWidth() != W)
    {
        gridCache = juce::Image (juce::Image::ARGB, W, H, true);
        juce::Graphics gg (gridCache);
        gg.setColour (th.g0.withAlpha (0.06f));
        for (int x = 0; x < W; x += 4) gg.drawVerticalLine (x, 0.0f, (float) H);
        for (int y = 0; y < H; y += 4) gg.drawHorizontalLine (y, 0.0f, (float) W);
    }
    g.drawImageAt (gridCache, 0, 0);
}

//==============================================================================
// PillButton
PillButton::PillButton (const char* l, float angleDeg) : text (l), angle (angleDeg) {}

void PillButton::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    juce::Rectangle<float> pill (b.getCentreX() - 32.0f, 6.0f, 64.0f, 18.0f);
    juce::Path p;
    p.addRoundedRectangle (pill, 9.0f);
    p.applyTransform (juce::AffineTransform::rotation (juce::degreesToRadians (angle),
                                                       pill.getCentreX(), pill.getCentreY()));
    g.setColour (hover ? cpal::shellLine : juce::Colour (0xff8f8a83));
    g.fillPath (p);
    g.setColour (juce::Colour (0xff6f6a64));
    g.strokePath (p, juce::PathStrokeType (1.2f));
    g.setColour (cpal::dim);
    g.setFont (CeladonLookAndFeel::label (9.0f));
    g.drawText (text, 0, (int) pill.getBottom() + 6, getWidth(), 12, juce::Justification::centred);
}

//==============================================================================
// ABButtons
void ABButtons::paint (juce::Graphics& g)
{
    bCircle = { 4.0f, 33.0f, 52.0f, 52.0f };
    aCircle = { (float) getWidth() - 76.0f, 8.0f, 52.0f, 52.0f };

    auto draw = [&g] (juce::Rectangle<float> c, bool active, const char* cap, int capY)
    {
        g.setColour (cpal::burgundy);
        g.fillEllipse (c);
        g.setColour (cpal::burgundyDk);
        g.drawEllipse (c, 2.0f);
        g.setColour (juce::Colours::white.withAlpha (active ? 0.75f : 0.22f));
        g.drawEllipse (c.reduced (3.0f).translated (0.0f, -2.0f), 1.5f);
        g.setColour (cpal::dim);
        g.setFont (CeladonLookAndFeel::label (9.0f));
        g.drawText (cap, (int) c.getX() - 18, capY, (int) c.getWidth() + 36, 12, juce::Justification::centred);
    };
    draw (bCircle, altMode,  "B · ALT",  (int) bCircle.getBottom() + 6);
    draw (aCircle, ! altMode, "A · MAIN", (int) aCircle.getBottom() + 6);
}

void ABButtons::mouseDown (const juce::MouseEvent& e)
{
    auto pos = e.position;
    if (bCircle.contains (pos) && onSelect) onSelect (true);
    else if (aCircle.contains (pos) && onSelect) onSelect (false);
}

//==============================================================================
// EdgeWheel
EdgeWheel::EdgeWheel (bool leftEdge, const char* caption) : left (leftEdge), text (caption) {}

void EdgeWheel::paint (juce::Graphics& g)
{
    const float cy = 40.0f;
    const float cx = left ? -16.0f : (float) getWidth() + 16.0f;
    g.setColour (juce::Colour (0xff8f8a83));
    g.fillEllipse (cx - 30, cy - 30, 60, 60);
    g.setColour (juce::Colour (0xff6f6a64));
    g.drawEllipse (cx - 30, cy - 30, 60, 60, 1.5f);
    for (int i = -3; i <= 3; ++i)
    {
        float y = cy + (float) i * 9.0f + std::fmod (spin, 9.0f);
        if (y < cy - 28 || y > cy + 28) continue;
        g.drawLine (left ? 2.0f : (float) getWidth() - 14.0f, y,
                    left ? 14.0f : (float) getWidth() - 2.0f, y, 2.0f);
    }
    g.setColour (cpal::dim);
    g.setFont (CeladonLookAndFeel::label (7.0f));
    g.drawText (text, 0, 78, getWidth(), 10,
                left ? juce::Justification::centredLeft : juce::Justification::centredRight);
}

void EdgeWheel::mouseDown (const juce::MouseEvent& e)
{
    lastY = e.getPosition().getY();
    if (onClick) onClick();
}
void EdgeWheel::mouseDrag (const juce::MouseEvent& e)
{
    const int dy = lastY - e.getPosition().getY();
    lastY = e.getPosition().getY();
    if (dy != 0 && onDelta) { onDelta ((float) dy / 220.0f); nudgeVisual ((float) dy * 0.4f); }
}

//==============================================================================
// GuideOverlay
void GuideOverlay::paint (juce::Graphics& g)
{
    g.fillAll (cpal::bezel.withAlpha (0.96f));
    auto area = getLocalBounds().reduced (70, 40);

    g.setColour (cpal::burgundy);
    g.setFont (CeladonLookAndFeel::pixel (20.0f));
    g.drawText ("CELADON GUIDE", area.removeFromTop (34), juce::Justification::left);
    g.setColour (cpal::track);
    g.setFont (CeladonLookAndFeel::label (9.0f));
    g.drawText ("CLICK ANYWHERE TO CLOSE", getLocalBounds().reduced (70, 40).removeFromTop (30),
                juce::Justification::right);
    area.removeFromTop (10);

    const char* colA =
        "THE CART\n"
        "Click the cartridge at the top to load any .sf2 soundfont. The < > arrows and\n"
        "MIDI program change step through its presets; click the preset name on the\n"
        "screen for the full list. Celadon honors what the cart says: key maps, loops,\n"
        "stereo pairs, tuning, per-zone envelopes and filters.\n\n"
        "THE KNOBS - MAIN SET\n"
        "ATTACK / DECAY / RELEASE scale the cart's own envelope times (x0.25 to x4,\n"
        "centre = as the maker intended). SUSTAIN offsets the sustain level in dB.\n"
        "CUTOFF shifts every zone's filter by up to +/-4 octaves; RESONANCE adds up\n"
        "to 24 dB of peak. GLIDE slides between notes; TUNE transposes +/-12 semitones.\n\n"
        "THE ALT SET (B BUTTON)\n"
        "Press B - or click any knob's small caption - and every knob flips to its\n"
        "second function (readouts turn burgundy): the MOD ENVELOPE (an ADSR aimed at\n"
        "the filter via ENV AMT), KEYTRACK, LFO DELAY and TARGET, VEL SENS and WIDTH,\n"
        "and each effect's second parameter. Press A to come back. Both sets are\n"
        "separate host parameters - flipping the panel never touches your values.";

    const char* colB =
        "LFO\n"
        "RATE (free Hz, or click SYNC for tempo divisions), DEPTH, and a TARGET\n"
        "switch: PITCH (vibrato), FILTER (wah), or AMP (tremolo). LFO DELAY fades\n"
        "the wobble in after each note.\n\n"
        "FX CHAIN\n"
        "CRUSH - bit depth and sample-rate reduction, the handheld-speaker knob.\n"
        "CHORUS - two-voice, very 90s. DELAY - tempo-synced echo (division on ALT).\n"
        "REVERB - a small room; SIZE on ALT. All mixes at zero = bit-transparent.\n\n"
        "AROUND THE SHELL\n"
        "VOLUME wheel on the left edge; CONTRAST wheel on the right swaps the screen\n"
        "palette (DMG green / Pocket grey / backlit). PANIC silences everything.\n"
        "The screen's waveform is the real output signal.\n\n"
        "MIDI & MACROS\n"
        "CC 20-47 map to every knob (see README for the table), CC 64 sustain pedal,\n"
        "program change steps presets, pitch bend +/-2 semitones. Suggested Ableton\n"
        "rack macros: Cutoff, Attack, Release, LFO Depth, Crush, Delay, Reverb, Glide.";

    auto half = area.getWidth() / 2 - 20;
    g.setColour (cpal::cream);
    g.setFont (CeladonLookAndFeel::readout (16.0f));
    g.drawFittedText (colA, area.removeFromLeft (half), juce::Justification::topLeft, 60);
    area.removeFromLeft (40);
    g.drawFittedText (colB, area, juce::Justification::topLeft, 60);
}

//==============================================================================
// Editor
CeladonEditor::CeladonEditor (CeladonProcessor& p)
    : AudioProcessorEditor (&p), proc (p), screen (p)
{
    setLookAndFeel (&lnf);

    auto add = [this] (DmgKnob::Spec sp)
    {
        auto k = std::make_unique<DmgKnob> (proc.apvts, sp);
        k->onAltRequest = [this] (bool want) { setAltMode (want); };
        addAndMakeVisible (*k);
        knobs.push_back (std::move (k));
    };

    // left panel
    add ({ "attack",  "ATTACK",  "menva", "MOD-ENV A", false });
    add ({ "decay",   "DECAY",   "menvd", "MOD-ENV D", false });
    add ({ "sustain", "SUSTAIN", "menvs", "MOD-ENV S", false });
    add ({ "release", "RELEASE", "menvr", "MOD-ENV R", false });
    add ({ "glide",   "GLIDE",   "velsens", "VEL SENS", false });
    add ({ "tune",    "TUNE",    "width",   "WIDTH",    false });
    // right panel
    add ({ "cutoff",  "CUTOFF",    "envamt",   "ENV AMT",  false });
    add ({ "reso",    "RESONANCE", "keytrack", "KEYTRACK", false });
    add ({ "lforate", "RATE",      "lfodelay", "DELAY",    false });
    add ({ "lfodepth","DEPTH",     "lfotarget","TARGET",   false });
    // fx strip (mini)
    add ({ "crush",     "CRUSH",  "crushrate",  "RATE", true });
    add ({ "chorus",    "CHORUS", "chorusrate", "RATE", true });
    add ({ "delaymix",  "DELAY",  "delaydiv",   "TIME", true });
    add ({ "reverbmix", "REVERB", "reverbsize", "SIZE", true });

    addAndMakeVisible (screen);
    screen.addMouseListener (this, false);

    ab.onSelect = [this] (bool wantAlt) { setAltMode (wantAlt); };
    addAndMakeVisible (ab);

    guideBtn.onClick = [this] { guide.setVisible (true); guide.toFront (false); };
    panicBtn.onClick = [this] { proc.panic(); };
    addAndMakeVisible (guideBtn);
    addAndMakeVisible (panicBtn);

    prevBtn.onClick = [this] { proc.stepPreset (-1); };
    nextBtn.onClick = [this] { proc.stepPreset (+1); };
    addAndMakeVisible (prevBtn);
    addAndMakeVisible (nextBtn);

    volWheel.onDelta = [this] (float d)
    {
        if (auto* par = proc.apvts.getParameter ("volume"))
            par->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, par->getValue() + d));
    };
    contrastWheel.onClick = [this]
    {
        const int t = (screen.getTheme() + 1) % 3;
        screen.setTheme (t);
        proc.apvts.state.setProperty ("screenTheme", t, nullptr);
        contrastWheel.nudgeVisual (4.0f);
    };
    addAndMakeVisible (volWheel);
    addAndMakeVisible (contrastWheel);

    addChildComponent (guide);            // hidden until asked

    screen.setTheme ((int) proc.apvts.state.getProperty ("screenTheme", 0));

    startTimerHz (30);
    setSize (1280, 720);                  // LAST: fires resized() synchronously
}

CeladonEditor::~CeladonEditor()
{
    screen.removeMouseListener (this);
    setLookAndFeel (nullptr);
}

void CeladonEditor::resized()
{
    if (knobs.size() < 14) return;        // guard against mid-construction calls

    auto place = [this] (int i, int cx, int cy) { knobs[(size_t) i]->setBounds (cx - 58, cy - 38, 116, 132); };
    // left
    place (0, 120, 214); place (1, 250, 214);
    place (2, 120, 344); place (3, 250, 344);
    place (4, 120, 514); place (5, 250, 514);
    // right
    place (6, 1030, 214); place (7, 1160, 214);
    place (8, 1030, 494); place (9, 1160, 494);
    // fx minis
    for (int i = 0; i < 4; ++i)
        knobs[(size_t) (10 + i)]->setBounds (305 + i * 172, 630, 166, 76);

    screen.setBounds (416, 156, 448, 404);
    ab.setBounds (1000, 296, 190, 132);
    guideBtn.setBounds (90, 624, 80, 52);
    panicBtn.setBounds (200, 624, 80, 52);
    prevBtn.setBounds (1032, 32, 26, 30);
    nextBtn.setBounds (1062, 32, 26, 30);
    volWheel.setBounds (16, 300, 46, 92);
    contrastWheel.setBounds (1218, 300, 46, 92);
    guide.setBounds (getLocalBounds());

    cartArea = { 700, 26, 320, 56 };
}

void CeladonEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff7c7873));

    // shell
    juce::Rectangle<float> shell (16, 16, 1248, 688);
    g.setColour (cpal::shell);
    g.fillRoundedRectangle (shell, 30.0f);
    g.setColour (cpal::shellLine);
    g.drawRoundedRectangle (shell, 30.0f, 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.3f));
    g.drawRoundedRectangle (shell.reduced (2.5f), 28.0f, 1.0f);

    // wordmark
    g.setColour (cpal::ink);
    g.setFont (CeladonLookAndFeel::pixel (26.0f));
    g.drawText ("CELADON", 48, 34, 300, 30, juce::Justification::left);
    g.setColour (cpal::burgundy);
    g.fillRect (250, 38, 10, 10);
    g.setColour (cpal::dim);
    g.setFont (CeladonLookAndFeel::label (9.0f));
    g.drawText ("SOUNDFONT SYNTHESIZER", 48, 70, 300, 12, juce::Justification::left);

    // cartridge slot
    g.setColour (cpal::bezel);
    g.fillRoundedRectangle (700.0f, 26.0f, 320.0f, 42.0f, 6.0f);
    g.setColour (juce::Colour (0xff8f8a83));
    g.fillRoundedRectangle (708.0f, 32.0f, 304.0f, 30.0f, 4.0f);
    g.setColour (juce::Colour (0xff6f6a64));
    for (int i = 0; i < 6; ++i) g.fillRect (716 + i * 10, 36, 4, 22);
    g.setColour (cpal::ink);
    g.setFont (CeladonLookAndFeel::label (11.0f));
    g.drawText (proc.currentCartName().substring (0, 24), 790, 38, 190, 18, juce::Justification::left);
    g.setColour (cpal::burgundy);
    g.drawText ("v", 985, 38, 20, 18, juce::Justification::left);
    g.setColour (cpal::dim);
    g.setFont (CeladonLookAndFeel::label (8.0f));
    g.drawText ("CART · CLICK TO LOAD .SF2", 700, 74, 220, 10, juce::Justification::left);

    // screen bezel
    g.setColour (cpal::bezel);
    g.fillRoundedRectangle (360.0f, 96.0f, 560.0f, 512.0f, 18.0f);
    g.setColour (cpal::burgundy);
    g.fillRect (384, 126, 102, 3);
    g.fillRect (794, 126, 102, 3);
    g.setColour (cpal::indigo);
    g.fillRect (384, 132, 102, 3);
    g.fillRect (794, 132, 102, 3);
    g.setColour (juce::Colour (0xffb9b4ae));
    g.setFont (CeladonLookAndFeel::label (9.0f));
    g.drawText ("SOUNDFONT · 16 VOICE · STEREO SOUND", 490, 126, 300, 12, juce::Justification::centred);

    // MIDI led
    const bool lit = ledCountdown > 0;
    g.setColour (cpal::ledRed.withAlpha (lit ? 1.0f : 0.25f));
    g.fillEllipse (383.0f, 355.0f, 10.0f, 10.0f);
    if (lit) { g.setColour (cpal::ledRed.withAlpha (0.35f)); g.drawEllipse (380.0f, 352.0f, 16.0f, 16.0f, 2.0f); }
    g.setColour (juce::Colour (0xffb9b4ae));
    g.setFont (CeladonLookAndFeel::label (7.0f));
    g.drawText ("MIDI", 372, 372, 32, 10, juce::Justification::centred);

    // section headers
    auto header = [&g] (int x, int y, int w, const char* t)
    {
        g.setColour (cpal::dim);
        g.setFont (CeladonLookAndFeel::label (11.0f));
        g.drawText (t, x, y - 10, 200, 13, juce::Justification::left);
        g.setColour (cpal::shellLine.withAlpha (0.55f));
        g.fillRect (x, y + 5, w, 1);
    };
    header (48, 150, 290, "ENVELOPE");
    header (48, 452, 290, "SOUND");
    header (930, 150, 302, "FILTER");
    header (930, 430, 302, "LFO");
    header (305, 614, 660, "FX CHAIN");

    // LFO sync toggle (drawn; hit-tested in mouseDown)
    {
        const bool sync = proc.apvts.getRawParameterValue ("lfosync")->load() > 0.5f;
        g.setColour (sync ? cpal::burgundy : cpal::track);
        g.fillRoundedRectangle (1168.0f, 416.0f, 64.0f, 20.0f, 10.0f);
        g.setColour (sync ? juce::Colours::white : cpal::ink);
        g.setFont (CeladonLookAndFeel::label (9.0f));
        g.drawText (sync ? "SYNC" : "FREE", 1168, 420, 64, 12, juce::Justification::centred);
    }

    // speaker slashes — six slots, one blind
    for (int i = 0; i < 6; ++i)
    {
        const float x = 1080.0f + (float) i * 26.0f;
        juce::Path slash;
        slash.addRoundedRectangle (x - 5.0f, 618.0f, 10.0f, 82.0f, 5.0f);
        slash.applyTransform (juce::AffineTransform::rotation (juce::degreesToRadians (-28.0f), x, 659.0f));
        g.setColour (i == 0 ? cpal::track : cpal::shellDark);
        g.fillPath (slash);
        g.setColour (cpal::shellLine);
        g.strokePath (slash, juce::PathStrokeType (1.0f));
    }
}

void CeladonEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto pos = e.getEventRelativeTo (this).getPosition();
    if (e.eventComponent == &screen)
    {
        if (e.getPosition().getY() < 112) showPresetMenu();
        return;
    }
    if (cartArea.contains (pos)) { openCartChooser(); return; }
    if (juce::Rectangle<int> (1168, 416, 64, 20).contains (pos))
        if (auto* par = proc.apvts.getParameter ("lfosync"))
            par->setValueNotifyingHost (par->getValue() > 0.5f ? 0.0f : 1.0f);
}

void CeladonEditor::setAltMode (bool alt)
{
    altMode = alt;
    for (auto& k : knobs) k->setAltMode (alt);
    ab.setAltMode (alt);
}

void CeladonEditor::openCartChooser()
{
    auto start = proc.currentCartPath().isNotEmpty()
                     ? juce::File (proc.currentCartPath()).getParentDirectory()
                     : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Load a SoundFont cart", start, "*.sf2");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (! f.existsAsFile()) return;
            juce::String err;
            if (! proc.loadSoundFontFile (f, err))
                juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                                  .withIconType (juce::MessageBoxIconType::WarningIcon)
                                                  .withTitle ("Celadon")
                                                  .withMessage ("Could not load cart: " + err)
                                                  .withButton ("OK"),
                                              nullptr);
        });
}

void CeladonEditor::showPresetMenu()
{
    auto* f = proc.font();
    if (f == nullptr) { openCartChooser(); return; }
    juce::PopupMenu m;
    m.setLookAndFeel (&lnf);
    for (int i = 0; i < (int) f->presets.size(); ++i)
        m.addItem (i + 1,
                   juce::String::formatted ("%03d  ", f->presets[(size_t) i].program)
                       + juce::String (f->presets[(size_t) i].name),
                   true, i == proc.presetIndex());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&screen),
                     [this] (int r) { if (r > 0) proc.selectPreset (r - 1); });
}

void CeladonEditor::timerCallback()
{
    // MIDI LED
    int act = proc.midiActivity.load();
    if (act > 0) { proc.midiActivity.store (act - 1); ledCountdown = 6; }
    if (ledCountdown > 0) { --ledCountdown; repaint (368, 348, 40, 40); }

    // cart / preset changes
    const int fv = proc.fontVersion.load();
    if (fv != lastFontVersion) { lastFontVersion = fv; repaint(); }

    // live readouts + screen
    for (auto& k : knobs) k->refreshReadout();
    screen.repaint();

    // reflect host-driven lfosync changes on the pill
    const bool sync = proc.apvts.getRawParameterValue ("lfosync")->load() > 0.5f;
    if (sync != lastSyncShown) { lastSyncShown = sync; repaint (1160, 410, 80, 32); }
}
