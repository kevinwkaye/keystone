#include "keystone/Controller.h"

#include "Analysis.h"
#include "Dsp.h"
#include "Footswitch.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace ks
{
// Never throw on odd characters: a device or file name that isn't valid UTF-8 is replaced,
// not allowed to take the link down.
static std::string dumps (const json& j) { return j.dump (-1, ' ', false, json::error_handler_t::replace); }

// Paths travel as UTF-8 everywhere; Windows' narrow paths would use the ANSI code page.
static fs::path toPath (const std::string& utf8) { return fs::path (std::u8string (utf8.begin(), utf8.end())); }
static std::string fromPath (const fs::path& p) { const auto u = p.u8string(); return std::string (u.begin(), u.end()); }

struct Controller::Song
{
    std::string name;
    bool part = false;                      // a part of the song above it
    std::map<std::string, float> values;   // non-global parameters only
};

struct Controller::State
{
    std::vector<Song> songs;
    int current = 0;
    FootswitchLogic logic;
    std::vector<std::string> outbox;
    std::string captureFile, spaceFile;
    double lastDTap = -10;
    int bank = 0;
    bool droneHeld = false;
    bool dTapCaptured = false;
};

static bool isGlobal (int i) { return (paramTable()[i].flags & Global) != 0; }

static std::map<std::string, float> defaults()
{
    std::map<std::string, float> m;
    for (int i = 0; i < kNumParams; ++i)
        if (! isGlobal (i)) m[paramTable()[i].id] = paramTable()[i].def;
    return m;
}

Controller::Controller (Engine& e, std::string data, std::string caps)
    : s (std::make_unique<State>()), engine (e), dataDir (std::move (data)), capturesDir (std::move (caps))
{
    s->logic.onEvent = [this] (int sw, FootswitchLogic::Event ev) { onFootswitch (sw, (int) ev); };
}

Controller::~Controller() = default;

// ---------------------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------------------
static json readJson (const fs::path& p)
{
    std::ifstream f (p);
    if (! f) return json();
    try { return json::parse (f); }
    catch (...) { return json(); }
}

static void writeJsonAtomic (const fs::path& p, const json& j)
{
    std::error_code ec;
    fs::create_directories (p.parent_path(), ec);
    const auto tmp = p.string() + ".tmp";
    {
        std::ofstream f (tmp, std::ios::trunc);
        if (! f) return;
        f << j.dump (2);
    }
    fs::rename (tmp, p, ec);
    if (ec) { fs::remove (p, ec); fs::rename (tmp, p, ec); }
}

void Controller::load()
{
    // setlist
    const auto sl = readJson (toPath (dataDir) / "setlist.json");
    s->songs.clear();
    if (sl.is_object() && sl.contains ("songs") && sl["songs"].is_array())
    {
        for (const auto& js : sl["songs"])
        {
            Song song;
            song.name = js.value ("name", std::string ("Song"));
            song.part = js.value ("part", false) && ! s->songs.empty();
            song.values = defaults();
            if (js.contains ("values") && js["values"].is_object())
                for (auto it = js["values"].begin(); it != js["values"].end(); ++it)
                    if (it.value().is_number() && song.values.count (it.key())) song.values[it.key()] = it.value().get<float>();
            // setlist version 2: the envelope filter quacks by default (band-pass, more
            // resonance, works on the whole bass) and the drone left the pedal
            if (sl.value ("version", 1) < 2)
            {
                song.values["filter.type"] = 1.0f;
                song.values["filter.q"] = 0.7f;
                song.values["filter.low"] = 200.0f;
                song.values["bloom.autocap"] = 0.0f;
            }
            // setlist version 5: Bitrot's place is a three-way choice, after the preamp by default
            if (sl.value ("version", 1) < 5) song.values["bitrot.place"] = song.values.count ("bitrot.first") && song.values["bitrot.first"] > 0.5f ? 0.0f : 2.0f;
            // setlist version 4: the drone sits further back
            if (sl.value ("version", 1) < 4 && song.values["drone.level"] > 0.45f) song.values["drone.level"] = 0.4f;
            // setlist version 3: Speckle is the whole Speckle engine (Sparkle is gone and the
            // effects moved down one), the octave up is off, and the drive has its body back
            if (sl.value ("version", 1) < 3)
            {
                const float fx = song.values["grains.effect"];
                song.values["grains.effect"] = fx < 0.5f ? 3.0f : fx - 1.0f;   // Sparkle becomes Haze
                song.values["grains.pitch"] = 0.0f;
                song.values["drive.lowInto"] = 0.0f;
                song.values["drive.edge.lowcut"] = 70.0f;
                song.values["drive.drive.lowcut"] = 70.0f;
            }
            s->songs.push_back (song);
            if ((int) s->songs.size() >= kMaxEntries) break;
        }
        s->current = std::clamp (sl.value ("current", 0), 0, std::max (0, (int) s->songs.size() - 1));
    }
    if (s->songs.empty())
    {
        // first run: the songs from the analysis, all on the same clean starting point
        for (auto* n : { "New choon", "I'm Not a Robot", "When the Circus Comes to Town", "The Comedian and the Boxer" })
            s->songs.push_back ({ n, false, defaults() });
        s->current = 0;
    }

    // global settings
    const auto st = readJson (toPath (dataDir) / "settings.json");
    if (st.is_object())
    {
        if (st.contains ("globals") && st["globals"].is_object())
            for (auto it = st["globals"].begin(); it != st["globals"].end(); ++it)
            {
                const int i = findParam (it.key().c_str());
                if (i >= 0 && isGlobal (i) && it.value().is_number()) engine.setParam ((P) i, it.value().get<float>());
            }
        s->captureFile = st.value ("capture", std::string());
        s->spaceFile = st.value ("space", std::string());
    }

    // the capture: the saved one, else the first in the folder
    std::string err;
    auto caps = listCaptures();
    bool loaded = false;
    if (! s->captureFile.empty() && std::find (caps.begin(), caps.end(), s->captureFile) != caps.end())
        loaded = useCapture (s->captureFile, err);
    if (! loaded && ! caps.empty()) loaded = useCapture (caps.front(), err);

    // the impulse for the Impulse reverb: the saved one, else the first in the folder
    auto spaces = listSpaces();
    bool spaceOk = false;
    if (! s->spaceFile.empty() && std::find (spaces.begin(), spaces.end(), s->spaceFile) != spaces.end())
        spaceOk = useSpace (s->spaceFile, err);
    if (! spaceOk && ! spaces.empty()) useSpace (spaces.front(), err);
    applySong (s->current);
}

void Controller::saveSetlist() const
{
    json j;
    j["version"] = 6;
    j["current"] = s->current;
    j["songs"] = json::array();
    for (const auto& song : s->songs)
    {
        json js;
        js["name"] = song.name;
        if (song.part) js["part"] = true;
        js["values"] = song.values;
        j["songs"].push_back (js);
    }
    writeJsonAtomic (toPath (dataDir) / "setlist.json", j);
}

void Controller::saveSettings() const
{
    json j;
    json g;
    for (int i = 0; i < kNumParams; ++i)
        if (isGlobal (i)) g[paramTable()[i].id] = engine.getParam ((P) i);
    j["globals"] = g;
    j["capture"] = s->captureFile;
    j["space"] = s->spaceFile;
    writeJsonAtomic (toPath (dataDir) / "settings.json", j);
}

// ---------------------------------------------------------------------------------------
// Songs
// ---------------------------------------------------------------------------------------
int Controller::songCount() const { return (int) s->songs.size(); }
int Controller::currentSong() const { return s->current; }
std::string Controller::songName (int i) const { return i >= 0 && i < songCount() ? s->songs[(size_t) i].name : std::string(); }

bool Controller::isPart (int i) const { return i > 0 && i < songCount() && s->songs[(size_t) i].part; }

int Controller::songNumber (int i) const
{
    int n = 0;
    for (int k = 0; k <= i && k < songCount(); ++k) if (! s->songs[(size_t) k].part) ++n;
    return n;
}

int Controller::songsOnly() const { return songNumber (songCount() - 1); }

int Controller::bank() const { return s->bank; }

void Controller::setBank (int b)
{
    s->bank = std::clamp (b, 0, 2);
    engine.setPedalBank (s->bank);
    static const char* names[] = { "GRIT", "GROOVE", "GLOW" };
    queueState();
    queueToast (names[s->bank]);
}

void Controller::applySong (int i, bool keepGoing)
{
    if (s->songs.empty()) return;
    i = std::clamp (i, 0, songCount() - 1);
    s->current = i;
    // a part of the same song: delay and reverb switched off keep their tails, so the
    // request goes in before the new settings
    if (keepGoing) engine.action (Action::Spill);
    for (const auto& [id, v] : s->songs[(size_t) i].values)
    {
        const int p = findParam (id.c_str());
        if (p >= 0) engine.setParam ((P) p, v);
    }
    engine.resetPedalJobs();
    s->bank = std::clamp ((int) std::lround (engine.getParam (P::BankStart)), 0, 2);
    engine.setPedalBank (s->bank);
    if (! keepGoing) engine.action (Action::DroneClear);   // a held drone trails off between songs
}

void Controller::selectSong (int i)
{
    if (s->songs.empty()) return;
    i = std::clamp (i, 0, songCount() - 1);
    const bool keepGoing = i != s->current && songNumber (i) == songNumber (s->current);
    applySong (i, keepGoing);
    saveSetlist();
    queueState();
    queueToast (isPart (i) ? songName (i) : songName (s->current));
}

void Controller::stepSong (int delta)
{
    const int n = songCount();
    if (n == 0) return;
    const int to = std::clamp (s->current + delta, 0, n - 1);
    if (to == s->current) { queueToast (delta > 0 ? "LAST SONG" : "FIRST SONG"); return; }
    selectSong (to);
}

bool Controller::edited() const
{
    if (s->songs.empty()) return false;
    for (const auto& [id, v] : s->songs[(size_t) s->current].values)
    {
        const int p = findParam (id.c_str());
        if (p >= 0 && std::fabs (engine.getParam ((P) p) - v) > 1.0e-4f) return true;
    }
    return false;
}

void Controller::setValue (P p, float v, bool broadcast)
{
    engine.setParam (p, v);
    if (isGlobal ((int) p)) saveSettings();
    if (broadcast)
    {
        json j { { "t", "val" }, { "id", info (p).id }, { "v", engine.getParam (p) }, { "edited", edited() } };
        queue (dumps (j));
    }
}

void Controller::setlistOp (const std::string& op, int i, int to, const std::string& name)
{
    auto& songs = s->songs;
    const int n = (int) songs.size();
    auto valid = [&] (int k) { return k >= 0 && k < n; };
    auto full = [&] (bool newSong)
    {
        if (n >= kMaxEntries) { queueToast ("40 songs and parts is the limit"); return true; }
        if (newSong && songsOnly() >= kMaxSongs) { queueToast ("15 songs is the limit"); return true; }
        return false;
    };
    auto groupEnd = [&] (int k) { while (k + 1 < n && songs[(size_t) k + 1].part) ++k; return k; };
    if (op == "add")
    {
        if (full (true)) return;
        Song ns { name.empty() ? "Song " + std::to_string (songsOnly() + 1) : name, false, defaults() };
        songs.push_back (ns);
        applySong (n);
    }
    else if (op == "copy" && valid (i))
    {
        if (full (! songs[(size_t) i].part)) return;
        Song c = songs[(size_t) i];
        c.name += " copy";
        songs.insert (songs.begin() + i + 1, c);
        if (s->current > i) ++s->current;
    }
    else if (op == "addPart" && valid (i))
    {
        // a new part at the end of i's song, starting from i's saved settings
        if (full (false)) return;
        const int end = groupEnd (i);
        int head = i;
        while (head > 0 && songs[(size_t) head].part) --head;
        Song c = songs[(size_t) i];
        c.part = true;
        c.name = name.empty() ? "Part " + std::to_string (end - head + 2) : name;
        songs.insert (songs.begin() + end + 1, c);
        if (s->current > end) ++s->current;
    }
    else if (op == "part" && valid (i) && i > 0)
    {
        // make a song a part of the one above, or a part its own song again
        if (! songs[(size_t) i].part) songs[(size_t) i].part = true;
        else { if (songsOnly() >= kMaxSongs) { queueToast ("15 songs is the limit"); return; } songs[(size_t) i].part = false; }
    }
    else if (op == "rename" && valid (i) && ! name.empty())
    {
        songs[(size_t) i].name = name.substr (0, 48);
    }
    else if (op == "delete" && valid (i))
    {
        if (n <= 1) { queueToast ("Keep at least one song"); return; }
        songs.erase (songs.begin() + i);
        songs.front().part = false;
        if (s->current == i) applySong (std::min (i, n - 2));
        else if (s->current > i) --s->current;
    }
    else if (op == "move" && valid (i) && valid (to) && i != to)
    {
        Song m = songs[(size_t) i];
        songs.erase (songs.begin() + i);
        songs.insert (songs.begin() + to, m);
        if (s->current == i) s->current = to;
        else if (i < s->current && to >= s->current) --s->current;
        else if (i > s->current && to <= s->current) ++s->current;
        songs.front().part = false;
    }
    else if (op == "save")
    {
        auto& vals = songs[(size_t) s->current].values;
        for (auto& [id, v] : vals)
        {
            const int p = findParam (id.c_str());
            if (p >= 0) v = engine.getParam ((P) p);
        }
        queueToast ("SAVED");
    }
    else if (op == "revert")
    {
        applySong (s->current);
    }
    else return;
    saveSetlist();
    queueState();
}

// ---------------------------------------------------------------------------------------
// Footswitches, in three banks. The bank sets what taps A to C and the pedal do:
//   Grit:   A Edge/Drive, B Bitrot, C Growl/Cut flip; pedal: dirt blend
//   Groove: A auto filter, B Dimension, C wah; pedal: wah, or volume with the wah off
//   Glow:   A Speckle, B delay, C reverb; pedal: Bloom
// Every bank: tap D drone. Hold A next bank, B next song or part, C tuner, D slap.
// Hold C and D together: Keep. Changing bank never changes the sound.
// ---------------------------------------------------------------------------------------
void Controller::footswitchPress (int sw, double now) { s->logic.press (sw, now); }
void Controller::footswitchRelease (int sw, double now) { s->logic.release (sw, now); }
void Controller::tick (double now)
{
    s->logic.tick (now);
}

bool Controller::tunerOn() const { return engine.meters().tunerOn; }

void Controller::onFootswitch (int sw, int ev)
{
    using E = FootswitchLogic::Event;
    const bool tuning = tunerOn();
    if (ev == E::ComboHoldCD) { keep(); return; }
    auto toggle = [&] (P p) { setValue (p, value (p) > 0.5f ? 0.0f : 1.0f); };
    if (ev == E::Tap)
    {
        if (tuning) { engine.action (Action::TunerOff); queueState(); return; }   // any tap leaves the tuner
        if (sw == 3)
        {
            // tap to hold the note you're playing, tap again to let it trail off
            const double now = std::chrono::duration<double> (std::chrono::steady_clock::now().time_since_epoch()).count();
            // a held drone that has died away on its own (Sustain under full) counts as off
            if (s->droneHeld && now - s->lastDTap > 2.0 && engine.meters().droneLayers == 0) s->droneHeld = false;
            if (s->droneHeld)
            {
                engine.action (Action::DroneClear);
                queueToast ("DRONE OFF");
                s->droneHeld = false;
            }
            else
            {
                engine.action (Action::DroneCapture);
                queueToast ("DRONE");
                s->droneHeld = true;
            }
            s->lastDTap = now;
            return;
        }
        if (sw < 0 || sw > 2) return;
        if (s->bank == 0)
        {
            if (sw == 0)
            {
                const bool on = value (P::DriveOn) > 0.5f;
                const int voice = (int) std::lround (value (P::DriveVoice));
                const bool withOff = std::lround (value (P::FsACycle)) == 1;
                if (! on) { setValue (P::DriveVoice, 0); setValue (P::DriveOn, 1); }
                else if (voice == 0) setValue (P::DriveVoice, 1);
                else if (withOff) { setValue (P::DriveOn, 0); setValue (P::DriveVoice, 0); }
                else setValue (P::DriveVoice, 0);
            }
            else if (sw == 1) toggle (P::BitrotOn);
            else toggle (P::PreFlip);
        }
        else if (s->bank == 1)
        {
            if (sw == 0) toggle (P::FiltOn);
            else if (sw == 1) toggle (P::DimOn);
            else toggle (P::WahOn);
        }
        else
        {
            if (sw == 0) toggle (P::GrOn);
            else if (sw == 1) toggle (P::DlOn);
            else toggle (P::RvOn);
        }
        return;
    }
    if (ev == E::HoldStart)
    {
        switch (sw)
        {
            case 0: if (! tuning) setBank ((s->bank + 1) % 3); break;
            case 1: if (! tuning) stepSong (1); break;
            case 2: engine.action (Action::TunerToggle); queueState(); break;
            case 3: if (! tuning) setValue (P::CompSlap, value (P::CompSlap) > 0.5f ? 0.0f : 1.0f); break;
            default: break;
        }
    }
}

// ---------------------------------------------------------------------------------------
// Keep
// ---------------------------------------------------------------------------------------
void Controller::keep()
{
    json e;
    e["plugin"] = "Keystone";
    e["version"] = kVersion;
    e["preset"] = songName (s->current);
    e["song"] = songNumber (s->current);
    e["part"] = isPart (s->current);
    e["edited"] = edited();
    json params, text;
    for (int i = 0; i < kNumParams; ++i)
    {
        params[paramTable()[i].id] = engine.getParam ((P) i);
        text[paramTable()[i].id] = formatValue ((P) i, engine.getParam ((P) i));
    }
    e["parameters"] = params;
    e["parameterText"] = text;
    {
        const auto m = engine.meters();
        static const char* banks[] = { "Grit", "Groove", "Glow" };
        static const char* jobs[] = { "Dirt blend", "Wah", "Volume", "Bloom sweep" };
        e["bank"] = banks[s->bank];
        e["expression"] = { { "assignment", jobs[m.pedalTarget] }, { "position", m.pedal },
                            { "dirt", m.dirtActive ? json (m.dirt) : json() }, { "wah", m.wahActive ? json (m.wah) : json() },
                            { "volume", m.volumeActive ? json (m.volume) : json() }, { "bloom", m.bloomActive ? json (m.bloom) : json() } };
    }
    e["tempo"] = nullptr;
    e["capture"] = engine.captureName();
    e["droneLayers"] = engine.meters().droneLayers;
    if (onKeep) onKeep (dumps (e));
    queueToast ("KEPT");
}

// ---------------------------------------------------------------------------------------
// Captures
// ---------------------------------------------------------------------------------------
std::vector<std::string> Controller::listCaptures() const
{
    std::vector<std::string> out;
    std::error_code ec;
    const auto dir = toPath (capturesDir);
    if (! fs::is_directory (dir, ec)) return out;
    for (const auto& entry : fs::directory_iterator (dir, ec))
    {
        if (! entry.is_regular_file()) continue;
        auto ext = fromPath (entry.path().extension());
        std::transform (ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".nam") out.push_back (fromPath (entry.path().filename()));
    }
    std::sort (out.begin(), out.end());
    return out;
}

std::string Controller::spacesDir() const { return fromPath (toPath (capturesDir).parent_path() / "Spaces"); }

std::vector<std::string> Controller::listSpaces() const
{
    std::vector<std::string> out;
    std::error_code ec;
    const auto dir = toPath (spacesDir());
    if (! fs::is_directory (dir, ec)) return out;
    for (const auto& entry : fs::directory_iterator (dir, ec))
    {
        if (! entry.is_regular_file()) continue;
        auto ext = fromPath (entry.path().extension());
        std::transform (ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".wav") out.push_back (fromPath (entry.path().filename()));
    }
    std::sort (out.begin(), out.end());
    return out;
}

bool Controller::useSpace (const std::string& file, std::string& error)
{
    if (file.empty()) { std::string e; engine.loadSpace ("", e); s->spaceFile.clear(); saveSettings(); return true; }
    if (file.find ("..") != std::string::npos || file.find_first_of ("/\\") != std::string::npos) { error = "Bad file name"; return false; }
    if (! engine.loadSpace (fromPath (toPath (spacesDir()) / toPath (file)), error)) return false;
    s->spaceFile = file;
    saveSettings();
    return true;
}

bool Controller::useCapture (const std::string& file, std::string& error)
{
    if (file.empty()) { engine.clearCapture(); s->captureFile.clear(); saveSettings(); return true; }
    if (file.find ("..") != std::string::npos || file.find_first_of ("/\\") != std::string::npos) { error = "Bad file name"; return false; }
    if (! engine.loadCapture (fromPath (toPath (capturesDir) / toPath (file)), error)) return false;
    s->captureFile = file;
    saveSettings();
    return true;
}

// ---------------------------------------------------------------------------------------
// Link
// ---------------------------------------------------------------------------------------
void Controller::queue (const std::string& msg) { s->outbox.push_back (msg); }
void Controller::queueState() { queue (stateJson()); }
void Controller::queueToast (const std::string& text)
{
    toast = text;
    queue (dumps (json { { "t", "toast" }, { "text", text } }));
}

std::vector<std::string> Controller::takeBroadcasts()
{
    std::vector<std::string> out;
    out.swap (s->outbox);
    return out;
}

std::string Controller::schemaJson() const
{
    json j;
    j["t"] = "schema";
    j["version"] = kVersion;
    j["params"] = json::array();
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& p = paramTable()[i];
        static const char* kinds[] = { "toggle", "choice", "flip", "slider" };
        json jp { { "id", p.id }, { "label", p.label }, { "module", p.module }, { "section", p.section },
                  { "kind", kinds[(int) p.kind] }, { "min", p.min }, { "max", p.max }, { "def", p.def },
                  { "unit", p.unit }, { "log", (p.flags & Log) != 0 }, { "global", (p.flags & Global) != 0 },
                  { "hidden", (p.flags & Hidden) != 0 } };
        std::string ch = p.choices;
        json arr = json::array();
        size_t st = 0;
        while (! ch.empty())
        {
            const size_t bar = ch.find ('|', st);
            arr.push_back (ch.substr (st, bar == std::string::npos ? std::string::npos : bar - st));
            if (bar == std::string::npos) break;
            st = bar + 1;
        }
        jp["choices"] = arr;
        if (*showIf ((P) i)) jp["showIf"] = showIf ((P) i);
        j["params"].push_back (jp);
    }
    int count = 0;
    const auto* mods = moduleTable (count);
    j["modules"] = json::array();
    for (int i = 0; i < count; ++i)
        j["modules"].push_back ({ { "id", mods[i].id }, { "title", mods[i].title }, { "group", mods[i].group }, { "summary", mods[i].summary } });
    return dumps (j);
}

std::string Controller::stateJson() const
{
    json j;
    j["t"] = "state";
    json vals;
    for (int i = 0; i < kNumParams; ++i) vals[paramTable()[i].id] = engine.getParam ((P) i);
    j["values"] = vals;
    j["songs"] = json::array();
    j["parts"] = json::array();
    for (const auto& song : s->songs) { j["songs"].push_back (song.name); j["parts"].push_back (song.part); }
    j["current"] = s->current;
    j["bank"] = s->bank;
    j["maxEntries"] = kMaxEntries;
    j["edited"] = edited();
    j["maxSongs"] = kMaxSongs;
    j["tuner"] = tunerOn();
    j["capture"] = engine.captureName();
    j["captureFile"] = s->captureFile;
    j["captures"] = listCaptures();
    j["space"] = engine.spaceName();
    j["spaceFile"] = s->spaceFile;
    j["spaces"] = listSpaces();
    j["namAvailable"] = Engine::namAvailable();
    j["keeperLive"] = keeperLiveTag ? keeperLiveTag() : false;
    j["footswitch"] = footswitchStatus ? footswitchStatus() : std::string();
    j["audio"] = audioStatus ? audioStatus() : std::string();
    j["version"] = kVersion;
    return dumps (j);
}

std::string Controller::meterJson (float cpuLoad, int bufferSize, double sampleRate, double latencyMs) const
{
    const auto m = engine.meters();
    json j;
    j["t"] = "meter";
    j["out"] = std::round (gainToDb (m.outPeak) * 10) / 10;
    j["rms"] = std::round (gainToDb (m.outRms) * 10) / 10;
    j["in"] = std::round (gainToDb (m.inPeak) * 10) / 10;
    j["lim"] = std::round (m.limiterReduction * 10) / 10;
    j["bloom"] = std::round (m.bloom * 1000) / 1000;
    j["bank"] = m.pedalBank;
    j["job"] = m.pedalTarget;
    {
        const float jv[] = { m.dirtActive ? m.dirt : 1.0f, m.wahActive ? m.wah : 0.5f, m.volume, m.bloom };
        j["jobv"] = std::round (jv[m.pedalTarget] * 1000) / 1000;
        const bool act[] = { m.dirtActive, m.wahActive, m.volumeActive, m.bloomActive };
        j["jobOn"] = act[m.pedalTarget];
    }
    j["wait"] = m.waiting;
    j["pedal"] = std::round (m.pedal * 1000) / 1000;
    j["drone"] = m.droneLayers;
    j["swell"] = m.droneSwelling;
    j["grains"] = std::round (m.grainsActivity * 100) / 100;
    j["clip"] = m.clipped;
    j["light"] = m.light;
    j["cpu"] = std::round (cpuLoad * 1000) / 10;
    j["buf"] = bufferSize;
    j["sr"] = sampleRate;
    j["lat"] = std::round (latencyMs * 10) / 10;
    if (m.tunerOn)
    {
        j["tuner"] = { { "on", true }, { "note", m.tunerMidi >= 0 ? noteName (m.tunerMidi) : "" },
                       { "oct", m.tunerMidi >= 0 ? m.tunerMidi / 12 - 1 : 0 }, { "cents", std::round (m.tunerCents * 10) / 10 },
                       { "hz", std::round (m.tunerFreq * 10) / 10 }, { "valid", m.tunerMidi >= 0 } };
    }
    return dumps (j);
}

void Controller::handleMessage (const std::string& line, const std::function<void (const std::string&)>& reply)
{
    try { handleMessageInner (line, reply); }
    catch (const std::exception& e) { if (onLog) onLog (std::string ("Link: couldn't handle ") + line.substr (0, 120) + ": " + e.what()); }
}

void Controller::handleMessageInner (const std::string& line, const std::function<void (const std::string&)>& reply)
{
    json m;
    try { m = json::parse (line); }
    catch (...) { return; }
    if (! m.is_object()) return;
    const std::string t = m.value ("t", std::string());

    if (t == "hello")
    {
        reply (schemaJson());
        reply (stateJson());
    }
    else if (t == "ping") reply (dumps (json { { "t", "pong" }, { "ts", m.value ("ts", 0.0) } }));
    else if (t == "set")
    {
        const int p = findParam (m.value ("id", std::string()).c_str());
        if (p >= 0 && m.contains ("v") && m["v"].is_number()) setValue ((P) p, m["v"].get<float>());
    }
    else if (t == "stomp")
    {
        const std::string k = m.value ("k", std::string());
        if (k.size() == 1 && k[0] >= 'A' && k[0] <= 'D') onFootswitch (k[0] - 'A', (int) FootswitchLogic::Tap);
    }
    else if (t == "song") selectSong (m.value ("i", 0));
    else if (t == "songStep") stepSong (m.value ("d", 1));
    else if (t == "setlist") setlistOp (m.value ("op", std::string()), m.value ("i", s->current), m.value ("to", -1), m.value ("name", std::string()));
    else if (t == "bloom") engine.setBloom (m.value ("v", 0.0f));
    else if (t == "pedal") engine.setJob ((PedalJob) engine.meters().pedalTarget, m.value ("v", 0.0f));   // the phone's bar
    else if (t == "bank") setBank (m.value ("i", 0));
    else if (t == "tuner")
    {
        engine.action (m.value ("on", false) ? Action::TunerOn : Action::TunerOff);
        queueState();
    }
    else if (t == "keep") keep();
    else if (t == "keepNote")
    {
        if (onKeepNote) onKeepNote (m.value ("n", lastKeepNumber ? lastKeepNumber() : 0), m.value ("text", std::string()).substr (0, 500));
        queueToast ("NOTE SAVED");
    }
    else if (t == "keeperLive")
    {
        if (setKeeperLiveTag) setKeeperLiveTag (m.value ("on", false));
        queueState();
    }
    else if (t == "drone")
    {
        const std::string op = m.value ("op", std::string());
        engine.action (op == "clear" ? Action::DroneClear : Action::DroneCapture);
    }
    else if (t == "tap") engine.action (Action::DelayTap);
    else if (t == "capture")
    {
        std::string err;
        if (useCapture (m.value ("file", std::string()), err)) queueToast ("CAPTURE LOADED");
        else queueToast (err);
        queueState();
    }
    else if (t == "space")
    {
        std::string err;
        if (useSpace (m.value ("file", std::string()), err)) queueToast ("SPACE LOADED");
        else queueToast (err);
        queueState();
    }
    else if (t == "learn")
    {
        if (onLearnFootswitch) onLearnFootswitch (m.value ("sw", 0));
    }
    else if (t == "calibrate")
    {
        if (onCalibrate) onCalibrate (m.value ("step", 0));
    }
    else if (t == "state") reply (stateJson());
    else if (t == "restartAudio") { if (onRestartAudio) onRestartAudio(); queueToast ("RESTARTING AUDIO"); }
    else if (t == "asioPanel") { if (onAsioPanel) onAsioPanel(); queueToast ("ASIO PANEL ON THE LAPTOP"); }
}
} // namespace ks
