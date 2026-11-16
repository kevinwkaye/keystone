#include "PluginEditor.h"
#include "BinaryData.h"

using namespace juce;

// =====================================================================================
// Palette: Flip Fella, recoloured brown and orange where Flip Fella is black and blue
// =====================================================================================
namespace kc
{
const Colour bg      { 0xff24180f };
const Colour header  { 0xff1a110b };
const Colour panel   { 0xff33231a };
const Colour card    { 0xff3f2b1f };
const Colour ink     { 0xfff6ebdd };
const Colour dim     { 0xffc9ae92 };
const Colour line    { 0xff8a6a50 };
const Colour dashed  { 0xff6e5240 };
const Colour orange  { 0xfff28c28 };
const Colour orangeDk{ 0xffb8661e };
const Colour dark    { 0xff1a110b };
const Colour ring    { 0xff4a3426 };
const Colour flip    { 0xff141110 };
const Colour flipHi  { 0xff262120 };
const Colour flipLo  { 0xff0e0c0b };
const Colour hat     { 0xff6b4429 };
const Colour brim    { 0xff4a2e1b };
const Colour melted  { 0xff8a3f0a };

Font font (float height, float kerning = 0.04f)
{
    static Typeface::Ptr typeface = Typeface::createSystemTypefaceFor (BinaryData::RobotoCondensedBold_ttf,
                                                                       BinaryData::RobotoCondensedBold_ttfSize);
    return Font (FontOptions (typeface).withHeight (height).withKerningFactor (kerning));
}

void text (Graphics& g, const String& t, Rectangle<float> r, float size, Colour c = ink,
           Justification j = Justification::centredLeft, float kerning = 0.04f)
{
    g.setColour (c);
    g.setFont (font (size, kerning));
    g.drawText (t, r, j, false);
}

void dashedRound (Graphics& g, Rectangle<float> r, float radius, Colour c, float w)
{
    Path p;
    p.addRoundedRectangle (r, radius);
    Path d;
    const float dashes[] = { 6.0f, 5.0f };
    PathStrokeType (w).createDashedStroke (d, p, dashes, 2);
    g.setColour (c);
    g.fillPath (d);
}

double now() { return Time::getMillisecondCounterHiRes() * 0.001; }
} // namespace kc

namespace
{
// ---------------------------------------------------------------------------------------
// A split-flap card that flips like the Karlsson clock when its value changes.
class FlipCard : public Component
{
public:
    void setValue (const String& v)
    {
        if (v == current) return;
        previous = current;
        current = v;
        progress = previous.isEmpty() ? 1.0f : 0.0f;
        repaint();
    }
    void setTextHeight (float h) { textHeight = h; }
    void advance (double dt)
    {
        if (progress < 1.0f) { progress = jmin (1.0f, progress + (float) (dt / 0.24)); repaint(); }
    }
    void paint (Graphics& g) override
    {
        auto card = getLocalBounds().toFloat();
        if (progress >= 1.0f) { half (g, card, current, true, 1.0f, false); half (g, card, current, false, 1.0f, false); }
        else
        {
            half (g, card, current, true, 1.0f, false);
            half (g, card, previous, false, 1.0f, false);
            const float e = 1.0f - std::pow (1.0f - progress, 2.0f);
            if (e < 0.5f) half (g, card, previous, true, 1.0f - e * 2.0f, true);
            else          half (g, card, current, false, (e - 0.5f) * 2.0f, true);
        }
        // the hinge line
        g.setColour (Colours::white.withAlpha (0.22f));
        g.fillRect (card.getX(), card.getCentreY() - 0.5f, card.getWidth(), 1.0f);
        g.setColour (Colours::black);
        g.fillRect (card.getX(), card.getCentreY() + 0.5f, card.getWidth(), 1.0f);
    }

private:
    void half (Graphics& g, Rectangle<float> card, const String& t, bool top, float scale, bool shade)
    {
        if (scale <= 0.001f) return;
        const float hinge = card.getCentreY(), h = card.getHeight() * 0.5f;
        Graphics::ScopedSaveState save (g);
        auto clip = top ? Rectangle<float> (card.getX(), hinge - h * scale, card.getWidth(), h * scale)
                        : Rectangle<float> (card.getX(), hinge, card.getWidth(), h * scale);
        g.reduceClipRegion (clip.getSmallestIntegerContainer());
        g.addTransform (AffineTransform::scale (1.0f, scale, 0.0f, hinge));
        if (top) g.setGradientFill (ColourGradient (kc::flipHi, 0, card.getY(), kc::flip, 0, hinge, false));
        else     g.setGradientFill (ColourGradient (kc::flip, 0, hinge, kc::flipLo, 0, card.getBottom(), false));
        g.fillRoundedRectangle (card, 6.0f);
        g.setColour (Colours::white);
        g.setFont (kc::font (textHeight, 0.02f));
        g.drawFittedText (t, card.reduced (4.0f, 0.0f).toNearestInt(), Justification::centred, 1, 0.6f);
        if (shade) { g.setColour (Colours::black.withAlpha (0.45f * (1.0f - scale))); g.fillRoundedRectangle (card, 6.0f); }
    }
    String current, previous;
    float progress = 1.0f, textHeight = 24.0f;
};

// ---------------------------------------------------------------------------------------
// A flip card bound to a parameter, with its caption underneath. Drag up or down to set it,
// double-click for the default, click a switch or a choice to step it.
class Readout : public Component, public SettableTooltipClient
{
public:
    Readout (KuddleProcessor& p, const String& pid, const String& caption, float cardH = 36.0f)
        : proc (p), id (pid), cap (caption), h (cardH)
    {
        def = kp::find (pid);
        param = p.apvts.getParameter (pid);
        card.setTextHeight (h * 0.56f);
        card.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (card);
        setMouseCursor (def != nullptr && def->kind == kp::Kind::Float ? MouseCursor::UpDownResizeCursor : MouseCursor::PointingHandCursor);
        setTooltip (String (def ? def->name : "") + (def && String (def->unit).isNotEmpty() ? " (" + String (def->unit) + ")" : String()));
        update();
    }
    int preferredWidth() const
    {
        const float w = kc::font (h * 0.56f, 0.02f).getStringWidthFloat (shown) + 14.0f;
        const float cw = kc::font (12.0f, 0.04f).getStringWidthFloat (cap) + 2.0f;
        return (int) std::ceil (jmax (h * 1.05f, w, cw));
    }
    int preferredHeight() const { return (int) h + 20; }
    void update()
    {
        if (def == nullptr) return;
        shown = kp::shortText (*def, proc.value (def->id));
        card.setValue (shown);
    }
    void advance (double dt) { card.advance (dt); }
    void resized() override { card.setBounds (0, 0, getWidth(), (int) h); }
    void paint (Graphics& g) override
    {
        kc::text (g, cap, { 0.0f, h + 3.0f, (float) getWidth() + 30.0f, 15.0f }, 12.0f, kc::dim);
    }
    void mouseDown (const MouseEvent&) override
    {
        if (def == nullptr || param == nullptr) return;
        startValue = proc.value (def->id);
        if (def->kind != kp::Kind::Float)
        {
            float v = startValue + 1.0f;
            if (v > def->max + 0.5f) v = def->min;
            proc.setPlain (def->id, v);
            return;
        }
        param->beginChangeGesture();
    }
    void mouseDrag (const MouseEvent& e) override
    {
        if (def == nullptr || def->kind != kp::Kind::Float || param == nullptr) return;
        const float range = def->max - def->min;
        const float fine = e.mods.isShiftDown() ? 0.15f : 1.0f;
        float v = jlimit (def->min, def->max, startValue - (float) e.getDistanceFromDragStartY() * range / 220.0f * fine);
        if (def->step > 0) v = std::round (v / def->step) * def->step;
        param->setValueNotifyingHost (param->convertTo0to1 (v));
    }
    void mouseUp (const MouseEvent&) override
    {
        if (def != nullptr && def->kind == kp::Kind::Float && param != nullptr) param->endChangeGesture();
    }
    void mouseDoubleClick (const MouseEvent&) override { if (def) proc.resetToDefault (def->id); }
    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w) override
    {
        if (def == nullptr || def->kind != kp::Kind::Float) return;
        const float stepSize = def->step > 0 ? def->step : (def->max - def->min) / 100.0f;
        proc.setPlain (def->id, jlimit (def->min, def->max, proc.value (def->id) + (w.deltaY > 0 ? stepSize : -stepSize)));
    }

private:
    KuddleProcessor& proc;
    String id, cap, shown;
    float h, startValue = 0;
    const kp::Def* def = nullptr;
    RangedAudioParameter* param = nullptr;
    FlipCard card;
};

// ---------------------------------------------------------------------------------------
// A round pill. Lit (orange, dark text) or outlined.
void pill (Graphics& g, Rectangle<float> b, const String& t, bool lit, float size = 14.0f)
{
    const float r = b.getHeight() * 0.5f;
    if (lit) { g.setColour (kc::orange); g.fillRoundedRectangle (b, r); }
    else { g.setColour (kc::line); g.drawRoundedRectangle (b.reduced (0.5f), r, 1.0f); }
    kc::text (g, t, b, size, lit ? kc::dark : kc::dim, Justification::centred);
}

class KButton : public Component, public SettableTooltipClient
{
public:
    enum Style { Filled, Outline, OrangeOutline, Quiet };
    KButton (const String& t, std::function<void()> fn, Style s = Outline, float size = 15.0f)
        : label (t), action (std::move (fn)), style (s), textSize (size) { setMouseCursor (MouseCursor::PointingHandCursor); }
    void setText (const String& t) { if (t != label) { label = t; repaint(); } }
    void setLit (bool l) { if (l != lit) { lit = l; repaint(); } }
    std::function<void()> action;
    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (1.0f);
        const float r = b.getHeight() * 0.5f;
        const bool fill = style == Filled || lit;
        if (fill) { g.setColour (down ? kc::orangeDk : kc::orange); g.fillRoundedRectangle (b, r); }
        else if (style == OrangeOutline) { g.setColour (kc::orange); g.drawRoundedRectangle (b.reduced (1.0f), r, 2.0f); }
        else if (style == Outline) { g.setColour (kc::line); g.drawRoundedRectangle (b.reduced (0.5f), r, 1.0f); }
        if (down && ! fill) { g.setColour (kc::ink.withAlpha (0.08f)); g.fillRoundedRectangle (b, r); }
        kc::text (g, label, b, textSize, fill ? kc::dark : kc::ink, Justification::centred);
    }
    void mouseDown (const MouseEvent&) override { down = true; repaint(); }
    void mouseUp (const MouseEvent& e) override { down = false; repaint(); if (getLocalBounds().contains (e.getPosition()) && action) action(); }

private:
    String label;
    Style style;
    float textSize;
    bool lit = false, down = false;
};

// round icon button with a chevron
class Chevron : public Component, public SettableTooltipClient
{
public:
    Chevron (bool r, std::function<void()> fn) : right (r), action (std::move (fn)) { setMouseCursor (MouseCursor::PointingHandCursor); }
    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (kc::dim);
        g.drawEllipse (b.reduced (0.5f), 1.0f);
        Path p;
        const float cx = b.getCentreX(), cy = b.getCentreY(), s = 5.0f;
        if (right) { p.startNewSubPath (cx - s * 0.6f, cy - s * 1.2f); p.lineTo (cx + s * 0.6f, cy); p.lineTo (cx - s * 0.6f, cy + s * 1.2f); }
        else { p.startNewSubPath (cx + s * 0.6f, cy - s * 1.2f); p.lineTo (cx - s * 0.6f, cy); p.lineTo (cx + s * 0.6f, cy + s * 1.2f); }
        g.setColour (kc::ink);
        g.strokePath (p, PathStrokeType (2.5f, PathStrokeType::curved, PathStrokeType::rounded));
    }
    void mouseUp (const MouseEvent& e) override { if (getLocalBounds().contains (e.getPosition()) && action) action(); }

private:
    bool right;
    std::function<void()> action;
};

// ---------------------------------------------------------------------------------------
// One effect's card: name, On/Off switch, a line about it, and its flip cards.
class ModuleCard : public Component
{
public:
    ModuleCard (KuddleProcessor& p, const String& title, const String& about, const char* onId,
                std::vector<std::pair<const char*, const char*>> reads, float cardH = 36.0f)
        : proc (p), name (title), note (about), onParam (onId)
    {
        for (auto& r : reads) { readouts.push_back (std::make_unique<Readout> (p, r.first, r.second, cardH)); addAndMakeVisible (*readouts.back()); }
        if (onId != nullptr) setMouseCursor (MouseCursor::NormalCursor);
    }
    bool isOn() const { return onParam == nullptr || proc.value (onParam) > 0.5f; }
    Rectangle<float> pillArea() const { return { (float) getWidth() - 14.0f - 58.0f, 14.0f, 58.0f, 30.0f }; }
    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (1.0f);
        if (isOn()) { g.setColour (kc::card); g.fillRoundedRectangle (b, 16.0f); g.setColour (kc::orange); g.drawRoundedRectangle (b, 16.0f, 2.0f); }
        else { g.setColour (kc::bg); g.fillRoundedRectangle (b, 16.0f); kc::dashedRound (g, b, 16.0f, kc::dashed, 2.0f); }
        kc::text (g, name, { 15.0f, 14.0f, (float) getWidth() - 90.0f, 30.0f }, 20.0f);
        g.setColour (kc::dim);
        g.setFont (kc::font (13.0f, 0.04f));
        g.drawFittedText (note, Rectangle<int> (15, 46, getWidth() - 30, 34), Justification::topLeft, 2, 1.0f);
        if (onParam != nullptr) pill (g, pillArea(), isOn() ? "On" : "Off", isOn(), 14.0f);
    }
    void resized() override
    {
        // flip cards flow left to right from the bottom, wrapping onto a second line
        const int gap = 8, maxW = getWidth() - 30;
        std::vector<std::vector<Readout*>> lines (1);
        int x = 0;
        for (auto& r : readouts)
        {
            const int w = r->preferredWidth();
            if (x > 0 && x + w > maxW) { lines.emplace_back(); x = 0; }
            lines.back().push_back (r.get());
            x += w + gap;
        }
        const int lh = readouts.empty() ? 0 : readouts[0]->preferredHeight() + 6;
        int y = getHeight() - 14 - lh * (int) lines.size() + 6;
        for (auto& l : lines)
        {
            int xx = 15;
            for (auto* r : l) { r->setBounds (xx, y, r->preferredWidth(), r->preferredHeight()); xx += r->preferredWidth() + gap; }
            y += lh;
        }
    }
    void mouseUp (const MouseEvent& e) override
    {
        if (onParam != nullptr && pillArea().expanded (6.0f).contains (e.position))
            proc.setPlain (onParam, isOn() ? 0.0f : 1.0f);
    }
    void tick (double dt)
    {
        bool relayout = false;
        for (auto& r : readouts) { const int w = r->preferredWidth(); r->update(); r->advance (dt); relayout |= w != r->preferredWidth(); }
        if (relayout) resized();
        const bool o = isOn();
        if (o != lastOn) { lastOn = o; repaint(); }
    }

private:
    KuddleProcessor& proc;
    String name, note;
    const char* onParam;
    bool lastOn = true;
    std::vector<std::unique_ptr<Readout>> readouts;
};

// ---------------------------------------------------------------------------------------
// The Melt dial: the hero control, an analogue dial with an orange arc and needle.
class MeltDial : public Component
{
public:
    explicit MeltDial (KuddleProcessor& p) : proc (p) { setMouseCursor (MouseCursor::UpDownResizeCursor); }
    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float size = jmin (b.getWidth(), b.getHeight());
        const auto c = b.getCentre();
        const float r = size * 0.4f;
        const float a0 = -MathConstants<float>::pi * 0.75f, a1 = MathConstants<float>::pi * 0.75f;
        const float v = jlimit (0.0f, 1.0f, proc.value (kp::id::melt) * 0.01f);
        Path track; track.addCentredArc (c.x, c.y, r, r, 0, a0, a1, true);
        g.setColour (kc::ring);
        g.strokePath (track, PathStrokeType (size * 0.056f, PathStrokeType::curved, PathStrokeType::rounded));
        if (v > 0.002f)
        {
            Path arc; arc.addCentredArc (c.x, c.y, r, r, 0, a0, a0 + (a1 - a0) * v, true);
            g.setColour (kc::orange);
            g.strokePath (arc, PathStrokeType (size * 0.056f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        // ticks
        g.setColour (kc::dim);
        for (int i = 0; i <= 10; ++i)
        {
            const float a = a0 + (a1 - a0) * (float) i / 10.0f;
            const Point<float> d (std::sin (a), -std::cos (a));
            g.drawLine (Line<float> (c + d * (r + size * 0.056f), c + d * (r + size * 0.088f)), 2.0f);
        }
        // the face and needle
        const float fr = size * 0.3f;
        g.setColour (kc::flip);
        g.fillEllipse (c.x - fr, c.y - fr, fr * 2, fr * 2);
        const float a = a0 + (a1 - a0) * v;
        Path needle;
        needle.addRoundedRectangle (-size * 0.0125f, -fr * 0.92f, size * 0.025f, fr * 0.42f, size * 0.0125f);
        g.setColour (kc::orange);
        g.fillPath (needle, AffineTransform::rotation (a).translated (c));
        g.fillEllipse (c.x - size * 0.0375f, c.y - size * 0.0375f, size * 0.075f, size * 0.075f);
    }
    void mouseDown (const MouseEvent&) override { start = proc.value (kp::id::melt); if (auto* p = proc.apvts.getParameter (kp::id::melt)) p->beginChangeGesture(); }
    void mouseDrag (const MouseEvent& e) override
    {
        if (auto* p = proc.apvts.getParameter (kp::id::melt))
            p->setValueNotifyingHost (p->convertTo0to1 (jlimit (0.0f, 100.0f, start - (float) e.getDistanceFromDragStartY() * 100.0f / 240.0f)));
    }
    void mouseUp (const MouseEvent&) override { if (auto* p = proc.apvts.getParameter (kp::id::melt)) p->endChangeGesture(); }
    void mouseDoubleClick (const MouseEvent&) override { proc.setPlain (kp::id::melt, 0.0f); }

private:
    KuddleProcessor& proc;
    float start = 0;
};

// A tick box drawn the Flip Fella way, bound to a switch.
class Tick : public Component
{
public:
    Tick (KuddleProcessor& p, const char* pid, const String& t) : proc (p), id (pid), label (t) { setMouseCursor (MouseCursor::PointingHandCursor); }
    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (kc::card);
        g.fillRoundedRectangle (b, 12.0f);
        auto box = Rectangle<float> (12.0f, b.getCentreY() - 10.0f, 20.0f, 20.0f);
        const bool on = proc.value (id) > 0.5f;
        if (on) { g.setColour (kc::orange); g.fillRoundedRectangle (box, 4.0f); Path t; t.startNewSubPath (box.getX() + 4, box.getCentreY()); t.lineTo (box.getX() + 8.5f, box.getBottom() - 5); t.lineTo (box.getRight() - 4, box.getY() + 5); g.setColour (kc::dark); g.strokePath (t, PathStrokeType (2.5f)); }
        else { g.setColour (kc::line); g.drawRoundedRectangle (box, 4.0f, 1.5f); }
        kc::text (g, label, b.withTrimmedLeft (44.0f), 15.0f);
    }
    void mouseUp (const MouseEvent& e) override { if (getLocalBounds().contains (e.getPosition())) proc.setPlain (id, proc.value (id) > 0.5f ? 0.0f : 1.0f); }
    void tick() { const bool o = proc.value (id) > 0.5f; if (o != last) { last = o; repaint(); } }

private:
    KuddleProcessor& proc;
    const char* id;
    String label;
    bool last = false;
};

// ---------------------------------------------------------------------------------------
// Footswitch tiles along the bottom: they work like the footswitches and light when pressed.
class FootTile : public Component
{
public:
    FootTile (KuddleProcessor& p, int s, String tap, String hold) : proc (p), sw (s), tapJob (std::move (tap)), holdJob (std::move (hold))
    {
        letter.setTextHeight (26.0f);
        letter.setValue (String::charToString ((juce_wchar) ('A' + s)));
        letter.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (letter);
        setMouseCursor (MouseCursor::PointingHandCursor);
    }
    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (kc::panel);
        g.fillRoundedRectangle (b, 14.0f);
        if (lit) { g.setColour (kc::orange); g.drawRoundedRectangle (b.reduced (1.0f), 14.0f, 2.0f); }
        kc::text (g, "Tap: " + tapJob, { 60.0f, 9.0f, b.getWidth() - 64.0f, 22.0f }, 16.0f);
        kc::text (g, "Hold: " + holdJob, { 60.0f, 31.0f, b.getWidth() - 64.0f, 20.0f }, 14.0f, kc::dim);
    }
    void resized() override { letter.setBounds (12, (getHeight() - 46) / 2, 36, 46); }
    void mouseDown (const MouseEvent&) override { proc.footDown (sw); }
    void mouseUp (const MouseEvent&) override { proc.footUp (sw); }
    void tick (double dt)
    {
        letter.advance (dt);
        const bool l = (proc.uiFootDown.load() >> sw) & 1;
        if (l != lit) { lit = l; repaint(); }
    }

private:
    KuddleProcessor& proc;
    int sw;
    String tapJob, holdJob;
    FlipCard letter;
    bool lit = false;
};

// ---------------------------------------------------------------------------------------
// Pebble from Flip Fella, Kuddle's version: orange, in shades and a pork pie hat with a
// cream band and an orange feather. Bobs to the music, hops on a footswitch, nods on Keep,
// and leans in while you squeeze.
class Pebble : public Component
{
public:
    explicit Pebble (KuddleProcessor& p) : proc (p) {}
    void update (double t, double dt)
    {
        now = t;
        level += (proc.inputLevel() * 4.0f - level) * (float) jmin (1.0, dt * 8.0);
        if (level > 0.02f) lastSound = t;
        hop = jmax (0.0f, hop - (float) dt * 4.0f);
        nod = jmax (0.0f, nod - (float) dt * 2.0f);
        const int fp = proc.uiFootPulse.load();
        if (fp != lastFoot) { lastFoot = fp; hop = 1.0f; }
        const int kp = proc.uiKeepPulse.load();
        if (kp != lastKeep) { lastKeep = kp; nod = 1.0f; }
        repaint();
    }
    void paint (Graphics& g) override
    {
        auto a = getLocalBounds().toFloat();
        const float s = a.getWidth() / 150.0f;
        const bool chilled = now - lastSound > 20.0;
        const float bob = chilled ? 0.0f : (1.5f + 3.0f * jmin (1.0f, level)) * (float) std::sin (now * 2.4);
        const float dy = bob - hop * 10.0f + nod * 6.0f * (float) std::sin (nod * 12.0f);
        const float lean = proc.squeezing() ? 6.0f : 0.0f;
        const auto tf = AffineTransform::translation (-100.0f, -100.0f).scaled (s).rotated (degreesToRadians (lean), 0.0f, 50.0f * s)
                            .translated (a.getCentreX(), a.getCentreY() + 14.0f * s);
        auto ell = [&] (float cx, float cy, float rx, float ry, Colour col) { Path p; p.addEllipse (cx - rx, cy - ry, rx * 2, ry * 2); g.setColour (col); g.fillPath (p, tf); };
        auto stroke = [&] (Path& p, float w, Colour col) { g.setColour (col); g.strokePath (p, PathStrokeType (w, PathStrokeType::curved, PathStrokeType::rounded), tf); };

        ell (107, 109 + dy, 58, 48, kc::orangeDk);
        ell (100, 100 + dy, 58, 48, kc::orange);

        // shades
        for (float ex : { 79.0f, 121.0f })
        {
            Path lens; lens.addRoundedRectangle (ex - 17, 84 + dy, 34, 20, 8);
            g.setColour (kc::flip); g.fillPath (lens, tf);
            Path glint; glint.startNewSubPath (ex - 10, 90 + dy); glint.lineTo (ex - 3, 88 + dy);
            stroke (glint, 3.0f, Colours::white);
        }
        Path bridge; bridge.startNewSubPath (96, 91 + dy); bridge.lineTo (104, 91 + dy); stroke (bridge, 4.0f, kc::flip);
        Path arms; arms.startNewSubPath (62, 91 + dy); arms.lineTo (48, 86 + dy); arms.startNewSubPath (138, 91 + dy); arms.lineTo (152, 86 + dy);
        stroke (arms, 4.0f, kc::flip);

        // mouth: a cool smile, opening a little with the music
        if (level > 0.08f && ! chilled) ell (100, 124 + dy, 7.0f + 3.0f * level, 3.0f + 6.0f * jmin (1.0f, level), kc::flip);
        else { Path m; m.startNewSubPath (88, 121 + dy); m.quadraticTo (100, 131 + dy, 113, 119 + dy); stroke (m, 5.0f, kc::flip); }

        // pork pie hat, tipped
        const auto hatTf = AffineTransform::rotation (degreesToRadians (-9.0f - 6.0f * hop), 100.0f, 52.0f + dy).followedBy (tf);
        auto hatFill = [&] (Path& p, Colour c) { g.setColour (c); g.fillPath (p, hatTf); };
        Path brim; brim.addEllipse (100 - 52, 52 + dy - 9, 104, 18); hatFill (brim, kc::brim);
        Path crown; crown.startNewSubPath (70, 52 + dy); crown.lineTo (74, 24 + dy); crown.quadraticTo (100, 17 + dy, 126, 24 + dy); crown.lineTo (130, 52 + dy); crown.closeSubPath(); hatFill (crown, kc::hat);
        Path band; band.startNewSubPath (71.5f, 40 + dy); band.quadraticTo (100, 35 + dy, 128.5f, 40 + dy); band.lineTo (129.5f, 50 + dy); band.quadraticTo (100, 46 + dy, 70.5f, 50 + dy); band.closeSubPath(); hatFill (band, kc::ink);
        Path feather; feather.startNewSubPath (120, 40 + dy); feather.cubicTo (132, 32 + dy, 138, 20 + dy, 133, 8 + dy);
        g.setColour (kc::orange); g.strokePath (feather, PathStrokeType (5.0f, PathStrokeType::curved, PathStrokeType::rounded), hatTf);
    }

private:
    KuddleProcessor& proc;
    double now = 0, lastSound = 0;
    float level = 0, hop = 0, nod = 0;
    int lastFoot = 0, lastKeep = 0;
};

// ---------------------------------------------------------------------------------------
// A Melt reach row on the Deep page: name, flip card and a bar you drag.
class ReachBar : public Component
{
public:
    ReachBar (KuddleProcessor& p, const char* pid, const String& t) : proc (p), id (pid), label (t)
    {
        card.setTextHeight (18.0f);
        card.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (card);
        setMouseCursor (MouseCursor::LeftRightResizeCursor);
    }
    void resized() override { card.setBounds (getWidth() - 44, 0, 44, 32); }
    void paint (Graphics& g) override
    {
        kc::text (g, label, { 0.0f, 0.0f, (float) getWidth() - 50.0f, 32.0f }, 15.0f);
        auto bar = Rectangle<float> (0.0f, 42.0f, (float) getWidth(), 8.0f);
        g.setColour (kc::ring); g.fillRoundedRectangle (bar, 4.0f);
        g.setColour (kc::orange); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * proc.value (id) * 0.01f), 4.0f);
    }
    void mouseDown (const MouseEvent& e) override { if (auto* p = proc.apvts.getParameter (id)) p->beginChangeGesture(); mouseDrag (e); }
    void mouseDrag (const MouseEvent& e) override
    {
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (std::round (jlimit (0.0f, 100.0f, e.position.x / (float) getWidth() * 100.0f))));
    }
    void mouseUp (const MouseEvent&) override { if (auto* p = proc.apvts.getParameter (id)) p->endChangeGesture(); }
    void mouseDoubleClick (const MouseEvent&) override { proc.resetToDefault (id); }
    void tick (double dt) { const float v = proc.value (id); card.setValue (String ((int) std::lround (v))); card.advance (dt); if (v != last) { last = v; repaint(); } }

private:
    KuddleProcessor& proc;
    const char* id;
    String label;
    FlipCard card;
    float last = -1;
};

// ---------------------------------------------------------------------------------------
// One footswitch on the Pedal page: its jobs and Learn.
class FootRow : public Component
{
public:
    FootRow (KuddleProcessor& p, int s, String tap, String hold) : proc (p), sw (s), tapJob (std::move (tap)), holdJob (std::move (hold)),
        learn ("Learn", [this] { proc.learnFootswitch (sw); }, KButton::Outline, 15.0f)
    {
        letter.setTextHeight (32.0f);
        letter.setValue (String::charToString ((juce_wchar) ('A' + s)));
        addAndMakeVisible (letter);
        addAndMakeVisible (learn);
    }
    void resized() override
    {
        letter.setBounds (14, (getHeight() - 56) / 2, 44, 56);
        learn.setBounds (getWidth() - 14 - 96, (getHeight() - 44) / 2, 96, 44);
    }
    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (kc::card);
        g.fillRoundedRectangle (b, 14.0f);
        const float x0 = 74.0f, colW = (b.getWidth() - x0 - 130.0f) * 0.5f;
        kc::text (g, "TAP", { x0, 14.0f, colW, 16.0f }, 13.0f, kc::dim, Justification::centredLeft, 0.08f);
        kc::text (g, tapJob, { x0, 32.0f, colW, 24.0f }, 19.0f);
        kc::text (g, "HOLD", { x0 + colW + 12.0f, 14.0f, colW, 16.0f }, 13.0f, kc::dim, Justification::centredLeft, 0.08f);
        g.setColour (kc::ink);
        g.setFont (kc::font (19.0f, 0.04f));
        g.drawFittedText (holdJob, Rectangle<float> (x0 + colW + 12.0f, 32.0f, colW, 24.0f).toNearestInt(), Justification::centredLeft, 1, 0.75f);
        const auto& s = proc.pedals();
        const String sends = proc.learningWhat() == sw ? "Press it now" : "CC " + String (s.fsCC[(size_t) sw]) + ", channel " + String (s.fsChannel[(size_t) sw]);
        kc::text (g, sends, { x0, 56.0f, b.getWidth() - x0 - 130.0f, 16.0f }, 12.0f, proc.learningWhat() == sw ? kc::orange : kc::dim);
    }
    void tick (double dt) { letter.advance (dt); learn.setLit (proc.learningWhat() == sw); repaint(); }

private:
    KuddleProcessor& proc;
    int sw;
    String tapJob, holdJob;
    FlipCard letter;
    KButton learn;
};

// The Melt sweep's three zones with the pedal's live position.
class ZoneBar : public Component
{
public:
    explicit ZoneBar (KuddleProcessor& p) : proc (p) {}
    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().withHeight (48.0f);
        Path clip; clip.addRoundedRectangle (b, 12.0f);
        Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (clip);
        const float w = b.getWidth() / 3.0f;
        const Colour fills[] = { kc::ink, kc::orange, kc::melted };
        const Colour inks[] = { kc::dark, kc::dark, kc::ink };
        const char* names[] = { "Open", "Warm", "Melted" };
        const int target = (int) std::lround (proc.value (kp::id::expTarget));
        const char* other[] = { "", "Wah heel", "Silent", "Dry" };
        const char* otherTo[] = { "", "Wah toe", "Full", "Wet" };
        for (int i = 0; i < 3; ++i)
        {
            g.setColour (fills[i]);
            g.fillRect (b.getX() + w * (float) i, b.getY(), w, b.getHeight());
            String t = names[i];
            if (target != 0) t = i == 0 ? other[target] : (i == 2 ? otherTo[target] : "");
            kc::text (g, t, { b.getX() + w * (float) i, b.getY(), w, b.getHeight() }, 15.0f, inks[i], Justification::centred);
        }
        // the pedal, now
        const float x = b.getX() + b.getWidth() * proc.expressionLevel();
        g.setColour (kc::flip);
        g.fillRect (x - 2.0f, b.getY(), 4.0f, b.getHeight());
    }

private:
    KuddleProcessor& proc;
};

// The tuner, over the main page while footswitch C is held.
class TunerView : public Component
{
public:
    explicit TunerView (KuddleProcessor& p) : proc (p)
    {
        note.setTextHeight (80.0f);
        addAndMakeVisible (note);
    }
    void resized() override { note.setBounds (getWidth() / 2 - 70, 70, 140, 140); }
    void paint (Graphics& g) override
    {
        g.fillAll (kc::bg);
        kc::text (g, "TUNER", { 0.0f, 20.0f, (float) getWidth(), 30.0f }, 22.0f, kc::dim, Justification::centred, 0.14f);
        const float hz = proc.tunerHz();
        const float ref = proc.value (kp::id::tunerRef);
        auto bar = Rectangle<float> (getWidth() * 0.5f - 300.0f, 250.0f, 600.0f, 14.0f);
        g.setColour (kc::ring); g.fillRoundedRectangle (bar, 7.0f);
        g.setColour (kc::dim); g.fillRect (bar.getCentreX() - 1.0f, bar.getY() - 10.0f, 2.0f, bar.getHeight() + 20.0f);
        if (hz > 0.0f)
        {
            const float cents = cs;
            const bool inTune = std::abs (cents) < 3.0f;
            const float x = bar.getCentreX() + jlimit (-50.0f, 50.0f, cents) / 50.0f * bar.getWidth() * 0.5f;
            g.setColour (inTune ? kc::orange : kc::ink);
            g.fillRoundedRectangle (x - 6.0f, bar.getY() - 14.0f, 12.0f, bar.getHeight() + 28.0f, 6.0f);
            kc::text (g, (cents >= 0 ? "+" : "") + String (cents, 0) + " cents   " + String (hz, 1) + " Hz", { 0.0f, 290.0f, (float) getWidth(), 24.0f }, 18.0f, kc::dim, Justification::centred);
        }
        else kc::text (g, "Play a note", { 0.0f, 290.0f, (float) getWidth(), 24.0f }, 18.0f, kc::dim, Justification::centred);
        kc::text (g, "Tap any footswitch to leave. A " + String ((int) std::lround (ref)) + " Hz", { 0.0f, (float) getHeight() - 50.0f, (float) getWidth(), 24.0f }, 15.0f, kc::dim, Justification::centred);
    }
    void tick (double dt)
    {
        const float hz = proc.tunerHz();
        if (hz > 0.0f)
        {
            static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            const float midi = 69.0f + 12.0f * std::log2 (hz / proc.value (kp::id::tunerRef));
            const int nearest = (int) std::lround (midi);
            cs = (midi - (float) nearest) * 100.0f;
            note.setValue (names[((nearest % 12) + 12) % 12]);
        }
        else note.setValue ("-");
        note.advance (dt);
        repaint();
    }

private:
    KuddleProcessor& proc;
    FlipCard note;
    float cs = 0;
};

enum class Page { Main, Deep, Pedal };
} // namespace

// =====================================================================================
// The editor
// =====================================================================================
struct KuddleEditor::Impl : public Component
{
    KuddleProcessor& proc;
    Page page = Page::Main;
    double lastTick = 0;
    TooltipWindow tips { this, 600 };

    // header
    Chevron prev { false, [this] { proc.stepPreset (-1); } }, next { true, [this] { proc.stepPreset (1); } };
    FlipCard presetTens, presetOnes;
    KButton studio { "Studio", [this] { proc.setKeeperLive (false); }, KButton::Quiet, 16.0f };
    KButton live { "Live", [this] { proc.setKeeperLive (true); }, KButton::Quiet, 16.0f };
    KButton keep { "Keep", [this] { proc.keepNow(); }, KButton::OrangeOutline, 16.0f };
    KButton back { "Back", [this] { setPage (Page::Main); }, KButton::Outline, 15.0f };
    Pebble pebble { proc };
    Rectangle<float> nameArea;

    // main
    MeltDial dial { proc };
    FlipCard meltTens, meltOnes;
    Tick meltWah { proc, kp::id::meltWah, "Pedal works the wah when it's in manual" };
    std::vector<std::unique_ptr<ModuleCard>> mainCards, deepCards;
    std::vector<std::unique_ptr<FootTile>> tiles;
    KButton deepButton { "Deep", [this] { setPage (Page::Deep); }, KButton::OrangeOutline, 15.0f };
    KButton pedalButton { "Pedal page", [this] { setPage (Page::Pedal); }, KButton::Filled, 15.0f };
    TunerView tuner { proc };

    // deep
    KButton revert { "Reset this preset", [this] { proc.revertPreset(); }, KButton::Outline, 15.0f };
    std::vector<std::unique_ptr<ReachBar>> reach;

    // pedal
    std::vector<std::unique_ptr<FootRow>> footRows;
    std::vector<std::unique_ptr<KButton>> targets;
    ZoneBar zones { proc };
    KButton heel { "Heel", [this] { proc.setHeel(); }, KButton::Filled, 15.0f };
    KButton toe { "Toe", [this] { proc.setToe(); }, KButton::Filled, 15.0f };
    KButton reverse { "Reverse", [this] { proc.toggleReverse(); }, KButton::Outline, 15.0f };
    KButton learnExp { "Learn pedal", [this] { proc.learnExpression(); }, KButton::Outline, 14.0f };
    KButton device { "Footswitch input", [this] { deviceMenu(); }, KButton::Outline, 14.0f };
    KButton learnKeep { "Learn Keeper", [this] { proc.learnKeeper(); }, KButton::Outline, 14.0f };
    KButton defaults { "Defaults", [this] { proc.resetFootswitches(); }, KButton::Outline, 14.0f };
    KButton exportK { "Export Keepers", [this] { proc.exportKeepers(); }, KButton::Outline, 14.0f };

    explicit Impl (KuddleProcessor& p) : proc (p)
    {
        for (auto* c : { &presetTens, &presetOnes }) { c->setTextHeight (32.0f); addAndMakeVisible (*c); }
        for (auto* c : { &meltTens, &meltOnes }) { c->setTextHeight (40.0f); addAndMakeVisible (*c); }
        for (auto* c : { (Component*) &prev, (Component*) &next, (Component*) &studio, (Component*) &live, (Component*) &keep, (Component*) &back, (Component*) &pebble,
                         (Component*) &dial, (Component*) &meltWah, (Component*) &deepButton, (Component*) &pedalButton, (Component*) &revert,
                         (Component*) &zones, (Component*) &heel, (Component*) &toe, (Component*) &reverse, (Component*) &learnExp, (Component*) &device,
                         (Component*) &learnKeep, (Component*) &defaults, (Component*) &exportK })
            addAndMakeVisible (c);
        prev.setTooltip ("Previous preset (hold footswitch A)");
        next.setTooltip ("Next preset (hold footswitch B)");

        using namespace kp::id;
        auto addMain = [&] (const char* n, const char* note, const char* on, std::vector<std::pair<const char*, const char*>> r)
        { mainCards.push_back (std::make_unique<ModuleCard> (proc, n, note, on, r)); addAndMakeVisible (*mainCards.back()); };
        addMain ("Mute", "Flatwound and foam feel", muteOn, { { foam, "Foam" }, { thump, "Thump" } });
        addMain ("Auto wah", "Dark voice, follows your touch", wahOn, { { wahSense, "Sense" }, { wahMode, "Mode" } });
        addMain ("Valve DI", "Always on, the heart of it", nullptr, { { diDrive, "Drive" }, { diAir, "Air" } });
        addMain ("Opto", "Slow, smooth levelling", optoOn, { { optoSqueeze, "Squeeze" }, { optoSpeed, "Speed" } });
        addMain ("Tape", "Push for saturation", tapeOn, { { tapePush, "Push" }, { tapeWow, "Wow" } });
        addMain ("Tone", "Bass, low mids, treble roll-off in kHz", toneOn, { { bass, "Bass" }, { mids, "Mids" }, { treble, "Treble" } });
        addMain ("Room", "Small dark room or spring", roomOn, { { roomType, "Type" }, { roomMix, "Mix" } });
        addMain ("Output", "Level after the chain", nullptr, { { output, "dB" }, { inGain, "Gain in" } });

        auto addDeep = [&] (const char* n, const char* note, const char* on, std::vector<std::pair<const char*, const char*>> r)
        { deepCards.push_back (std::make_unique<ModuleCard> (proc, n, note, on, r)); addChildComponent (*deepCards.back()); };
        addDeep ("Input", "Gain in, gate and the tuner note", nullptr, { { inGain, "Gain dB" }, { gate, "Gate dB" }, { tunerRef, "Tuner Hz" } });
        addDeep ("Mute", "Foam, thump and string age", muteOn, { { foam, "Foam" }, { thump, "Thump" }, { strings, "Strings" } });
        addDeep ("Auto wah", "How it follows your touch", wahOn, { { wahSense, "Sense" }, { wahRange, "Range" }, { wahSpeed, "Speed" }, { wahPeak, "Peak" }, { wahMode, "Mode" }, { wahPos, "Manual" } });
        addDeep ("Valve DI", "Drive, air and how it sags", nullptr, { { diDrive, "Drive" }, { diAir, "Air" }, { diSag, "Sag" }, { diLowCut, "Low cut" } });
        addDeep ("Opto", "Blend mixes in the dry signal", optoOn, { { optoSqueeze, "Squeeze" }, { optoSpeed, "Speed" }, { optoBlend, "Blend" }, { optoHold, "Hold D dB" } });
        addDeep ("Tape", "Push, movement and head bump", tapeOn, { { tapePush, "Push" }, { tapeWow, "Wow" }, { tapeFlutter, "Flutter" }, { tapeBump, "Bump" }, { tapeHiss, "Hiss" } });
        addDeep ("Tone", "Where bass and mids sit, in Hz", toneOn, { { bass, "Bass" }, { bassHz, "Bass Hz" }, { mids, "Mids" }, { midsHz, "Mids Hz" }, { treble, "Treble" } });
        addDeep ("Room", "Small dark room or spring", roomOn, { { roomType, "Type" }, { roomSize, "Size" }, { roomDecay, "Decay s" }, { roomPre, "Pre ms" }, { roomMix, "Mix" } });

        for (auto r : { std::pair<const char*, const char*> { reachMute, "Mute" }, { reachTape, "Tape push" }, { reachTreble, "Treble roll-off" }, { reachRoom, "Room" } })
        { reach.push_back (std::make_unique<ReachBar> (proc, r.first, r.second)); addChildComponent (*reach.back()); }

        const char* taps[] = { "Mute", "Auto wah", "Room", "Tape push" };
        const char* holds[] = { "Previous preset", "Next preset", "Tuner", "Squeeze" };
        for (int i = 0; i < 4; ++i)
        {
            tiles.push_back (std::make_unique<FootTile> (proc, i, taps[i], holds[i])); addAndMakeVisible (*tiles.back());
            footRows.push_back (std::make_unique<FootRow> (proc, i, taps[i], i == 3 ? "Squeeze (firmer compression)" : holds[i])); addChildComponent (*footRows.back());
        }
        const char* targetNames[] = { "Melt sweep", "Wah", "Volume", "Room" };
        for (int i = 0; i < 4; ++i)
        {
            targets.push_back (std::make_unique<KButton> (targetNames[i], [this, i] { proc.setPlain (kp::id::expTarget, (float) i); }, KButton::Quiet, 15.0f));
            addChildComponent (*targets.back());
        }
        addChildComponent (tuner);
        setPage (Page::Main);
    }

    void setPage (Page p)
    {
        page = p;
        const bool m = p == Page::Main, d = p == Page::Deep, pd = p == Page::Pedal;
        for (auto* c : { (Component*) &dial, (Component*) &meltTens, (Component*) &meltOnes, (Component*) &meltWah, (Component*) &deepButton, (Component*) &pedalButton })
            c->setVisible (m);
        for (auto& c : mainCards) c->setVisible (m);
        for (auto& c : tiles) c->setVisible (m);
        for (auto& c : deepCards) c->setVisible (d);
        for (auto& r : reach) r->setVisible (d);
        revert.setVisible (d);
        for (auto& r : footRows) r->setVisible (pd);
        for (auto& t : targets) t->setVisible (pd);
        for (auto* c : { (Component*) &zones, (Component*) &heel, (Component*) &toe, (Component*) &reverse, (Component*) &learnExp, (Component*) &device,
                         (Component*) &learnKeep, (Component*) &defaults, (Component*) &exportK })
            c->setVisible (pd);
        // the header follows the approved pages: presets, Studio/Live and Keep on Main only
        for (auto* c : { (Component*) &prev, (Component*) &next, (Component*) &presetTens, (Component*) &presetOnes,
                         (Component*) &studio, (Component*) &live, (Component*) &keep }) c->setVisible (m);
        back.setVisible (! m);
        resized();
        repaint();
    }

    void presetMenu()
    {
        PopupMenu menu;
        for (int i = 0; i < KuddleProcessor::kPresets; ++i)
            if (proc.hasPreset (i)) menu.addItem (1 + i, String (i + 1).paddedLeft ('0', 2) + "  " + proc.presetName (i), true, i == proc.selectedPreset());
        menu.addSeparator();
        PopupMenu save;
        for (int i = 0; i < KuddleProcessor::kPresets; ++i)
            save.addItem (100 + i, String (i + 1).paddedLeft ('0', 2) + "  " + (proc.hasPreset (i) ? proc.presetName (i) : String ("Empty")));
        if (proc.selectedPreset() >= 0) menu.addItem (200, "Save over " + proc.presetName (proc.selectedPreset()));
        menu.addSubMenu ("Save in", save);
        if (proc.selectedPreset() >= 0)
        {
            menu.addItem (201, "Rename " + proc.presetName (proc.selectedPreset()));
            menu.addItem (202, "Clear " + proc.presetName (proc.selectedPreset()));
        }
        menu.showMenuAsync (PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (nameArea.toNearestInt())),
            [this] (int r)
            {
                if (r >= 1 && r <= KuddleProcessor::kPresets) proc.loadPreset (r - 1);
                else if (r >= 100 && r < 100 + KuddleProcessor::kPresets) askName (r - 100, proc.hasPreset (r - 100) ? proc.presetName (r - 100) : String(), false);
                else if (r == 200) proc.savePreset (proc.selectedPreset(), proc.presetName (proc.selectedPreset()));
                else if (r == 201) askName (proc.selectedPreset(), proc.presetName (proc.selectedPreset()), true);
                else if (r == 202) proc.clearPreset (proc.selectedPreset());
            });
    }

    void askName (int slot, const String& current, bool rename)
    {
        auto* w = new AlertWindow (rename ? "Rename preset" : "Save preset", "Name it by its vibe", MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", current.isNotEmpty() ? current : "Preset " + String (slot + 1));
        w->addButton ("Save", 1, KeyPress (KeyPress::returnKey));
        w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
        w->enterModalState (true, ModalCallbackFunction::create ([this, w, slot, rename] (int r)
        {
            if (r == 1)
            {
                const auto n = w->getTextEditorContents ("name").trim();
                if (rename) proc.renamePreset (slot, n); else proc.savePreset (slot, n);
            }
        }), true);
    }

    void deviceMenu()
    {
        PopupMenu menu;
        const auto names = DirectMidiInput::deviceNames();
        const auto cur = proc.footDevice();
        menu.addItem (1, "Find the footswitch automatically", true, cur.isEmpty());
        for (int i = 0; i < names.size(); ++i) menu.addItem (10 + i, names[i], true, cur == names[i]);
        menu.addItem (2, "Off: footswitch MIDI comes from Ableton", true, cur == "none");
        menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&device), [this, names] (int r)
        {
            if (r == 1) proc.setFootDevice ({});
            else if (r == 2) proc.setFootDevice ("none");
            else if (r >= 10) proc.setFootDevice (names[r - 10]);
        });
    }

    void mouseUp (const MouseEvent& e) override
    {
        if (page == Page::Main && nameArea.contains (e.position)) presetMenu();
    }

    void paint (Graphics& g) override
    {
        g.fillAll (kc::bg);
        // header
        g.setColour (kc::header);
        g.fillRect (0, 0, getWidth(), 96);
        if (page == Page::Main)
        {
            kc::text (g, "KUDDLE", { 28.0f, 24.0f, 160.0f, 44.0f }, 36.0f, kc::ink, Justification::centredLeft, 0.06f);
            kc::text (g, "Home rig " JucePlugin_VersionString, { 176.0f, 38.0f, 120.0f, 22.0f }, 14.0f, kc::dim);
        }
        else
        {
            kc::text (g, page == Page::Deep ? "DEEP" : "PEDAL", { 128.0f, 24.0f, 140.0f, 44.0f }, 36.0f, kc::ink, Justification::centredLeft, 0.06f);
            if (page == Page::Pedal) kc::text (g, "Footswitches and expression for Kuddle", { 262.0f, 38.0f, 300.0f, 22.0f }, 14.0f, kc::dim);
        }
        if (page == Page::Main)
        {
            const int sel = proc.selectedPreset();
            const String nm = sel >= 0 ? proc.presetName (sel) + (proc.presetEdited() ? "*" : "") : String ("No preset");
            kc::text (g, nm, nameArea, 26.0f, kc::ink);
        }
        // Pebble's line
        if (kc::now() - proc.uiMessageAt < 3.0)
            kc::text (g, proc.uiMessage, { (float) getWidth() - 420.0f, 70.0f, 320.0f, 20.0f }, 13.0f, kc::orange, Justification::centredRight, 0.08f);

        if (page == Page::Main)
        {
            auto meltPanel = Rectangle<float> (28.0f, 116.0f, 380.0f, 560.0f);
            g.setColour (kc::panel); g.fillRoundedRectangle (meltPanel, 20.0f);
            kc::text (g, "MELT", { 52.0f, 132.0f, 200.0f, 24.0f }, 18.0f, kc::ink, Justification::centredLeft, 0.14f);
            kc::text (g, "Expression", { 200.0f, 132.0f, 184.0f, 24.0f }, 14.0f, kc::dim, Justification::centredRight);
            kc::text (g, "Heel: open", { 52.0f, 532.0f, 120.0f, 20.0f }, 15.0f);
            kc::text (g, "Warm", { 168.0f, 532.0f, 100.0f, 20.0f }, 15.0f, kc::ink, Justification::centred);
            kc::text (g, "Toe: melted", { 264.0f, 532.0f, 120.0f, 20.0f }, 15.0f, kc::ink, Justification::centredRight);
            kc::text (g, "Towards the toe you get more mute, more tape and less treble.", { 52.0f, 558.0f, 332.0f, 40.0f }, 14.0f, kc::dim, Justification::centred);
            kc::text (g, "CHAIN", { 432.0f, 116.0f, 200.0f, 24.0f }, 18.0f, kc::ink, Justification::centredLeft, 0.14f);
            kc::text (g, "In playing order. Tap a switch to turn an effect on or off.", { 600.0f, 116.0f, 572.0f, 24.0f }, 14.0f, kc::dim, Justification::centredRight);
            kc::text (g, "Hold C and D: Keep", { 940.0f, 688.0f, 232.0f, 18.0f }, 14.0f, kc::dim, Justification::centredRight);
        }
        else if (page == Page::Deep)
        {
            kc::text (g, "Set once, saved with each preset", { 262.0f, 38.0f, 300.0f, 22.0f }, 14.0f, kc::dim);
            auto strip = Rectangle<float> (28.0f, 576.0f, 1144.0f, 110.0f);
            g.setColour (kc::panel); g.fillRoundedRectangle (strip, 16.0f);
            kc::text (g, "Melt reach", { 46.0f, 594.0f, 200.0f, 26.0f }, 19.0f);
            g.setColour (kc::dim);
            g.setFont (kc::font (13.0f, 0.04f));
            g.drawFittedText ("How far each part moves when the pedal reaches the toe", Rectangle<int> (46, 622, 200, 40), Justification::topLeft, 2, 1.0f);
        }
        else
        {
            auto left = Rectangle<float> (28.0f, 116.0f, 660.0f, 640.0f), right = Rectangle<float> (712.0f, 116.0f, 460.0f, 640.0f);
            g.setColour (kc::panel); g.fillRoundedRectangle (left, 20.0f); g.fillRoundedRectangle (right, 20.0f);
            kc::text (g, "FOOTSWITCHES", { 52.0f, 132.0f, 220.0f, 24.0f }, 18.0f, kc::ink, Justification::centredLeft, 0.14f);
            kc::text (g, "Same pattern as Keystone, so your feet don't relearn anything", { 260.0f, 132.0f, 404.0f, 24.0f }, 14.0f, kc::dim, Justification::centredRight);
            auto boxes = Rectangle<float> (52.0f, 540.0f, 612.0f, 70.0f);
            const float bw = (boxes.getWidth() - 12.0f) / 2.0f;
            g.setColour (kc::card);
            g.fillRoundedRectangle (boxes.withWidth (bw), 14.0f);
            g.fillRoundedRectangle (boxes.withTrimmedLeft (bw + 12.0f), 14.0f);
            kc::text (g, "HOLD C AND D", { 66.0f, 550.0f, bw - 20.0f, 16.0f }, 13.0f, kc::dim, Justification::centredLeft, 0.08f);
            kc::text (g, "Keep", { 66.0f, 570.0f, bw - 20.0f, 24.0f }, 19.0f);
            kc::text (g, "HOLD A AND B", { 66.0f + bw + 12.0f, 550.0f, bw - 20.0f, 16.0f }, 13.0f, kc::dim, Justification::centredLeft, 0.08f);
            kc::text (g, "Switch rig", { 66.0f + bw + 12.0f, 568.0f, bw - 20.0f, 22.0f }, 19.0f);
            kc::text (g, "In the Keystone app only. Does nothing in Ableton.", { 66.0f + bw + 12.0f, 590.0f, bw - 20.0f, 16.0f }, 12.0f, kc::dim);
            kc::text (g, proc.footInputStatus(), { 52.0f, 676.0f, 612.0f, 18.0f }, 13.0f, proc.pedalsDirect() ? kc::orange : kc::dim);
            if (proc.learningWhat() == KuddleProcessor::LearnKeeper) kc::text (g, "Press your Keeper switch now", { 52.0f, 696.0f, 612.0f, 18.0f }, 13.0f, kc::orange);

            kc::text (g, "EXPRESSION", { 736.0f, 132.0f, 220.0f, 24.0f }, 18.0f, kc::ink, Justification::centredLeft, 0.14f);
            const auto& s = proc.pedals();
            kc::text (g, "CC " + String (s.expCC) + (proc.learningWhat() == KuddleProcessor::LearnExpression ? ": move it now" : ""), { 900.0f, 132.0f, 248.0f, 24.0f }, 13.0f,
                      proc.learningWhat() == KuddleProcessor::LearnExpression ? kc::orange : kc::dim, Justification::centredRight);
            auto group = Rectangle<float> (736.0f, 168.0f, 412.0f, 44.0f);
            g.setColour (kc::line); g.drawRoundedRectangle (group, 22.0f, 1.0f);
            kc::text (g, "Heel", { 736.0f, 284.0f, 100.0f, 18.0f }, 14.0f, kc::dim);
            kc::text (g, "Toe", { 1048.0f, 284.0f, 100.0f, 18.0f }, 14.0f, kc::dim, Justification::centredRight);
            const int target = (int) std::lround (proc.value (kp::id::expTarget));
            const char* about[] = { "Heel down is open and clear. As you push towards the toe, the mute closes in, the tape pushes harder and the treble rolls off. With the wah in manual, the pedal works the wah instead.",
                                    "The pedal sweeps the wah from heel to toe. Set the wah to manual so it stays where your foot is.",
                                    "A volume pedal: silent at the heel for swells, full at the toe. It sets the Output level.",
                                    "The pedal sets how much of the room you hear. Turn the Room on to hear it." };
            g.setColour (kc::ink);
            g.setFont (kc::font (15.0f, 0.02f));
            g.drawFittedText (about[jlimit (0, 3, target)], Rectangle<int> (736, 312, 412, 90), Justification::topLeft, 5, 1.0f);
            auto cal = Rectangle<float> (736.0f, 470.0f, 412.0f, 262.0f);
            g.setColour (kc::card); g.fillRoundedRectangle (cal, 14.0f);
            g.setColour (kc::ink);
            g.drawFittedText ("Rock the pedal to the heel and press Heel, then to the toe and press Toe. If it still works backwards, press Reverse.",
                              Rectangle<int> (750, 482, 384, 60), Justification::topLeft, 3, 1.0f);
            kc::text (g, "Shared with every Flip Fella plugin and with Keystone." + String (s.reversed ? " Reversed." : ""), { 750.0f, 604.0f, 384.0f, 18.0f }, 13.0f, kc::dim);
        }
    }

    void resized() override
    {
        // header
        prev.setBounds (300, 26, 44, 44);
        presetTens.setBounds (352, 23, 38, 50);
        presetOnes.setBounds (394, 23, 38, 50);
        nameArea = { 442.0f, 26.0f, 200.0f, 44.0f };
        next.setBounds (646, 26, 44, 44);
        back.setBounds (28, 26, 84, 44);
        studio.setBounds (762, 26, 86, 44);
        live.setBounds (848, 26, 70, 44);
        keep.setBounds (930, 26, 96, 44);
        pebble.setBounds (1086, 6, 96, 90);

        if (page == Page::Main)
        {
            dial.setBounds (58, 160, 320, 300);
            meltTens.setBounds (168, 434, 46, 60);
            meltOnes.setBounds (220, 434, 46, 60);
            meltWah.setBounds (48, 612, 340, 48);
            const int x0 = 432, y0 = 152, gap = 14, w = (740 - gap * 3) / 4, h = (676 - y0 - gap) / 2;
            for (int i = 0; i < (int) mainCards.size(); ++i)
                mainCards[(size_t) i]->setBounds (x0 + (i % 4) * (w + gap), y0 + (i / 4) * (h + gap), w, h);
            const int tw = (900 - 3 * 14) / 4;
            for (int i = 0; i < 4; ++i) tiles[(size_t) i]->setBounds (28 + i * (tw + 14), 690, tw, 66);
            deepButton.setBounds (980, 712, 80, 44);
            pedalButton.setBounds (1068, 712, 104, 44);
        }
        else if (page == Page::Deep)
        {
            revert.setBounds (880, 26, 180, 44);
            const int x0 = 28, y0 = 116, gap = 14, w = (1144 - gap * 3) / 4, h = (576 - y0 - gap * 2) / 2;
            for (int i = 0; i < (int) deepCards.size(); ++i)
                deepCards[(size_t) i]->setBounds (x0 + (i % 4) * (w + gap), y0 + (i / 4) * (h + gap), w, h);
            const int rx = 270, rw = (1154 - rx - 3 * 24) / 4;
            for (int i = 0; i < 4; ++i) reach[(size_t) i]->setBounds (rx + i * (rw + 24), 604, rw, 56);
        }
        else
        {
            for (int i = 0; i < 4; ++i) footRows[(size_t) i]->setBounds (52, 168 + i * 92, 612, 80);
            for (int i = 0; i < 4; ++i) targets[(size_t) i]->setBounds (737 + i * 103, 169, 102, 42);
            zones.setBounds (736, 230, 412, 52);
            heel.setBounds (750, 548, 120, 44);
            toe.setBounds (882, 548, 120, 44);
            reverse.setBounds (1014, 548, 120, 44);
            learnExp.setBounds (750, 640, 186, 40);
            device.setBounds (948, 640, 186, 40);
            learnKeep.setBounds (52, 622, 140, 40);
            defaults.setBounds (204, 622, 110, 40);
            exportK.setBounds (326, 622, 140, 40);
        }
        tuner.setBounds (0, 96, getWidth(), getHeight() - 96);
    }

    void tick()
    {
        const double t = kc::now();
        const double dt = lastTick > 0 ? jmin (0.1, t - lastTick) : 0.016;
        lastTick = t;

        const int sel = proc.selectedPreset();
        const String num = sel >= 0 ? String (sel + 1).paddedLeft ('0', 2) : String ("--");
        presetTens.setValue (num.substring (0, 1)); presetOnes.setValue (num.substring (1, 2));
        presetTens.advance (dt); presetOnes.advance (dt);
        const String melt = String ((int) std::lround (jlimit (0.0f, 99.0f, proc.value (kp::id::melt)))).paddedLeft ('0', 2);
        meltTens.setValue (melt.substring (0, 1)); meltOnes.setValue (melt.substring (1, 2));
        meltTens.advance (dt); meltOnes.advance (dt);
        studio.setLit (! proc.keeperLive());
        live.setLit (proc.keeperLive());
        pebble.update (t, dt);

        if (page == Page::Main)
        {
            dial.repaint();
            meltWah.tick();
            for (auto& c : mainCards) c->tick (dt);
            for (auto& c : tiles) c->tick (dt);
        }
        else if (page == Page::Deep)
        {
            for (auto& c : deepCards) c->tick (dt);
            for (auto& r : reach) r->tick (dt);
        }
        else
        {
            for (auto& r : footRows) r->tick (dt);
            const int target = (int) std::lround (proc.value (kp::id::expTarget));
            for (int i = 0; i < 4; ++i) targets[(size_t) i]->setLit (i == target);
            zones.repaint();
        }

        const bool tun = proc.tunerOn() && page == Page::Main;
        if (tun != tuner.isVisible()) { tuner.setVisible (tun); if (tun) tuner.toFront (false); }
        if (tun) tuner.tick (dt);

        // the header line and preset name
        repaint (0, 0, getWidth(), 96);
        if (page == Page::Pedal) repaint (28, 660, 660, 60);
    }
};

// =====================================================================================
KuddleEditor::KuddleEditor (KuddleProcessor& p)
    : AudioProcessorEditor (&p), impl (std::make_unique<Impl> (p))
{
    addAndMakeVisible (*impl);
    impl->setBounds (0, 0, 1200, 780);
    setResizable (true, true);
    setResizeLimits (720, 468, 1800, 1170);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio (1200.0 / 780.0);
    setSize (1200, 780);
    startTimerHz (60);
}

KuddleEditor::~KuddleEditor() { stopTimer(); }

void KuddleEditor::paint (Graphics& g) { g.fillAll (kc::bg); }

void KuddleEditor::resized()
{
    impl->setTransform (AffineTransform::scale ((float) getWidth() / 1200.0f));
}

void KuddleEditor::timerCallback() { impl->tick(); }
