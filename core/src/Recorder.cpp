#include "Recorder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace ks
{
// 16 seconds at up to 96 kHz, allocated once so the audio thread never waits on it
static constexpr size_t kRingFrames = 16 * 96000;

static fs::path toPath (const std::string& utf8) { return fs::path (std::u8string (utf8.begin(), utf8.end())); }
static std::string fromPath (const fs::path& p) { const auto u = p.u8string(); return std::string (u.begin(), u.end()); }

Recorder::Recorder (std::string f) : folder (std::move (f))
{
    ring.assign (kRingFrames * 2, 0.0f);
    writer = std::thread ([this] { run(); });
}

Recorder::~Recorder()
{
    quit.store (true);
    cv.notify_all();
    if (writer.joinable()) writer.join();
}

void Recorder::setRate (double rate) { sr.store (rate); }

void Recorder::push (const float* dry, const float* out, int n)
{
    const int64_t cap = (int64_t) ring.size() / 2;
    int64_t w = written.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i, ++w)
    {
        const size_t k = (size_t) (w % cap) * 2;
        ring[k] = dry[i];
        ring[k + 1] = out[i];
    }
    written.store (w, std::memory_order_release);
}

void Recorder::setEnabled (bool on) { enabled.store (on); if (! on) stopRequest.store (true); cv.notify_all(); }
void Recorder::stopTake() { stopRequest.store (true); cv.notify_all(); }

void Recorder::mark (const std::string& what)
{
    const std::lock_guard<std::mutex> l (lock);
    pendingMarks.emplace_back (written.load(), what);
}

std::vector<std::pair<double, std::string>> Recorder::recentMarks (size_t n) const
{
    std::vector<std::pair<double, std::string>> out;
    const std::lock_guard<std::mutex> l (lock);
    if (! active.load()) return out;
    const size_t from = takeMarks.size() > n ? takeMarks.size() - n : 0;
    for (size_t i = from; i < takeMarks.size(); ++i) out.emplace_back ((double) takeMarks[i].first / sr.load(), takeMarks[i].second);
    return out;
}

std::string Recorder::firstSong (const std::string& take) const
{
    std::ifstream in (toPath (folder) / (take + ".json"));
    json j;
    try { if (in) in >> j; } catch (...) { return {}; }
    if (j.is_object() && j.contains ("marks") && j["marks"].is_array())
        for (const auto& m : j["marks"])
        {
            const auto w = m.value ("what", std::string());
            if (w.rfind ("Song: ", 0) == 0) return w.substr (6);
        }
    return {};
}

double Recorder::quietSeconds() const
{
    const int64_t last = lastSignal.load();
    return last < 0 ? 1.0e9 : (double) (written.load() - last) / sr.load();
}

double Recorder::takeSeconds() const { return (double) framesNow.load() / sr.load(); }

std::string Recorder::takeName() const
{
    const std::lock_guard<std::mutex> l (lock);
    return active.load() ? name : std::string();
}

void Recorder::flushForTests()
{
    const int64_t target = written.load();
    cv.notify_all();
    for (int i = 0; i < 2000 && caughtUp.load() < target; ++i) std::this_thread::sleep_for (std::chrono::milliseconds (5));
}

// ---------------------------------------------------------------------------------------
// The writer thread
// ---------------------------------------------------------------------------------------
void Recorder::run()
{
    std::vector<int32_t> buf;
    std::string context;   // the song loaded, for the start of a take
    while (! quit.load())
    {
        {
            std::unique_lock<std::mutex> l (wake);
            cv.wait_for (l, std::chrono::milliseconds (50));
        }
        const double rate = sr.load();
        const int64_t cap = (int64_t) ring.size() / 2;
        const int64_t w = written.load (std::memory_order_acquire);
        if (w - readFrame > cap - 1024) readFrame = w - cap + 1024;   // fallen behind: skip, never stall the audio

        // markers: songs give the context for the next take
        std::vector<std::pair<int64_t, std::string>> marks;
        {
            const std::lock_guard<std::mutex> l (lock);
            marks.swap (pendingMarks);
        }

        int64_t from = readFrame;
        if (! active.load())
        {
            for (auto& m : marks) if (m.second.rfind ("Song: ", 0) == 0) context = m.second;
            marks.clear();
            stopRequest.store (false);
            int64_t loud = -1;
            for (int64_t f = from; f < w && loud < 0; ++f)
                if (std::fabs (ring[(size_t) (f % cap) * 2]) > kPlayThreshold) loud = f;
            if (loud >= 0) lastSignal.store (w);
            if (! enabled.load() || loud < 0) { readFrame = w; caughtUp.store (w); continue; }
            from = std::max ({ loud - (int64_t) (kPreRollSeconds * rate), w - cap + 1024, (int64_t) 0 });
            open (from);
            if (! file) { readFrame = w; caughtUp.store (w); continue; }
            const std::lock_guard<std::mutex> l (lock);
            if (! context.empty()) takeMarks.emplace_back (0, context);
        }

        // write what's new
        buf.resize ((size_t) (w - from) * 2);
        size_t k = 0;
        float pk = std::pow (10.0f, peakDb.load() / 20.0f);
        for (int64_t f = from; f < w; ++f)
        {
            const size_t r = (size_t) (f % cap) * 2;
            for (int ch = 0; ch < 2; ++ch)
            {
                const float x = ring[r + (size_t) ch];
                const float ax = std::fabs (x);
                if (ch == 0 && ax > kPlayThreshold) { lastLoud = f; lastSignal.store (f); }
                pk = std::max (pk, ax);
                if (ax >= 0.999f) clip.store (true);
                buf[k++] = (int32_t) std::lrint (std::clamp (x, -1.0f, 1.0f) * 8388607.0f);
            }
        }
        peakDb.store (20.0f * std::log10 (std::max (pk, 1.0e-5f)));
        std::vector<uint8_t> bytes (k * 3);
        for (size_t i = 0; i < k; ++i)
        {
            bytes[i * 3] = (uint8_t) (buf[i] & 0xFF);
            bytes[i * 3 + 1] = (uint8_t) ((buf[i] >> 8) & 0xFF);
            bytes[i * 3 + 2] = (uint8_t) ((buf[i] >> 16) & 0xFF);
        }
        if (! bytes.empty()) file->write ((const char*) bytes.data(), (std::streamsize) bytes.size());
        frames += w - from;
        framesNow.store (frames);
        readFrame = w;
        {
            const std::lock_guard<std::mutex> l (lock);
            for (auto& m : marks) takeMarks.emplace_back (std::max<int64_t> (0, m.first - takeStart), m.second);
            markCount.store ((int) takeMarks.size());
        }
        if (frames - lastHeader > (int64_t) (5 * rate)) { writeHeader(); lastHeader = frames; }

        // end of a take: a quiet minute, the phone's Stop, switched off, or too long
        if (stopRequest.load() || w - lastLoud > (int64_t) (kQuietSeconds * rate) || frames > (int64_t) (kMaxTakeHours * 3600 * rate))
            close();
        caughtUp.store (w);
    }
    if (file) close();
}

static std::string stamp()
{
    const std::time_t t = std::time (nullptr);
    std::tm tm {};
#ifdef _WIN32
    localtime_s (&tm, &t);
#else
    localtime_r (&t, &tm);
#endif
    char s[32];
    std::strftime (s, sizeof (s), "%Y-%m-%d %H-%M-%S", &tm);
    return s;
}

void Recorder::open (int64_t startFrame)
{
    std::error_code ec;
    fs::create_directories (toPath (folder), ec);
    housekeeping();
    std::string n = stamp();
    if (fs::exists (toPath (folder) / (n + ".wav"))) n += " b";
    file = std::make_unique<std::fstream> (toPath (folder) / (n + ".wav"), std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
    if (! *file) { file.reset(); return; }
    {
        const std::lock_guard<std::mutex> l (lock);
        name = n;
        takeMarks.clear();
    }
    takeStart = startFrame;
    frames = 0; lastHeader = 0;
    lastLoud = startFrame + (int64_t) (kPreRollSeconds * sr.load());
    peakDb.store (-100); clip.store (false);
    stopRequest.store (false);
    writeHeader();
    active.store (true);
}

void Recorder::close()
{
    if (! file) { active.store (false); return; }
    // trim the quiet minute at the end down to three seconds
    const int64_t keep = std::min (frames, lastLoud - takeStart + (int64_t) (3 * sr.load()));
    if (keep < frames && keep > 0) frames = keep;
    writeHeader();
    file.reset();
    std::error_code ec;
    fs::resize_file (toPath (folder) / (name + ".wav"), (uintmax_t) (44 + frames * 6), ec);
    active.store (false);
    framesNow.store (0);
    stopRequest.store (false);
}

// the WAV header with the sizes so far, and the markers beside it
void Recorder::writeHeader()
{
    if (! file) return;
    const uint32_t rate = (uint32_t) std::lround (sr.load());
    const uint32_t data = (uint32_t) std::min<int64_t> (frames * 6, 0xFFFFFFFFll - 44);
    auto u32 = [] (uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (uint8_t) (v >> (8 * i)); };
    auto u16 = [] (uint8_t* p, uint16_t v) { p[0] = (uint8_t) v; p[1] = (uint8_t) (v >> 8); };
    uint8_t h[44] = { 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' };
    u32 (h + 4, 36 + data);
    u32 (h + 16, 16); u16 (h + 20, 1); u16 (h + 22, 2); u32 (h + 24, rate); u32 (h + 28, rate * 6); u16 (h + 32, 6); u16 (h + 34, 24);
    h[36] = 'd'; h[37] = 'a'; h[38] = 't'; h[39] = 'a';
    u32 (h + 40, data);
    file->seekp (0);
    file->write ((const char*) h, 44);
    file->seekp (44 + frames * 6);
    file->flush();

    json j;
    j["take"] = name;
    j["rate"] = rate;
    j["seconds"] = (double) frames / rate;
    j["tracks"] = { "dry bass", "to the amp" };
    j["marks"] = json::array();
    {
        const std::lock_guard<std::mutex> l (lock);
        for (auto& [f, what] : takeMarks) j["marks"].push_back ({ { "t", std::round ((double) f / rate * 100) / 100 }, { "what", what } });
    }
    const auto side = toPath (folder) / (name + ".json");
    const auto old = [&] { std::ifstream in (side); json o; try { if (in) in >> o; } catch (...) {} return o; }();
    if (old.is_object() && old.contains ("room")) j["room"] = old["room"];
    std::ofstream (side) << j.dump (2);
}

// below the free-space mark, the oldest takes go first (never the one being made)
void Recorder::housekeeping()
{
    std::error_code ec;
    while (freeBytes() >= 0 && freeBytes() < keepFreeBytes)
    {
        auto all = takes();
        if (all.empty()) return;
        const auto stem = all.back().name;
        for (const auto& e : fs::directory_iterator (toPath (folder), ec))
            if (fromPath (e.path().filename()).rfind (stem, 0) == 0) fs::remove (e.path(), ec);
        if (ec) return;
    }
}

int64_t Recorder::freeBytes() const
{
    std::error_code ec;
    const auto sp = fs::space (toPath (folder), ec);
    return ec ? -1 : (int64_t) sp.available;
}

std::vector<Recorder::Take> Recorder::takes() const
{
    std::vector<Take> out;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator (toPath (folder), ec))
    {
        if (e.path().extension() != ".wav") continue;
        Take t;
        t.name = fromPath (e.path().stem());
        t.bytes = (int64_t) e.file_size (ec);
        t.seconds = std::max<int64_t> (0, t.bytes - 44) / 6.0 / sr.load();
        t.modified = (int64_t) std::chrono::duration_cast<std::chrono::seconds> (e.last_write_time (ec).time_since_epoch()).count();
        out.push_back (t);
    }
    std::sort (out.begin(), out.end(), [] (const Take& a, const Take& b) { return a.name > b.name; });
    return out;
}

bool Recorder::addRoom (const std::string& take, int seq, bool last, const std::vector<uint8_t>& data, const std::string& ext, double startsAt)
{
    // a take's own name and a plain extension only, nothing that could point elsewhere
    if (take.empty() || take.find_first_of ("/\\:") != std::string::npos || take.find ("..") != std::string::npos) return false;
    if (ext.empty() || ext.size() > 5 || ! std::all_of (ext.begin(), ext.end(), [] (char c) { return std::isalnum ((unsigned char) c); })) return false;
    if (! fs::exists (toPath (folder) / (take + ".wav"))) return false;
    const auto p = toPath (folder) / (take + " room." + ext);
    std::ofstream f (p, std::ios::binary | (seq == 0 ? std::ios::trunc : std::ios::app));
    if (! f) return false;
    f.write ((const char*) data.data(), (std::streamsize) data.size());
    f.close();
    if (last)
    {
        const auto side = toPath (folder) / (take + ".json");
        json j;
        { std::ifstream in (side); try { if (in) in >> j; } catch (...) {} }
        if (! j.is_object()) j = json::object();
        j["room"] = fromPath (p.filename());
        if (startsAt >= 0) j["roomStartsAt"] = std::round (startsAt * 10.0) / 10.0;   // seconds into the take
        std::ofstream (side) << j.dump (2);
    }
    return true;
}
} // namespace ks
