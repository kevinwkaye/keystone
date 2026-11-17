#pragma once
// Keystone controller: everything between the engine and the outside world that isn't
// audio or hardware. Song presets and the setlist, footswitch jobs, Keep, captures and
// the link protocol the phone speaks. Portable: the Windows app uses it now, the Pi later.
//
// Thread safety: call every method from one control thread (the host's message thread).

#include "Engine.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ks
{
class Controller
{
public:
    void saveGlobals() const { saveSettings(); }   // after the app changes a global setting itself
    static constexpr int kMaxSongs = 15;     // songs; their parts don't count
    static constexpr int kMaxEntries = 40;   // songs and parts together
    static constexpr const char* kVersion = "0.4.9";

    // dataDir holds setlist.json and settings.json; capturesDir holds .nam files.
    Controller (Engine& engine, std::string dataDir, std::string capturesDir);
    ~Controller();

    void load();   // setlist, settings and the saved capture

    // ---- footswitches: raw presses from any source, plus tick() regularly ----
    void footswitchPress (int sw, double nowSeconds);
    void footswitchRelease (int sw, double nowSeconds);
    void tick (double nowSeconds);

    // ---- the phone link (JSON lines) ----
    // Handles one message. Replies go to `reply` (this client); changes everyone should
    // see are queued and collected with takeBroadcasts().
    void handleMessage (const std::string& line, const std::function<void (const std::string&)>& reply);
    std::string schemaJson() const;        // parameter list and modules, sent on hello
    std::string stateJson() const;         // full state: values, songs, flags
    std::string meterJson (float cpuLoad, int bufferSize, double sampleRate, double latencyMs) const;
    std::vector<std::string> takeBroadcasts();

    // ---- host hooks ----
    std::function<void (const std::string& entryJson)> onKeep;      // host writes the Keeper entry
    std::function<void (int number, const std::string& note)> onKeepNote;
    std::function<void (int sw)> onLearnFootswitch;                   // 0..3, or 4 for expression
    std::function<void (int step)> onCalibrate;                       // 0 heel, 1 toe, 2 reset, 3 reverse
    std::function<std::string()> footswitchStatus;                   // device status line
    std::function<std::string()> audioStatus;                        // device status line
    // Keystone's own Wi-Fi: the host starts or stops it (Windows hotspot or Wi-Fi Direct,
    // the Pi's access point) and says how it's going
    std::function<void (bool on, const std::string& ssid, const std::string& pass)> onWifi;
    std::function<std::string()> wifiStatus;
    void startWifi();   // after load(), with the saved setting
    std::function<bool()> keeperLiveTag;
    std::function<void (bool)> setKeeperLiveTag;
    std::function<int()> lastKeepNumber;
    std::function<void (const std::string&)> onLog;
    std::function<void()> onRestartAudio;
    std::function<void()> onAsioPanel;   // opens the ASIO driver's own settings on the laptop
    // Update firmware: the phone sends the new release a piece at a time; once it has
    // arrived whole the host hands it to its updater. Returns "" when the update is under
    // way, otherwise what went wrong in plain words. Not set: this device can't update itself.
    std::function<std::string (const std::string& file, const std::string& version)> onFirmware;
    std::string updatesDir() const;

    // The listening pack: a stretch of a take's dry bass played through every song and part,
    // one .wav each in Recordings\Listening\<take>, beside the dry bass and the take as played.
    // Runs on its own thread; progress shows in the meter message ("listen").
    bool startListening (const std::string& take, double fromSeconds, double seconds, std::string& error);
    bool listening() const;
    std::vector<std::pair<double, std::string>> takeMarks (const std::string& take) const;
    void waitForListening();   // tests

    // the daily backup of the setlist, Kuddle presets and settings (Backups\<day>, 30 days kept)
    bool backup (const std::string& day);
    static std::string today();

    // songs
    int songCount() const;
    int currentSong() const;
    std::string songName (int i) const;
    void selectSong (int i);
    void stepSong (int delta);   // forwards or back through songs and parts, no wrap
    bool edited() const;
    bool isPart (int i) const;
    int songNumber (int i) const;   // 1-based, parts share their song's number
    int songsOnly() const;          // songs, parts not counted

    // banks: what taps A to C and the pedal do. 0 Grit, 1 Groove, 2 Glow
    int bank() const;
    void setBank (int b);

    // rigs: 0 Keystone (band), 1 Kuddle (home). Each keeps its own list and settings.
    int rig() const;
    void setRig (int r, bool announce = true);

    // Keystone's feet: 0 Stomp mode (banks), 1 Song mode (A previous song, B next part,
    // C next song, on press). C and D pressed together swap them.
    int footMode() const;
    void setFootMode (int m);
    void jumpSong (int dir);   // whole songs, skipping parts
    void tapTempo (double nowSeconds);   // the phone's Tap, sets the song's tempo
    std::string songLabel (int i) const;   // "Circus, Verse" for a part
    std::string takesJson() const;           // the recorder's takes, for the phone
    std::string firmwareMessage (const std::string& line);
    std::string defaultMessage (const std::string& line);
    float myDefault (int param) const;
    std::map<std::string, float> myDefaultsFor (int rig) const;
    static std::vector<uint8_t> fromBase64 (const std::string& in);
    static std::string toBase64 (const std::vector<uint8_t>& in);
    std::string listenMessage (const std::string& line);

    // the rehearsal recorder: marks land in the take being recorded
    void mark (const std::string& what);
    class Recorder& recorder();
private:
    void syncPairs();
public:

    // for tests
    void setValue (P p, float v, bool broadcast = true);
    float value (P p) const { return engine.getParam (p); }
    bool tunerOn() const;
    void notify (const std::string& text) { queueToast (text); }
    std::string lastToast() const { return toast; }

    struct Song;   // one entry of a list: a song, a part or a Kuddle preset

private:
    struct State;
    std::unique_ptr<State> s;
    Engine& engine;
    std::string dataDir, capturesDir;
    std::string toast;

    void onFootswitch (int sw, int event);
    void handleMessageInner (const std::string& line, const std::function<void (const std::string&)>& reply);
    void applySong (int i, bool keepGoing = false);   // keepGoing: a part of the same song
    void saveSetlist() const;
    void useBandSetlist (bool backup);   // the band's songs, built in
    void saveSettings() const;
    void queue (const std::string& msg);
    void queueState();
    void queueToast (const std::string& text);
    void keep();
    std::vector<std::string> listCaptures() const;
    bool useCapture (const std::string& file, std::string& error);
    std::string spacesDir() const;   // Spaces, next to Captures
    std::vector<std::string> listSpaces() const;
    bool useSpace (const std::string& file, std::string& error);
    void setlistOp (const std::string& op, int i, int to, const std::string& name);
};
} // namespace ks
