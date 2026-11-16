#pragma once
// The laptop status window (Windows): what Keystone is connected to and how the bass is
// coming in, in the Flip Fella look. Closing it hides it to the tray; Keystone keeps playing.

#include "Health.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include "BinaryData.h"

namespace status
{
using namespace juce;

const Colour bg (0xff0E0E10), card (0xff18181B), btn (0xff1C1C20), flip (0xff262626), hinge (0xff0A0A0A),
    text (0xffF2EDE4), muted (0xffB9B2A6), blue (0xff4B57FF), green (0xff5DCAA5), orange (0xffF0997B);

inline Typeface::Ptr regular()
{
    static auto t = Typeface::createSystemTypefaceFor (BinaryData::RobotoCondensed_ttf, BinaryData::RobotoCondensed_ttfSize);
    return t;
}
inline Typeface::Ptr bold()
{
    static auto t = Typeface::createSystemTypefaceFor (BinaryData::RobotoCondensedBold_ttf, BinaryData::RobotoCondensedBold_ttfSize);
    return t;
}
inline Font font (float h, bool b = false) { return Font (FontOptions (b ? bold() : regular()).withHeight (h)); }

inline void pebble (Graphics& g, Rectangle<float> r)
{
    const float s = r.getWidth() / 200.0f;
    auto blob = [&] (float dx, float dy)
    {
        Path p;
        p.startNewSubPath (100, 22);
        p.cubicTo (146, 20, 176, 54, 174, 100);
        p.cubicTo (172, 146, 140, 176, 98, 174);
        p.cubicTo (54, 172, 24, 142, 26, 98);
        p.cubicTo (28, 56, 58, 24, 100, 22);
        p.closeSubPath();
        p.applyTransform (AffineTransform::translation (dx, dy).scaled (s).translated (r.getX(), r.getY()));
        return p;
    };
    g.setColour (Colour (0xff2329A8)); g.fillPath (blob (7, 9));
    g.setColour (blue); g.fillPath (blob (0, 0));
    auto at = [&] (float x, float y, float rad, Colour c) { g.setColour (c); g.fillEllipse (r.getX() + (x - rad) * s, r.getY() + (y - rad) * s, rad * 2 * s, rad * 2 * s); };
    at (78, 80, 18, Colours::white); at (122, 80, 18, Colours::white);
    at (80, 82, 9, Colour (0xff0B0B12)); at (124, 82, 9, Colour (0xff0B0B12));
}

class View : public Component, private Timer
{
public:
    std::function<Health()> getHealth;
    std::function<void()> onRestart, onOpenFolder, onHide;

    View()
    {
        LookAndFeel::getDefaultLookAndFeel().setDefaultSansSerifTypeface (regular());
        for (auto* b : { &restart, &folder, &hide })
        {
            addAndMakeVisible (*b);
            b->setColour (TextButton::buttonColourId, btn);
            b->setColour (TextButton::textColourOffId, text);
            b->setColour (ComboBox::outlineColourId, Colours::transparentBlack);
        }
        restart.onClick = [this] { if (onRestart) onRestart(); };
        folder.onClick = [this] { if (onOpenFolder) onOpenFolder(); };
        hide.onClick = [this] { if (onHide) onHide(); };
        setSize (520, 640);
        startTimerHz (8);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (24, 20).removeFromBottom (44);
        const int w = (r.getWidth() - 16) / 3;
        restart.setBounds (r.removeFromLeft (w)); r.removeFromLeft (8);
        folder.setBounds (r.removeFromLeft (w)); r.removeFromLeft (8);
        hide.setBounds (r);
    }

    void paint (Graphics& g) override
    {
        g.fillAll (bg);
        const Health h = getHealth ? getHealth() : Health();
        auto r = getLocalBounds().reduced (24, 20).toFloat();

        // header: Pebble, name, song on a flip card
        const float headTop = r.getY();
        auto head = r.removeFromTop (52);
        pebble (g, head.removeFromLeft (48).withSizeKeepingCentre (48, 48));
        head.removeFromLeft (12);
        g.setColour (text); g.setFont (font (24, true));
        g.drawText ("KEYSTONE", head.withHeight (30), Justification::topLeft);
        g.setColour (muted); g.setFont (font (14));
        g.drawText (h.version + ", running for " + duration (h.uptimeSec), head.withTrimmedTop (30), Justification::topLeft);
        const String song = String (h.songNumber).paddedLeft ('0', 2) + " " + h.song;
        g.setFont (font (20, true));
        const float cw = jmin (230.0f, GlyphArrangement::getStringWidth (g.getCurrentFont(), song) + 28.0f);
        auto flipR = Rectangle<float> (r.getRight() - cw, headTop + 4, cw, 44);
        g.setColour (blue); g.fillRoundedRectangle (flipR, 6);
        g.setColour (Colours::white); g.drawText (song, flipR.reduced (14, 0), Justification::centred, true);
        g.setColour (hinge); g.fillRect (flipR.withHeight (2).withY (flipR.getCentreY() - 1));
        r.removeFromTop (14);

        // bass in
        auto inCard = r.removeFromTop (104);
        g.setColour (card); g.fillRoundedRectangle (inCard, 10);
        inCard.reduce (16, 14);
        label (g, inCard.removeFromTop (18), "BASS IN");
        inCard.removeFromTop (8);
        auto bar = inCard.removeFromTop (18);
        g.setColour (flip); g.fillRoundedRectangle (bar, 9);
        const float frac = jlimit (0.0f, 1.0f, (h.inDb + 60.0f) / 60.0f);
        const Colour inCol = h.inDb > -1 ? orange : (h.inDb < -50 ? muted : green);
        g.setColour (inCol); g.fillRoundedRectangle (bar.withWidth (jmax (bar.getHeight(), bar.getWidth() * frac)), 9);
        inCard.removeFromTop (8);
        g.setColour (text); g.setFont (font (15));
        g.drawText (inputWords (h), inCard, Justification::centredLeft);
        g.setColour (muted);
        g.drawText ("Input " + String (h.inChannel + 1), inCard, Justification::centredRight);
        r.removeFromTop (14);

        // connections
        struct Row { const char* name; String detail; bool ok; };
        const Row rows[] = {
            { "Audio interface", h.audioText, h.audioOk },
            { "Footswitches", h.fsText + (h.fsAgoSec >= 0 ? ", last press " + duration (h.fsAgoSec) + " ago (" + h.fsLast + ")" : String()), h.fsOk },
            { "Expression", h.expAgoSec < 0 ? String ("Not moved yet") : "Pedal at " + String (roundToInt (h.expPos * 100)) + "%, moved " + duration (h.expAgoSec) + " ago" + (h.expCalibrated ? "" : ", not calibrated"), h.expAgoSec >= 0 },
            { "Phone", h.phones > 0 ? "Connected from " + h.phoneAddresses : "None connected. Laptop is " + h.laptopAddresses, h.phones > 0 },
            { "Laptop", "CPU " + String (roundToInt (h.cpu * 100)) + "%, " + (h.dropouts == 0 ? String ("no dropouts") : String (h.dropouts) + " dropouts (" + String (h.dropoutsRecent) + " in the last minute)") + (h.light ? ", effects light" : ""), h.cpu < 0.7f && h.dropoutsRecent == 0 },
        };
        auto list = r.removeFromTop (8 + 52 * 5);
        g.setColour (card); g.fillRoundedRectangle (list, 10);
        list.reduce (16, 4);
        for (auto& row : rows)
        {
            auto line = list.removeFromTop (52);
            g.setColour (row.ok ? green : orange);
            g.fillEllipse (line.getX(), line.getCentreY() - 5, 10, 10);
            line.removeFromLeft (22);
            g.setColour (text); g.setFont (font (17, true));
            g.drawText (row.name, line.removeFromLeft (130), Justification::centredLeft);
            g.setColour (muted); g.setFont (font (14));
            g.drawFittedText (row.detail, line.toNearestInt(), Justification::centredLeft, 2, 0.9f);
            g.setColour (flip); g.fillRect (line.getX() - 152, line.getBottom() - 1, list.getWidth(), 1.0f);
        }
    }

private:
    void timerCallback() override { repaint(); }

    static void label (Graphics& g, Rectangle<float> r, const String& s)
    {
        g.setColour (muted); g.setFont (font (13));
        g.drawText (s, r, Justification::centredLeft);
    }
    static String inputWords (const Health& h)
    {
        if (h.inputDead) return "No signal at all: check Windows microphone access";
        if (h.inDb < -70) return "Silent: play a note, or check the cable and gain";
        if (h.inDb < -40) return String (roundToInt (h.inDb)) + " dB peak, very quiet: turn the gain up";
        if (h.inDb > -1) return String (roundToInt (h.inDb)) + " dB peak, clipping: turn the gain down";
        return String (roundToInt (h.inDb)) + " dB peak, good";
    }
    static String duration (double sec)
    {
        if (sec < 60) return String ((int) sec) + " s";
        if (sec < 3600) return String ((int) (sec / 60)) + " min";
        return String ((int) (sec / 3600)) + " h " + String (((int) sec % 3600) / 60) + " min";
    }

    TextButton restart { "Restart audio" }, folder { "Open folder" }, hide { "Hide to tray" };
};

class Window : public DocumentWindow
{
public:
    explicit Window (View* view) : DocumentWindow ("Keystone", bg, DocumentWindow::minimiseButton | DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (view, true);
        setResizable (false, false);
        centreWithSize (getWidth(), getHeight());
    }
    void closeButtonPressed() override { setVisible (false); }   // hide, keep playing
};
} // namespace status
