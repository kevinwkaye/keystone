#pragma once
// The floor screen: the 2.0" 320 x 240 IPS screen in the pedal's lid. Draws one View into
// a 320 x 240 picture, which FloorPanel sends to the screen on the Pi (and the preview
// tool saves as a PNG). Flip Fella look: black, dark flip cards with a split across the
// middle, Archivo ExtraBold.
//
// The screen is read from standing height, so every screen has one big thing on it:
//   Splash  Pebble and KEYSTONE while Keystone starts
//   Song    the part, big, the song above it, what's next below; setlist place and tempo
//   Stomp   the bank you're on (name, Drive capture, A to D) on the left, the other two
//           banks on the right so an effect left on there shows at a glance
//   Tuner   the note, huge, and the cents beside it, amber or green
//   Alert   two red (or amber) cards over everything for a few seconds, then gone
//   Kuddle  the preset's name
//
// The designs are drawn at 640 x 480 on the Keystone Pedal screens canvas; positions here
// are in those units and halved when drawn.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ks::floor
{
constexpr int kWidth = 320, kHeight = 240;

enum class Screen { Splash, Song, Stomp, Tuner, Alert, Kuddle };

struct View
{
    Screen screen = Screen::Splash;

    // Song mode
    std::string song, part, next;   // part: empty for a song without parts; next: the next part, or the next song's name
    bool nextIsSong = false;        // next is a whole song (no more parts)
    int songNumber = 0, songCount = 0;
    float bpm = 0;

    // Stomp mode
    int bank = 0;                   // 0 Grit, 1 Groove, 2 Glow
    std::string capture;            // the Drive capture's short name
    bool fx[3][3] {};               // [bank][A to C] on
    bool drone = false;             // D
    bool speckleHold = false;       // Glow's A block says HOLD

    // Tuner
    std::string note;               // "E", "F#"; empty while no note is heard
    float cents = 0;
    bool muted = false;

    // Alert
    std::string alertTop, alertBottom, alertNote;
    bool alertWarning = false;      // amber rather than red
    int alertSeconds = 0;           // until it goes

    // Kuddle
    std::string preset;

    bool operator== (const View&) const = default;
};

// A 320 x 240 picture, 8-bit RGB, rows top to bottom.
struct Picture
{
    std::vector<uint8_t> rgb = std::vector<uint8_t> ((size_t) kWidth * kHeight * 3, 0);
    // RGB565, big-endian, as the ST7789 wants it
    void toRgb565 (std::vector<uint8_t>& out) const;
};

class Renderer
{
public:
    Renderer();
    ~Renderer();
    void draw (const View& v, Picture& out);

    // for tests: how wide a line of text comes out, in screen pixels
    float textWidth (const std::string& s, float designSize, bool medium = false);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace ks::floor
