// Draws each floor screen to a PNG, to check against the designs on the Keystone Pedal
// screens canvas. Usage: floor_preview [folder]   (default: the current folder)

#include "FloorScreen.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#include <cstdio>
#include <string>

using namespace ks::floor;

int main (int argc, char** argv)
{
    const std::string dir = argc > 1 ? std::string (argv[1]) + "/" : std::string();
    Renderer r;
    Picture pic;
    auto save = [&] (const char* name, const View& v)
    {
        r.draw (v, pic);
        const auto file = dir + name + ".png";
        if (! stbi_write_png (file.c_str(), kWidth, kHeight, 3, pic.rgb.data(), kWidth * 3)) { std::printf ("couldn't write %s\n", file.c_str()); return false; }
        std::printf ("%s\n", file.c_str());
        return true;
    };

    View v;
    v.screen = Screen::Splash;
    bool ok = save ("splash", v);

    v = {};
    v.screen = Screen::Song;
    v.song = "Blumes"; v.part = "Chorus"; v.next = "Verse 2"; v.songNumber = 3; v.songCount = 9; v.bpm = 124;
    ok &= save ("song", v);
    v.song = "When the Circus Comes to Town"; v.part = "Middle 32"; v.next = "Action Films"; v.nextIsSong = true;
    ok &= save ("song-long", v);
    v.song = "Maps"; v.part.clear(); v.next = "Notes"; v.nextIsSong = true; v.songNumber = 7;
    ok &= save ("song-whole", v);

    v = {};
    v.screen = Screen::Stomp;
    v.bank = 0; v.capture = "EQD BLUMES";
    v.fx[0][0] = v.fx[0][2] = true; v.fx[1][1] = true;
    ok &= save ("stomp-grit", v);
    v.bank = 1; v.drone = true;
    ok &= save ("stomp-groove", v);
    v.bank = 0; v.drone = false; v.capture = "AMPEG SVT CLASSIC WITH 8X10 CAB";
    ok &= save ("stomp-long-capture", v);

    v = {};
    v.screen = Screen::Tuner;
    v.note = "E"; v.cents = -12; v.muted = true;
    ok &= save ("tuner-flat", v);
    v.note = "A"; v.cents = 1;
    ok &= save ("tuner-in-tune", v);
    v.note.clear();
    ok &= save ("tuner-no-note", v);

    v = {};
    v.screen = Screen::Alert;
    v.alertTop = "Watch"; v.alertBottom = "Lost"; v.alertNote = "Bitrot glides back"; v.alertSeconds = 4;
    ok &= save ("alert", v);
    v.alertTop = "Pi"; v.alertBottom = "Hot"; v.alertNote = "Keystone is fine for now"; v.alertWarning = true;
    ok &= save ("alert-warning", v);

    v = {};
    v.screen = Screen::Kuddle;
    v.preset = "Khruangbin";
    ok &= save ("kuddle", v);
    return ok ? 0 : 1;
}
