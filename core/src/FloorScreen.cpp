#include "FloorScreen.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb/stb_truetype.h"

// the fonts, built into the program by CMake from core/fonts
extern const unsigned char ks_font_archivo_extrabold[];
extern const unsigned char ks_font_archivo_medium[];

namespace ks::floor
{
namespace
{
struct Col { uint8_t r = 0, g = 0, b = 0; };
constexpr Col hex (uint32_t v) { return { (uint8_t) (v >> 16), (uint8_t) (v >> 8), (uint8_t) v }; }

const Col kBlack = hex (0x000000), kCream = hex (0xf4f1ea), kMute = hex (0x8d8a83), kCard = hex (0x1c1c1c),
          kOff = hex (0x2a2a2a), kOffText = hex (0x5f5e5a), kDark = hex (0x111111), kWhite = hex (0xffffff);
const Col kBank[3] = { hex (0xEF9F27), hex (0xAFA9EC), hex (0x85B7EB) };
const char* const kBankName[3] = { "GRIT", "GROOVE", "GLOW" };
const Col kSongGreen = hex (0x97C459), kSongGreenText = hex (0x173404), kNextPurple = hex (0xAFA9EC);
const Col kTeal = hex (0x5DCAA5), kAmber = hex (0xEF9F27);
const Col kProblem = hex (0xA32D2D), kWarning = hex (0x854F0B);
const Col kPebble = hex (0x4B57FF), kPebbleShade = hex (0x2329A8), kPupil = hex (0x0B0B12);

constexpr float S = 0.5f;   // design units (640 x 480) to screen pixels

std::vector<uint32_t> decode (const std::string& s, bool upper)
{
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();)
    {
        const auto c = (unsigned char) s[i];
        uint32_t cp = c;
        int n = 0;
        if (c >= 0xf0) { cp = c & 0x07; n = 3; }
        else if (c >= 0xe0) { cp = c & 0x0f; n = 2; }
        else if (c >= 0xc0) { cp = c & 0x1f; n = 1; }
        ++i;
        for (int k = 0; k < n && i < s.size(); ++k, ++i) cp = (cp << 6) | ((unsigned char) s[i] & 0x3f);
        if (upper && cp >= 'a' && cp <= 'z') cp -= 32;
        out.push_back (cp);
    }
    return out;
}
} // namespace

void Picture::toRgb565 (std::vector<uint8_t>& out) const
{
    out.resize ((size_t) kWidth * kHeight * 2);
    for (size_t i = 0, n = (size_t) kWidth * kHeight; i < n; ++i)
    {
        const uint16_t v = (uint16_t) (((rgb[i * 3] & 0xf8) << 8) | ((rgb[i * 3 + 1] & 0xfc) << 3) | (rgb[i * 3 + 2] >> 3));
        out[i * 2] = (uint8_t) (v >> 8);
        out[i * 2 + 1] = (uint8_t) v;
    }
}

struct Renderer::Impl
{
    struct Font
    {
        stbtt_fontinfo info {};
        int capHeight = 700;
    };
    struct Glyph
    {
        std::vector<uint8_t> alpha;
        int w = 0, h = 0, x0 = 0, y0 = 0;
    };
    Font fonts[2];   // 0 ExtraBold, 1 Medium
    std::map<uint64_t, Glyph> glyphs;
    Picture* pic = nullptr;

    Impl()
    {
        const unsigned char* data[2] = { ks_font_archivo_extrabold, ks_font_archivo_medium };
        for (int i = 0; i < 2; ++i)
        {
            auto& f = fonts[i];
            stbtt_InitFont (&f.info, data[i], stbtt_GetFontOffsetForIndex (data[i], 0));
            int x0, y0, x1, y1;
            if (stbtt_GetCodepointBox (&f.info, 'H', &x0, &y0, &x1, &y1)) f.capHeight = y1;
        }
    }

    // ---- pixels (screen units) ----
    void blend (int x, int y, Col c, float a)
    {
        if (x < 0 || y < 0 || x >= kWidth || y >= kHeight || a <= 0.0f) return;
        a = std::min (a, 1.0f);
        auto* p = &pic->rgb[((size_t) y * kWidth + (size_t) x) * 3];
        p[0] = (uint8_t) std::lround (p[0] + (c.r - p[0]) * a);
        p[1] = (uint8_t) std::lround (p[1] + (c.g - p[1]) * a);
        p[2] = (uint8_t) std::lround (p[2] + (c.b - p[2]) * a);
    }

    // a rectangle with rounded corners (radii top left, top right, bottom right, bottom
    // left), anti-aliased, in design units
    void roundRect (float x, float y, float w, float h, Col c, float tl, float tr = -1, float br = -1, float bl = -1)
    {
        if (tr < 0) tr = tl;
        if (br < 0) br = tl;
        if (bl < 0) bl = tl;
        x *= S; y *= S; w *= S; h *= S; tl *= S; tr *= S; br *= S; bl *= S;
        const int px0 = (int) std::floor (x), py0 = (int) std::floor (y), px1 = (int) std::ceil (x + w), py1 = (int) std::ceil (y + h);
        for (int py = py0; py < py1; ++py)
            for (int px = px0; px < px1; ++px)
            {
                const float cx = px + 0.5f, cy = py + 0.5f;
                const float r = cx < x + w * 0.5f ? (cy < y + h * 0.5f ? tl : bl) : (cy < y + h * 0.5f ? tr : br);
                // signed distance to the rounded box
                const float qx = std::fabs (cx - (x + w * 0.5f)) - (w * 0.5f - r);
                const float qy = std::fabs (cy - (y + h * 0.5f)) - (h * 0.5f - r);
                const float out = std::hypot (std::max (qx, 0.0f), std::max (qy, 0.0f)) + std::min (std::max (qx, qy), 0.0f) - r;
                blend (px, py, c, std::clamp (0.5f - out, 0.0f, 1.0f));
            }
    }
    void rect (float x, float y, float w, float h, Col c) { roundRect (x, y, w, h, c, 0); }

    void ellipse (float cx, float cy, float rx, float ry, Col c)
    {
        cx *= S; cy *= S; rx *= S; ry *= S;
        for (int py = (int) std::floor (cy - ry); py <= (int) std::ceil (cy + ry); ++py)
            for (int px = (int) std::floor (cx - rx); px <= (int) std::ceil (cx + rx); ++px)
            {
                const float dx = (px + 0.5f - cx) / rx, dy = (py + 0.5f - cy) / ry;
                const float d = (std::sqrt (dx * dx + dy * dy) - 1.0f) * std::min (rx, ry);
                blend (px, py, c, std::clamp (0.5f - d, 0.0f, 1.0f));
            }
    }

    // ---- text ----
    float pixelScale (int font, float designSize) const
    {
        return stbtt_ScaleForMappingEmToPixels (&fonts[font].info, designSize * S);
    }
    float capHeight (int font, float designSize) const { return fonts[font].capHeight * pixelScale (font, designSize); }

    const Glyph& glyph (int font, float scale, uint32_t cp, int sub)
    {
        uint32_t sbits;
        std::memcpy (&sbits, &scale, sizeof (sbits));
        const uint64_t key = ((uint64_t) sbits << 32) ^ ((uint64_t) font << 30) ^ ((uint64_t) sub << 26) ^ cp;
        auto it = glyphs.find (key);
        if (it != glyphs.end()) return it->second;
        if (glyphs.size() > 4000) glyphs.clear();   // a long gig never grows it for ever
        Glyph g;
        const float shift = sub * 0.25f;
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBoxSubpixel (&fonts[font].info, (int) cp, scale, scale, shift, 0, &x0, &y0, &x1, &y1);
        g.w = std::max (0, x1 - x0);
        g.h = std::max (0, y1 - y0);
        g.x0 = x0;
        g.y0 = y0;
        g.alpha.assign ((size_t) g.w * (size_t) g.h, 0);
        if (g.w > 0 && g.h > 0)
            stbtt_MakeCodepointBitmapSubpixel (&fonts[font].info, g.alpha.data(), g.w, g.h, g.w, scale, scale, shift, 0, (int) cp);
        return glyphs.emplace (key, std::move (g)).first->second;
    }

    bool hasGlyph (int font, uint32_t cp) const { return stbtt_FindGlyphIndex (&fonts[font].info, (int) cp) != 0; }

    // width in screen pixels, letter spacing in ems, no spacing after the last letter
    float measure (const std::vector<uint32_t>& cps, int font, float designSize, float spacing)
    {
        const float sc = pixelScale (font, designSize);
        float w = 0;
        for (size_t i = 0; i < cps.size(); ++i)
        {
            int adv, lsb;
            stbtt_GetCodepointHMetrics (&fonts[font].info, (int) cps[i], &adv, &lsb);
            w += adv * sc;
            if (i + 1 < cps.size())
                w += stbtt_GetCodepointKernAdvance (&fonts[font].info, (int) cps[i], (int) cps[i + 1]) * sc + spacing * designSize * S;
        }
        return w;
    }

    // draws from screen x at the screen baseline; returns the width
    float drawRun (const std::vector<uint32_t>& cps, int font, float designSize, float spacing, float x, float baseline, Col c)
    {
        const float sc = pixelScale (font, designSize);
        const float start = x;
        const int by = (int) std::lround (baseline);
        for (size_t i = 0; i < cps.size(); ++i)
        {
            const int ix = (int) std::floor (x);
            const int sub = std::clamp ((int) ((x - ix) * 4.0f), 0, 3);
            const auto& g = glyph (font, sc, cps[i], sub);
            for (int gy = 0; gy < g.h; ++gy)
                for (int gx = 0; gx < g.w; ++gx)
                    if (const auto a = g.alpha[(size_t) gy * g.w + gx]) blend (ix + g.x0 + gx, by + g.y0 + gy, c, a / 255.0f);
            int adv, lsb;
            stbtt_GetCodepointHMetrics (&fonts[font].info, (int) cps[i], &adv, &lsb);
            x += adv * sc;
            if (i + 1 < cps.size())
                x += stbtt_GetCodepointKernAdvance (&fonts[font].info, (int) cps[i], (int) cps[i + 1]) * sc + spacing * designSize * S;
        }
        return x - start;
    }

    struct Fit { std::vector<uint32_t> cps; float size; };

    // the biggest size up to `size` (down to `minSize`) that fits `maxW` design units;
    // still too wide at the smallest: cut with an ellipsis
    Fit fit (const std::string& text, int font, float size, float minSize, float spacing, float maxW)
    {
        Fit f { decode (text, true), size };
        const float limit = maxW * S;
        while (f.size > minSize && measure (f.cps, font, f.size, spacing) > limit) f.size = std::max (minSize, f.size * 0.94f);
        if (measure (f.cps, font, f.size, spacing) <= limit) return f;
        const uint32_t dots = hasGlyph (font, 0x2026) ? 0x2026u : (uint32_t) '.';
        while (! f.cps.empty())
        {
            f.cps.pop_back();
            while (! f.cps.empty() && f.cps.back() == ' ') f.cps.pop_back();
            auto t = f.cps;
            t.push_back (dots);
            if (measure (t, font, f.size, spacing) <= limit) { f.cps = t; break; }
        }
        return f;
    }

    // text in a box (design units), centred both ways on its capitals
    void textCentred (const std::string& text, float x, float y, float w, float h, float size, Col c, float spacing = 0, float minSize = -1, int font = 0)
    {
        const auto f = fit (text, font, size, minSize < 0 ? size * 0.4f : minSize, spacing, w - 24);
        const float tw = measure (f.cps, font, f.size, spacing);
        const float baseline = (y + h * 0.5f) * S + capHeight (font, f.size) * 0.5f;
        drawRun (f.cps, font, f.size, spacing, (x + w * 0.5f) * S - tw * 0.5f, baseline, c);
    }

    // text from the left (or right) edge, with its capitals centred on `midY`; returns the
    // width in design units
    float textAt (const std::string& text, float x, float midY, float size, Col c, float spacing = 0, bool alignRight = false, int font = 0, float maxW = 1e9f, float minSize = -1)
    {
        const auto f = fit (text, font, size, minSize < 0 ? size : minSize, spacing, maxW);
        const float tw = measure (f.cps, font, f.size, spacing);
        const float baseline = midY * S + capHeight (font, f.size) * 0.5f;
        drawRun (f.cps, font, f.size, spacing, alignRight ? x * S - tw : x * S, baseline, c);
        return tw / S;
    }

    // ---- pieces ----
    void flipCard (float x, float y, float w, float h, Col bg = kCard)
    {
        roundRect (x, y, w, h, bg, 10);
        rect (x, y + h * 0.5f - 1.5f, w, 3, kBlack);
    }

    // the mode tag top left: a coloured pill with dark text
    void tag (const std::string& text, Col bg, Col fg)
    {
        const float size = 22, spacing = 0.12f;
        const float tw = measure (decode (text, true), 0, size, spacing) / S;
        roundRect (24, 23, tw + 24, 34, bg, 6);
        textAt (text, 36, 40, size, fg, spacing);
    }
    void headRight (const std::string& text)
    {
        textAt (text, 616, 40, 26, kMute, 0.06f, true);
    }

    // ---- screens ----
    void splash()
    {
        ellipse (318 + 4, 147.5f + 5, 58, 55.5f, kPebbleShade);
        ellipse (318, 147.5f, 58, 55.5f, kPebble);
        for (float ex : { 318.0f - 18, 318.0f + 18 })
        {
            ellipse (ex, 140.5f, 13, 13, kWhite);
            ellipse (ex, 140.5f, 5.5f, 5.5f, kPupil);
        }
        const char* word = "KEYSTONE";
        for (int i = 0; i < 8; ++i)
        {
            const float x = 43 + i * 70.0f;
            roundRect (x, 238, 64, 96, kCard, 10);
            textCentred (std::string (1, word[i]), x, 238, 64, 96, 66, kCream);
            rect (x, 238 + 47, 64, 3, kBlack);
        }
        const float tw = measure (decode ("LIVE BASS RIG", true), 1, 20, 0.24f) / S;
        textAt ("LIVE BASS RIG", 320 - tw * 0.5f, 376, 20, kMute, 0.24f, false, 1);
    }

    void song (const View& v)
    {
        tag (v.songCount > 0 ? "SONG " + std::to_string (v.songNumber) + " / " + std::to_string (v.songCount) : std::string ("SONG"), kSongGreen, kSongGreenText);
        if (v.bpm > 0) headRight (std::to_string ((int) std::lround (v.bpm)) + " BPM");
        // the song above, the part big; a song without parts: just its name, big
        const bool hasPart = ! v.part.empty();
        if (hasPart)
        {
            roundRect (24, 72, 592, 96, kCard, 10);
            textCentred (v.song, 24, 72, 592, 96, 64, kCream, 0.04f, 36);
            rect (24, 72 + 47, 592, 3, kBlack);
        }
        const float top = hasPart ? 182 : 72, h = hasPart ? 176 : 286;
        roundRect (24, top, 592, h, kCard, 10);
        textCentred (hasPart ? v.part : v.song, 24, top, 592, h, 124, kCream, 0.02f, 48);
        rect (24, top + h * 0.5f - 1.5f, 592, 3, kBlack);
        const std::string label = v.next.empty() ? "LAST SONG" : v.nextIsSong ? "NEXT SONG" : "NEXT";
        const float lw = textAt (label, 24, 404, 24, kNextPurple, 0.14f);
        if (! v.next.empty()) textAt (v.next, 24 + lw + 16, 404, 44, kCream, 0.04f, false, 0, 592 - lw - 16, 28);
    }

    void block (float x, float y, float w, float h, const char* letter, bool lit, Col on, float size, float radius)
    {
        roundRect (x, y, w, h, lit ? on : kOff, radius);
        textCentred (letter, x, y, w, h, size, lit ? kDark : kOffText);
    }

    void stomp (const View& v)
    {
        const int b = std::clamp (v.bank, 0, 2);
        const Col c = kBank[b];
        // the bank you're on: a card with a strip of its colour along the top
        roundRect (24, 22, 384, 436, c, 10);
        roundRect (24, 32, 384, 426, kCard, 0, 0, 10, 10);
        const std::string name = kBankName[b];
        const float nameSize = name.size() > 4 ? 76.0f : 112.0f;
        textAt (name, 42, 48 + nameSize * 0.5f, nameSize, kCream, 0.03f, false, 0, 348, 50);
        const float dw = textAt ("DRIVE", 42, 178, 20, kMute, 0.06f);
        if (! v.capture.empty())
            textAt (v.capture, 42 + dw + 10, 178, 22, kCream, 0.04f, false, 0, 348 - dw - 10, 17);
        const char* letters[4] = { "A", "B", "C", "D" };
        const float bw = (348 - 24) / 4.0f;
        for (int i = 0; i < 4; ++i)
        {
            const bool lit = i < 3 ? v.fx[b][i] : v.drone;
            block (42 + i * (bw + 8), 270, bw, 170, letters[i], lit, i < 3 ? c : kCream, 44, 8);
        }
        // the other two banks, stacked on the right
        int row = 0;
        for (int o = 0; o < 3; ++o)
        {
            if (o == b) continue;
            const float y = 22 + row++ * 224.0f;
            roundRect (420, y, 196, 212, kCard, 10);
            textAt (kBankName[o], 434, y + 14 + 15, 26, kBank[o], 0.06f, false, 0, 168, 18);
            const float cw = (196 - 28 - 8) / 2.0f, ch = (212 - 14 - 30 - 10 - 14 - 8) / 2.0f;
            for (int i = 0; i < 4; ++i)
            {
                const float x = 434 + (i % 2) * (cw + 8), yy = y + 14 + 30 + 10 + (i / 2) * (ch + 8);
                block (x, yy, cw, ch, letters[i], i < 3 && v.fx[o][i], kBank[o], 22, 8);
            }
        }
    }

    void tuner (const View& v)
    {
        const bool heard = ! v.note.empty();
        const bool inTune = heard && std::fabs (v.cents) <= 2.0f;
        const Col tone = ! heard ? kMute : inTune ? kTeal : kAmber;
        tag ("TUNER", tone, kDark);
        if (v.muted) headRight ("MUTED");
        roundRect (24, 72, 300, 340, kCard, 10);
        if (heard) textCentred (v.note, 24, 72, 300, 340, 280, kCream, 0, 120);
        rect (24, 72 + 168.5f, 300, 3, kBlack);
        if (heard)
        {
            const int c = (int) std::lround (v.cents);
            const std::string num = c == 0 ? "0" : (c < 0 ? (hasGlyph (0, 0x2212) ? "\xe2\x88\x92" : "-") : "+") + std::to_string (std::abs (c));
            textAt (num, 344, 166 + 55, 110, tone, 0, false, 0, 272, 60);
            textAt (inTune ? "IN TUNE" : c < 0 ? "CENTS FLAT" : "CENTS SHARP", 344, 300, 30, tone, 0.1f, false, 0, 272, 18);
        }
        else textAt ("PLAY A NOTE", 344, 242, 30, kMute, 0.1f, false, 0, 272, 18);
    }

    void alert (const View& v)
    {
        const Col card = v.alertWarning ? kWarning : kProblem;
        tag ("ALERT", card, kWhite);
        if (v.alertSeconds > 0) headRight ("GONE IN " + std::to_string (v.alertSeconds) + " S");
        flipCard (24, 72, 592, 150, card);
        textCentred (v.alertTop, 24, 72, 592, 150, 128, kWhite, 0.03f, 56);
        rect (24, 72 + 73.5f, 592, 3, kBlack);
        flipCard (24, 234, 592, 150, card);
        textCentred (v.alertBottom, 24, 234, 592, 150, 128, kWhite, 0.03f, 56);
        rect (24, 234 + 73.5f, 592, 3, kBlack);
        if (! v.alertNote.empty()) textAt (v.alertNote, 24, 414, 30, kCream, 0.04f, false, 0, 592, 18);
    }

    void kuddle (const View& v)
    {
        tag ("KUDDLE", kAmber, kDark);
        roundRect (24, 72, 592, 286, kCard, 10);
        textCentred (v.preset, 24, 72, 592, 286, 124, kCream, 0.02f, 48);
        rect (24, 72 + 141.5f, 592, 3, kBlack);
    }
};

Renderer::Renderer() : impl (std::make_unique<Impl>()) {}
Renderer::~Renderer() = default;

float Renderer::textWidth (const std::string& s, float designSize, bool medium)
{
    return impl->measure (decode (s, true), medium ? 1 : 0, designSize, 0);
}

void Renderer::draw (const View& v, Picture& out)
{
    std::fill (out.rgb.begin(), out.rgb.end(), (uint8_t) 0);
    impl->pic = &out;
    switch (v.screen)
    {
        case Screen::Splash: impl->splash(); break;
        case Screen::Song:   impl->song (v); break;
        case Screen::Stomp:  impl->stomp (v); break;
        case Screen::Tuner:  impl->tuner (v); break;
        case Screen::Alert:  impl->alert (v); break;
        case Screen::Kuddle: impl->kuddle (v); break;
    }
    impl->pic = nullptr;
}
} // namespace ks::floor
