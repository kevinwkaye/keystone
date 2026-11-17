#include "keystone/Controller.h"

#include "Analysis.h"
#include "Dsp.h"
#include "Footswitch.h"

#include <nlohmann/json.hpp>
#include "Recorder.h"
#include "Sha256.h"

#include <algorithm>
#include <regex>
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
    double backupCheckedAt = -1;
    std::string lastBackupDay;
    // an update arriving from the phone
    struct Firmware { std::unique_ptr<std::ofstream> file; std::string path, version, sha; int64_t size = 0, got = 0; int seq = 0; } fw;
    std::vector<Song> songs;
    int current = 0;
    FootswitchLogic logic;
    std::vector<std::string> outbox;
    std::string captureFile, spaceFile;
    double lastDTap = -10;
    int bank = 0;
    // the two rigs: songs and current are the playing rig's list, other the resting one's
    int rig = 0;
    std::unique_ptr<Recorder> recorder;     // rehearsal takes, in Recordings beside the setlist
    int footMode = 0;                       // Keystone's feet: 0 Stomp, 1 Song
    double lastTap = 0;                     // tap tempo
    std::array<double, 4> tapGaps {};
    int tapCount = 0;
    int undoSong = -1;                      // Song mode: where a C press came from, in case it was half of C+D
    double undoAt = -10;
    std::vector<Song> other;
    int otherCurrent = 0;
    bool droneHeld = false;
    bool dTapCaptured = false;
};

static bool isGlobal (int i) { return (paramTable()[i].flags & Global) != 0; }
static bool isKuddle (int i) { return (paramTable()[i].flags & Kuddle) != 0; }

// a Keystone song's values: everything but the globals and Kuddle's
static std::map<std::string, float> defaults()
{
    std::map<std::string, float> m;
    for (int i = 0; i < kNumParams; ++i)
        if (! isGlobal (i) && ! isKuddle (i)) m[paramTable()[i].id] = paramTable()[i].def;
    return m;
}

// a Kuddle preset's values (the Melt position is the pedal's, not saved)
static std::map<std::string, float> kuddleDefaults()
{
    std::map<std::string, float> m;
    for (int i = 0; i < kNumParams; ++i)
        if (isKuddle (i) && (P) i != P::KMelt) m[paramTable()[i].id] = paramTable()[i].def;
    return m;
}

static std::map<std::string, float> defaultsFor (int rig) { return rig == 1 ? kuddleDefaults() : defaults(); }

// Kuddle's starters, as in the plugin: Voodoo (the defaults), Khruangbin, Dub
static std::vector<std::pair<std::string, std::map<std::string, float>>> kuddleStarters()
{
    auto make = [] (std::initializer_list<std::pair<const char*, float>> changes)
    {
        auto v = kuddleDefaults();
        for (const auto& [id, x] : changes) v[std::string ("kuddle.") + id] = x;
        return v;
    };
    return {
        { "Voodoo", make ({}) },
        { "Khruangbin", make ({ { "foam", 0.65f }, { "thump", 0.7f }, { "wahOn", 0 }, { "diDrive", 0.2f }, { "diAir", 0.1f },
                                { "optoSqueeze", 0.55f }, { "optoBlend", 0.9f }, { "tapePush", 0.45f }, { "tapeWow", 0.2f }, { "tapeBump", 0.6f },
                                { "bass", 3 }, { "mids", -1 }, { "midsHz", 500 }, { "treble", 2400 },
                                { "roomOn", 1 }, { "roomType", 0 }, { "roomMix", 0.18f }, { "roomSize", 0.25f } }) },
        { "Dub", make ({ { "foam", 0.75f }, { "thump", 0.8f }, { "wahOn", 0 }, { "diDrive", 0.25f }, { "diAir", 0 }, { "optoSqueeze", 0.6f },
                         { "tapePush", 0.55f }, { "tapeBump", 0.8f }, { "bass", 5 }, { "bassHz", 70 }, { "mids", -3 }, { "midsHz", 700 }, { "treble", 1600 },
                         { "roomOn", 1 }, { "roomType", 1 }, { "roomMix", 0.25f }, { "roomDecay", 1.4f } }) },
    };
}

static const char* listFile (int rig) { return rig == 1 ? "kuddle.json" : "setlist.json"; }


// ---------------------------------------------------------------------------------------
// The band's setlist, from Kevin's notes on how he works the pedals (Nov 2026). A starting
// point to tune at rehearsal: every entry is the defaults plus a few changes.
// ---------------------------------------------------------------------------------------
using Overrides = std::vector<std::pair<const char*, float>>;

static const Overrides kClean = { { "drive.on", 0 }, { "drive.voice", 0 }, { "fs.aCycle", 1 } };
// the Blumes capture on the Drive voice
static const Overrides kBlumes = { { "drive.on", 1 }, { "drive.voice", 1 }, { "fs.aCycle", 3 } };
// the Blumes with the preamp pushed: more valve gain and grit, and more Cut so it carries
static const Overrides kPushed = { { "drive.on", 1 }, { "drive.voice", 1 }, { "fs.aCycle", 3 },
                                   { "preamp.gain", 0.6f }, { "preamp.drive", 0.45f }, { "preamp.blend", 0.65f } };
// Speckle as sparkle: Haze, an octave up, sitting back
static const Overrides kSparkle = { { "grains.on", 1 }, { "grains.effect", 3 }, { "grains.pitch", 1 }, { "grains.mix", 0.35f } };

static std::vector<std::pair<std::string, std::map<std::string, float>>> bandEntries()
{
    std::vector<std::pair<std::string, std::map<std::string, float>>> out;
    auto add = [&] (const std::string& name, std::initializer_list<const Overrides*> sets, const Overrides& extra = {})
    {
        auto v = defaults();
        for (const auto* set : sets) for (const auto& [id, x] : *set) v[id] = x;
        for (const auto& [id, x] : extra) v[id] = x;
        out.emplace_back (name, v);
    };
    const float grit = 0, groove = 1, glow = 2;
    // a part's name starts with "/" here, so the list can say which entries are parts
    add ("The Comedian and the Boxer", { &kClean }, { { "bank.start", groove } });                 // tap A: filter for the solo
    add ("When the Circus Comes to Town", { &kPushed }, { { "bank.start", grit } });               // heavy start
    add ("/Verse", { &kClean }, { { "bank.start", glow }, { "reverb.on", 1 }, { "reverb.type", 2 }, { "reverb.mix", 0.25f },
                                  { "reverb.decay", 2.8f }, { "bloom.tails", 1 } });
    add ("/Chorus", { &kBlumes }, { { "bank.start", grit } });
    add ("/Clean", { &kClean }, { { "bank.start", grit } });
    add ("/Bridge", { &kClean, &kSparkle }, { { "bank.start", glow }, { "bitrot.on", 1 } });
    add ("/Ending", { &kPushed }, { { "bank.start", grit } });
    add ("DWHALTS", { &kClean }, { { "bank.start", grit }, { "drive.voice", 1 }, { "fs.aCycle", 3 } });   // tap A: Drive in the choruses
    add ("I'm Not a Robot", { &kClean }, { { "bank.start", groove } });                             // tap A: filter for the middle 8
    add ("Maps", { &kClean }, { { "bank.start", grit } });
    add ("Notes", { &kClean }, { { "bank.start", grit } });
    add ("They'll Believe Me", { &kClean }, { { "bank.start", grit } });
    add ("Influencer", { &kBlumes }, { { "bank.start", grit } });                                   // tap A: clean for the verses
    add ("Sluts", { &kBlumes }, { { "bank.start", grit }, { "bitrot.on", 1 }, { "preamp.gain", 0.55f } });
    add ("/Middle 32", { &kClean, &kSparkle }, { { "bank.start", glow }, { "grains.mix", 0.55f }, { "grains.activity", 0.7f },
                                                 { "reverb.on", 1 }, { "reverb.type", 5 }, { "reverb.decay", 8 }, { "reverb.mix", 0.45f },
                                                 { "delay.on", 1 }, { "delay.type", 2 }, { "delay.feedback", 0.6f }, { "delay.mix", 0.35f },
                                                 { "bloom.tails", 1 } });
    add ("/Back in", { &kBlumes }, { { "bank.start", grit }, { "bitrot.on", 1 }, { "preamp.gain", 0.55f } });
    add ("Action Films", { &kClean, &kSparkle }, { { "bank.start", glow }, { "delay.on", 1 }, { "delay.mix", 0.25f },
                                                   { "reverb.on", 1 }, { "reverb.type", 1 }, { "reverb.mix", 0.25f }, { "bloom.tails", 1 } });
    add ("/Chorus", { &kPushed }, { { "bank.start", grit } });
    add ("/Down", { &kClean, &kSparkle }, { { "bank.start", glow }, { "delay.on", 1 }, { "delay.mix", 0.25f },
                                            { "reverb.on", 1 }, { "reverb.type", 1 }, { "reverb.mix", 0.25f }, { "bloom.tails", 1 } });
    add ("/Ending", { &kPushed }, { { "bank.start", grit }, { "bitrot.on", 1 }, { "reverb.on", 1 }, { "reverb.type", 1 }, { "reverb.mix", 0.2f } });
    return out;
}

Controller::Controller (Engine& e, std::string data, std::string caps)
    : s (std::make_unique<State>()), engine (e), dataDir (std::move (data)), capturesDir (std::move (caps))
{
    s->logic.onEvent = [this] (int sw, FootswitchLogic::Event ev) { onFootswitch (sw, (int) ev); };
    s->recorder = std::make_unique<Recorder> (fromPath (toPath (dataDir) / "Recordings"));
    engine.setRecorder (s->recorder.get());
}

Controller::~Controller() { engine.setRecorder (nullptr); }

void Controller::mark (const std::string& what) { s->recorder->mark (what); }
Recorder& Controller::recorder() { return *s->recorder; }

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

// A file that's there but won't read (a power cut at the wrong moment): keep it as .broken
// and use the newest daily backup instead
static json readJsonOrBackup (const fs::path& dataDir, const char* name, const std::function<void (const std::string&)>& log)
{
    const auto p = dataDir / name;
    auto j = readJson (p);
    std::error_code ec;
    if (! j.is_null() || ! fs::exists (p, ec)) return j;
    fs::copy_file (p, fs::path (p.string() + ".broken"), fs::copy_options::overwrite_existing, ec);
    std::vector<fs::path> days;
    for (const auto& e : fs::directory_iterator (dataDir / "Backups", ec)) if (e.is_directory (ec)) days.push_back (e.path());
    std::sort (days.rbegin(), days.rend());
    for (const auto& d : days)
    {
        auto b = readJson (d / name);
        if (b.is_null()) continue;
        if (log) log (std::string (name) + " couldn't be read (kept as " + name + ".broken): using the backup from " + fromPath (d.filename()));
        return b;
    }
    if (log) log (std::string (name) + " couldn't be read and there's no backup (kept as " + name + ".broken)");
    return j;
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

// Puts a saved song's or preset's values into the engine
static void applyValues (Engine& engine, const std::map<std::string, float>& values)
{
    for (const auto& [id, v] : values)
    {
        const int p = findParam (id.c_str());
        if (p >= 0) engine.setParam ((P) p, v);
    }
}

// A saved list: the setlist (Keystone's songs and parts) or Kuddle's presets. Values not
// in the file, or from older versions that have since gone, get today's defaults.
static std::vector<Controller::Song> readList (const json& j, const std::map<std::string, float>& base, const char* fallback, int max, bool parts)
{
    std::vector<Controller::Song> out;
    if (! j.is_object() || ! j.contains ("songs") || ! j["songs"].is_array()) return out;
    const int version = j.value ("version", 1);
    for (const auto& js : j["songs"])
    {
        Controller::Song song { js.value ("name", std::string (fallback)), parts && ! out.empty() && js.value ("part", false), base };
        if (js.contains ("values") && js["values"].is_object())
            for (auto it = js["values"].begin(); it != js["values"].end(); ++it)
                if (it.value().is_number() && song.values.count (it.key())) song.values[it.key()] = it.value().get<float>();
        // setlist version 7 (0.4.0): quieter Bitrot, a shorter drone trail, a Blumes that cuts
        // through. Only values still on the old defaults move; ones you'd changed stay.
        if (parts && version < 7)
        {
            auto was = [&] (const char* id, float x) { return std::fabs (song.values[id] - x) < 1.0e-3f; };
            if (was ("bitrot.vol", 0.7f)) song.values["bitrot.vol"] = 0.4f;
            if (was ("drone.release", 4.0f)) song.values["drone.release"] = 2.0f;
            if (was ("drive.drive.presence", 2.0f)) song.values["drive.drive.presence"] = 4.0f;
            if (was ("drive.drive.lowcut", 70.0f)) song.values["drive.drive.lowcut"] = 110.0f;
            if (was ("drive.drive.level", 0.0f)) song.values["drive.drive.level"] = 1.5f;
            if (was ("preamp.blend", 0.5f) && was ("preamp.gain", 0.6f)) song.values["preamp.blend"] = 0.65f;
        }
        // setlist version 8 (0.4.2): each song has a tempo. It starts from the delay time,
        // which was the beat before, so synced delays and Speckle keep their timing.
        if (parts && version < 8) song.values["tempo.bpm"] = std::round (clampf (60000.0f / std::max (1.0f, song.values["delay.time"]), 40.0f, 240.0f) * 10.0f) / 10.0f;
        out.push_back (std::move (song));
        if ((int) out.size() >= max) break;
    }
    return out;
}

void Controller::load()
{
    // Keystone's setlist; the band's songs on a first run
    const auto sl = readJsonOrBackup (toPath (dataDir), listFile (0), onLog);
    s->songs = readList (sl, defaults(), "Song", kMaxEntries, true);
    if (s->songs.empty()) useBandSetlist (false);
    else s->current = std::clamp (sl.value ("current", 0), 0, (int) s->songs.size() - 1);

    // Kuddle's presets; the starters on a first run. Kuddle's sound is loaded too, so a
    // switch is instant.
    const auto kj = readJsonOrBackup (toPath (dataDir), listFile (1), onLog);
    s->other = readList (kj, kuddleDefaults(), "Preset", kMaxSongs, false);
    if (s->other.empty()) for (auto& [n, v] : kuddleStarters()) s->other.push_back ({ n, false, v });
    s->otherCurrent = std::clamp (kj.is_object() ? kj.value ("current", 0) : 0, 0, (int) s->other.size() - 1);
    applyValues (engine, s->other[(size_t) s->otherCurrent].values);

    // global settings
    int startRig = 0;
    const auto st = readJsonOrBackup (toPath (dataDir), "settings.json", onLog);
    if (st.is_object())
    {
        if (st.contains ("globals") && st["globals"].is_object())
            for (auto it = st["globals"].begin(); it != st["globals"].end(); ++it)
            {
                const int i = findParam (it.key().c_str());
                if (i >= 0 && isGlobal (i) && it.value().is_number()) engine.setParam ((P) i, it.value().get<float>());
            }
        s->captureFile = st.value ("capture", std::string());
        startRig = st.value ("rig", 0) == 1 ? 1 : 0;
        s->footMode = st.value ("footMode", 0) == 1 ? 1 : 0;
        s->spaceFile = st.value ("space", std::string());
    }
    s->recorder->setEnabled (value (P::RecOn) > 0.5f);

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
    mark ("Song: " + songLabel (s->current));   // the context for the first take
    if (startRig == 1) setRig (1, false);   // the last rig used
    syncPairs();

    // the updater waits for this to say the new version started
    const auto now = std::chrono::duration_cast<std::chrono::seconds> (std::chrono::system_clock::now().time_since_epoch()).count();
    std::ofstream (toPath (dataDir) / "started.json") << json { { "version", kVersion }, { "at", now } }.dump();
}

void Controller::useBandSetlist (bool backup)
{
    if (backup)
    {
        // keep what was there, next to the setlist, in case it mattered
        std::error_code ec;
        fs::copy_file (toPath (dataDir) / "setlist.json", toPath (dataDir) / "setlist before the band setlist.json",
                       fs::copy_options::overwrite_existing, ec);
    }
    s->songs.clear();
    for (auto& [name, v] : bandEntries())
    {
        Song song;
        song.part = name[0] == '/';
        song.name = song.part ? name.substr (1) : name;
        song.values = v;
        s->songs.push_back (song);
    }
    s->current = 0;
    saveSetlist();
}

void Controller::saveSetlist() const
{
    json j;
    j["version"] = 8;
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
    writeJsonAtomic (toPath (dataDir) / listFile (s->rig), j);
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
    j["rig"] = s->rig;
    j["footMode"] = s->footMode;
    writeJsonAtomic (toPath (dataDir) / "settings.json", j);
}

// ---------------------------------------------------------------------------------------
// Songs
// ---------------------------------------------------------------------------------------
int Controller::songCount() const { return (int) s->songs.size(); }
int Controller::currentSong() const { return s->current; }
std::string Controller::songName (int i) const { return i >= 0 && i < songCount() ? s->songs[(size_t) i].name : std::string(); }

// "Circus, Verse" for a part, "Circus" for a song
std::string Controller::songLabel (int i) const
{
    int head = i;
    while (head > 0 && isPart (head)) --head;
    return isPart (i) ? songName (head) + ", " + songName (i) : songName (i);
}

bool Controller::isPart (int i) const { return i > 0 && i < songCount() && s->songs[(size_t) i].part; }

int Controller::songNumber (int i) const
{
    int n = 0;
    for (int k = 0; k <= i && k < songCount(); ++k) if (! s->songs[(size_t) k].part) ++n;
    return n;
}

int Controller::songsOnly() const { return songNumber (songCount() - 1); }

int Controller::bank() const { return s->bank; }
int Controller::rig() const { return s->rig; }

void Controller::setRig (int r, bool announce)
{
    r = r == 1 ? 1 : 0;
    if (r == s->rig) return;
    saveSetlist();                           // the resting rig's list as it was
    std::swap (s->songs, s->other);
    std::swap (s->current, s->otherCurrent);
    s->rig = r;
    engine.setRig (r);
    engine.setSqueeze (false);
    syncPairs();
    if (r == 1) { engine.action (Action::DroneClear); s->droneHeld = false; }   // a held drone trails off
    if (r == 0) engine.setPedalBank (s->bank);
    saveSettings();
    if (announce) { queueState(); queueToast (r == 1 ? "KUDDLE" : "KEYSTONE"); }
    mark (r == 1 ? "Kuddle: " + songName (s->current) : "Keystone: " + songLabel (s->current));
}

void Controller::setBank (int b)
{
    s->bank = std::clamp (b, 0, 2);
    engine.setPedalBank (s->bank);
    static const char* names[] = { "GRIT", "GROOVE", "GLOW" };
    queueState();
    queueToast (names[s->bank]);
    mark (std::string ("Bank: ") + names[s->bank]);
}

void Controller::applySong (int i, bool keepGoing)
{
    if (s->songs.empty()) return;
    i = std::clamp (i, 0, songCount() - 1);
    s->current = i;
    // a part of the same song: delay and reverb switched off keep their tails, so the
    // request goes in before the new settings
    if (keepGoing) engine.action (Action::Spill);
    applyValues (engine, s->songs[(size_t) i].values);
    if (s->rig == 1) return;   // Kuddle: no banks, no drone
    engine.resetPedalJobs();
    s->bank = std::clamp ((int) std::lround (engine.getParam (P::BankStart)), 0, 2);
    engine.setPedalBank (s->bank);
    if (! keepGoing) engine.action (Action::DroneClear);   // a held drone trails off between songs
}

void Controller::selectSong (int i)
{
    if (s->songs.empty()) return;
    i = std::clamp (i, 0, songCount() - 1);
    if (s->rig == 0) mark ("Song: " + songLabel (i));
    const bool keepGoing = i != s->current && songNumber (i) == songNumber (s->current);
    applySong (i, keepGoing);
    saveSetlist();
    queueState();
    queueToast (isPart (i) ? songName (i) : songName (s->current));
}

// whole songs: to the first part of the song before or after this one
void Controller::jumpSong (int dir)
{
    const int n = songCount();
    if (n == 0) return;
    int i = s->current;
    while (i > 0 && isPart (i)) --i;                 // the head of this song
    if (dir > 0) { do ++i; while (i < n && isPart (i)); }
    else { --i; while (i > 0 && isPart (i)) --i; }
    if (i < 0 || i >= n) { queueToast (dir > 0 ? "LAST SONG" : "FIRST SONG"); return; }
    selectSong (i);
}

// Tap tempo: the average of the last few gaps, to a tenth of a BPM; a pause of two seconds
// starts afresh. A free delay follows the tap as well.
void Controller::tapTempo (double now)
{
    const double gap = s->lastTap > 0 ? (now - s->lastTap) * 1000.0 : 1.0e9;
    s->lastTap = now;
    if (gap > 2000.0) { s->tapCount = 0; return; }
    if (gap < 250.0) return;
    s->tapGaps[(size_t) (s->tapCount++ % 4)] = gap;
    const int k = std::min (s->tapCount, 4);
    double sum = 0;
    for (int i = 0; i < k; ++i) sum += s->tapGaps[(size_t) i];
    const double ms = sum / k;
    setValue (P::Tempo, (float) (std::round (600000.0 / ms) / 10.0));
    if (std::lround (value (P::DlSync)) == 0) setValue (P::DlTime, (float) ms);
}

int Controller::footMode() const { return s->footMode; }

// A and B are separate presses in Keystone's Song mode, a combo (held: swap rig) otherwise
void Controller::syncPairs() { s->logic.pairEnabled = { s->rig == 1 || s->footMode == 0, true }; }

void Controller::setFootMode (int m)
{
    s->footMode = m == 1 ? 1 : 0;
    syncPairs();
    saveSettings();
    queueState();
    queueToast (s->footMode == 1 ? "SONG MODE" : "STOMP MODE");
    mark (s->footMode == 1 ? "Song mode" : "Stomp mode");
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
    if (p == P::RecOn) s->recorder->setEnabled (v > 0.5f);
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
        Song ns { name.empty() ? (s->rig == 1 ? "Preset " : "Song ") + std::to_string (songsOnly() + 1) : name, false, defaultsFor (s->rig) };
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
    else if (op == "addPart" && valid (i) && s->rig == 0)
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
    else if (op == "part" && valid (i) && i > 0 && s->rig == 0)
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
    else if (op == "band" && s->rig == 0)
    {
        useBandSetlist (true);
        applySong (0);
        queueToast ("BAND SETLIST");
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
    // the daily backup: checked once a minute, made once a day
    if (now - s->backupCheckedAt > 60.0 || s->backupCheckedAt < 0)
    {
        s->backupCheckedAt = now;
        backup (today());
    }
}

std::string Controller::today()
{
    const auto t = std::chrono::system_clock::to_time_t (std::chrono::system_clock::now());
    std::tm tm {};
   #ifdef _WIN32
    localtime_s (&tm, &t);
   #else
    localtime_r (&t, &tm);
   #endif
    char buf[16];
    std::strftime (buf, sizeof (buf), "%Y-%m-%d", &tm);
    return buf;
}

// A copy of the setlist, Kuddle's presets and the settings in Backups\<day>, once a day,
// keeping the last 30 days. Returns true when it made one.
bool Controller::backup (const std::string& day)
{
    if (day == s->lastBackupDay) return false;
    std::error_code ec;
    const auto root = toPath (dataDir) / "Backups";
    const auto dir = root / toPath (day);
    s->lastBackupDay = day;
    if (fs::exists (dir, ec)) return false;
    bool any = false;
    for (const char* f : { "setlist.json", "kuddle.json", "settings.json" })
    {
        const auto from = toPath (dataDir) / f;
        if (! fs::exists (from, ec)) continue;
        if (! any) fs::create_directories (dir, ec);
        fs::copy_file (from, dir / f, fs::copy_options::overwrite_existing, ec);
        any = any || ! ec;
    }
    if (! any) return false;
    // the oldest go once there are more than 30
    std::vector<fs::path> days;
    for (const auto& e : fs::directory_iterator (root, ec)) if (e.is_directory (ec)) days.push_back (e.path());
    std::sort (days.begin(), days.end());
    for (size_t i = 0; i + 30 < days.size(); ++i) fs::remove_all (days[i], ec);
    if (onLog) onLog ("Backed up the setlist, Kuddle presets and settings to Backups\\" + day);
    return true;
}

bool Controller::tunerOn() const { return engine.meters().tunerOn; }

void Controller::onFootswitch (int sw, int ev)
{
    using E = FootswitchLogic::Event;
    const bool tuning = tunerOn();
    auto toggle = [&] (P p) { setValue (p, value (p) > 0.5f ? 0.0f : 1.0f); };
    auto now = [] { return std::chrono::duration<double> (std::chrono::steady_clock::now().time_since_epoch()).count(); };
    // hold A and B for a second: the other rig (Stomp mode in Keystone, any time in Kuddle)
    if (ev == E::ComboHoldAB) { if (! tuning) setRig (1 - s->rig); return; }
    if (s->rig == 1)
    {
        // Kuddle: tap A Mute, B Auto wah, C Room, D Tape. Hold A previous preset, B next,
        // C tuner, D squeeze while held. Hold C and D: Keep. No banks, no modes.
        if (ev == E::ComboHoldCD) { keep(); return; }
        if (ev == E::Tap)
        {
            if (tuning) { engine.action (Action::TunerOff); queueState(); return; }
            static const P taps[] = { P::KMuteOn, P::KWahOn, P::KRoomOn, P::KTapeOn };
            if (sw >= 0 && sw < 4) toggle (taps[sw]);
        }
        else if (ev == E::HoldStart)
        {
            if (sw == 2) { engine.action (Action::TunerToggle); queueState(); }
            else if (tuning) return;
            else if (sw == 0) stepSong (-1);
            else if (sw == 1) stepSong (1);
            else if (sw == 3) { engine.setSqueeze (true); queueToast ("SQUEEZE"); }
        }
        else if (ev == E::HoldEnd && sw == 3) engine.setSqueeze (false);
        return;
    }
    // Keystone: C and D pressed together swap Stomp and Song mode, the moment both are down
    if (ev == E::ComboCD)
    {
        // C goes first in Song mode (next song, on press): if D followed it into the combo, undo it
        if (s->footMode == 1 && s->undoSong >= 0 && now() - s->undoAt < 0.3 && s->undoSong != s->current) selectSong (s->undoSong);
        s->undoSong = -1;
        setFootMode (1 - s->footMode);
        return;
    }
    if (ev == E::ComboAB || ev == E::ComboHoldCD) return;
    if (s->footMode == 1)
    {
        // Song mode acts on press, so changes land on the beat: A previous song, B next
        // part, C next song. Tap D is still the drone, hold D slap. Nothing else holds.
        if (ev == E::Down && sw <= 2)
        {
            if (tuning) { engine.action (Action::TunerOff); queueState(); return; }
            if (sw == 0) jumpSong (-1);
            else if (sw == 1)
            {
                if (s->current + 1 < songCount() && isPart (s->current + 1)) selectSong (s->current + 1);
                else queueToast ("LAST PART");
            }
            else { s->undoSong = s->current; s->undoAt = now(); jumpSong (1); }
            return;
        }
        if (sw <= 2) return;   // their release and holds do nothing in Song mode
        if (ev == E::HoldStart) { if (! tuning) toggle (P::CompSlap); return; }
        if (ev != E::Tap) return;
    }
    if (ev == E::Tap)
    {
        if (tuning) { engine.action (Action::TunerOff); queueState(); return; }   // any tap leaves the tuner
        if (sw == 3)
        {
            // tap to hold the note you're playing, tap again to let it trail off; a held drone
            // that has died away on its own (Sustain under full) counts as off
            const double t = now();
            if (s->droneHeld && t - s->lastDTap > 2.0 && engine.meters().droneLayers == 0) s->droneHeld = false;
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
            s->lastDTap = t;
            return;
        }
        if (sw < 0 || sw > 2) return;
        if (s->bank == 0)
        {
            if (sw == 0)
            {
                // per song: Edge and Drive, Off Edge Drive, Off and Edge, or Off and Drive
                const bool on = value (P::DriveOn) > 0.5f;
                const int voice = (int) std::lround (value (P::DriveVoice));
                const int cycle = (int) std::lround (value (P::FsACycle));
                if (cycle >= 2)
                {
                    if (on) setValue (P::DriveOn, 0);
                    else { setValue (P::DriveVoice, cycle == 2 ? 0.0f : 1.0f); setValue (P::DriveOn, 1); }
                }
                else if (! on) { setValue (P::DriveVoice, 0); setValue (P::DriveOn, 1); }
                else if (voice == 0) setValue (P::DriveVoice, 1);
                else if (cycle == 1) { setValue (P::DriveOn, 0); setValue (P::DriveVoice, 0); }
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
            case 3: if (! tuning) toggle (P::CompSlap); break;
            default: break;
        }
    }
}

// ---------------------------------------------------------------------------------------
// Keep
// ---------------------------------------------------------------------------------------
void Controller::keep()
{
    mark ("Keep");
    json e;
    e["plugin"] = "Keystone";
    e["version"] = kVersion;
    e["rig"] = s->rig == 1 ? "Kuddle" : "Keystone";
    e["footMode"] = s->footMode == 1 ? "Song" : "Stomp";
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
void Controller::queue (const std::string& msg)
{
    // if nothing collects them (a stalled host), keep the newest rather than grow for ever
    if (s->outbox.size() >= 4096) s->outbox.erase (s->outbox.begin(), s->outbox.begin() + 1024);
    s->outbox.push_back (msg);
}
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
    j["rig"] = s->rig;
    j["footMode"] = s->footMode;
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

// base64 as the phone sends it (standard alphabet, padding optional, whitespace ignored)
std::vector<uint8_t> Controller::fromBase64 (const std::string& in)
{
    static const auto table = [] { std::array<int, 256> t {}; t.fill (-1);
        const char* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i) t[(size_t) (unsigned char) a[i]] = i; return t; }();
    std::vector<uint8_t> out;
    out.reserve (in.size() * 3 / 4);
    uint32_t acc = 0; int bits = 0;
    for (unsigned char c : in)
    {
        const int v = table[c];
        if (v < 0) continue;
        acc = (acc << 6) | (uint32_t) v; bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back ((uint8_t) (acc >> bits)); }
    }
    return out;
}

static json recentMarks (const Recorder& r)
{
    json a = json::array();
    for (auto& [t, what] : r.recentMarks (4)) a.push_back ({ std::round (t), what });
    return a;
}

std::string Controller::updatesDir() const { return fromPath (toPath (dataDir) / "Updates"); }

// Update firmware, a piece at a time: begin, chunk, chunk, ..., end
std::string Controller::firmwareMessage (const std::string& line)
{
    const auto m = json::parse (line, nullptr, false);
    auto& fw = s->fw;
    const auto op = m.value ("op", std::string());
    auto answer = [&] (bool ok, const std::string& why = {}) {
        json a { { "t", "fwAck" }, { "op", op }, { "seq", m.value ("seq", -1) }, { "ok", ok } };
        if (! why.empty()) a["why"] = why;
        return dumps (a);
    };
    auto fail = [&] (const std::string& why) {
        if (fw.file) { fw.file.reset(); std::error_code ec; fs::remove (toPath (fw.path), ec); }
        fw = {};
        if (onLog) onLog ("Update firmware: " + why);
        return answer (false, why);
    };
    const bool playing = s->recorder->quietSeconds() < 20.0;
    if (op == "begin")
    {
        if (! onFirmware) return fail ("This device can't update itself yet");
        const auto version = m.value ("version", std::string());
        const auto file = m.value ("file", std::string());
        static const std::regex okVersion (R"(\d{1,3}(\.\d{1,3}){1,3})");
        static const std::regex okFile (R"(Keystone-[0-9.]+(-Setup\.exe|-linux-arm64\.tar\.gz))");
        if (! std::regex_match (version, okVersion) || ! std::regex_match (file, okFile)) return fail ("That isn't a Keystone release");
        const int64_t size = m.value ("size", (int64_t) 0);
        if (size <= 0 || size > 300ll * 1024 * 1024) return fail ("That file is the wrong size");
        if (playing) return fail ("You're playing. Try again once the bass has been quiet for 20 seconds");
        std::error_code ec;
        fs::create_directories (toPath (updatesDir()), ec);
        fw = {};
        fw.path = fromPath (toPath (updatesDir()) / toPath (file));
        fw.version = version;
        fw.sha = m.value ("sha256", std::string());
        fw.size = size;
        fw.file = std::make_unique<std::ofstream> (toPath (fw.path), std::ios::binary | std::ios::trunc);
        if (! *fw.file) return fail ("Couldn't save the update on this device");
        if (onLog) onLog ("Update firmware: receiving " + file);
        return answer (true);
    }
    if (op == "chunk")
    {
        if (! fw.file) return fail ("The update started again, try once more");
        if (m.value ("seq", -1) != fw.seq) return fail ("A piece went missing, try once more");
        const auto data = fromBase64 (m.value ("data", std::string()));
        fw.got += (int64_t) data.size();
        if (fw.got > fw.size) return fail ("More arrived than expected, try once more");
        fw.file->write ((const char*) data.data(), (std::streamsize) data.size());
        if (! *fw.file) return fail ("Couldn't save the update on this device");
        ++fw.seq;
        return answer (true);
    }
    if (op == "end")
    {
        if (! fw.file) return fail ("Nothing has arrived");
        fw.file.reset();
        if (fw.got != fw.size) return fail ("The update didn't arrive whole, try once more");
        const auto sum = Sha256::ofFile (toPath (fw.path));
        if (fw.sha.empty() || sum != fw.sha) return fail ("The update didn't arrive whole, try once more");
        if (playing) return fail ("You're playing. Try again once the bass has been quiet for 20 seconds");
        const auto why = onFirmware (fw.path, fw.version);
        const auto version = fw.version;
        fw = {};
        if (! why.empty()) return answer (false, why);
        if (onLog) onLog ("Update firmware: installing " + version);
        queueToast ("UPDATING TO " + version);
        return answer (true);
    }
    return answer (false, "Unknown step");
}

std::string Controller::takesJson() const
{
    json j { { "t", "takes" }, { "list", json::array() }, { "free", s->recorder->freeBytes() } };
    int n = 0;
    for (const auto& tk : s->recorder->takes())
    {
        if (++n > 30) break;
        j["list"].push_back ({ { "name", tk.name }, { "song", s->recorder->firstSong (tk.name) }, { "seconds", std::round (tk.seconds) }, { "bytes", tk.bytes } });
    }
    return dumps (j);
}

std::string Controller::meterJson (float cpuLoad, int bufferSize, double sampleRate, double latencyMs) const
{
    const auto m = engine.meters();
    json j;
    j["t"] = "meter";
    {
        auto& r = *s->recorder;
        j["rec"] = r.recording() ? json { { "on", true }, { "s", std::round (r.takeSeconds()) }, { "pk", std::round (r.takePeakDb()) },
                                         { "clip", r.clipped() }, { "take", r.takeName() }, { "marks", recentMarks (r) } }
                                 : json { { "on", false } };
    }
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
    j["rig"] = m.rig;
    j["melt"] = std::round (m.melt * 1000) / 1000;
    j["sq"] = std::round (m.kuddleSqueezeDb * 10) / 10;
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
    else if (t == "pedal")   // the phone's bar
    {
        if (s->rig == 1) engine.setMelt (m.value ("v", 0.0f));
        else engine.setJob ((PedalJob) engine.meters().pedalTarget, m.value ("v", 0.0f));
    }
    else if (t == "fw") reply (firmwareMessage (line));
    else if (t == "recStop") { s->recorder->stopTake(); queueToast ("TAKE STOPPED"); }
    else if (t == "takes") reply (takesJson());
    else if (t == "room")
    {
        // the phone's room recording for a take, in pieces of base64
        const bool ok = s->recorder->addRoom (m.value ("take", std::string()), m.value ("seq", 0), m.value ("last", false),
                                              fromBase64 (m.value ("data", std::string())), m.value ("ext", std::string ("m4a")),
                                              m.contains ("at") && m["at"].is_number() ? m["at"].get<double>() : -1.0);
        reply (dumps (json { { "t", "roomAck" }, { "take", m.value ("take", std::string()) }, { "seq", m.value ("seq", 0) }, { "ok", ok } }));
    }
    else if (t == "footMode") setFootMode (m.value ("m", 1 - s->footMode));
    else if (t == "rig") setRig (m.value ("r", 1 - s->rig));
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
    else if (t == "tap") tapTempo (std::chrono::duration<double> (std::chrono::steady_clock::now().time_since_epoch()).count());
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
