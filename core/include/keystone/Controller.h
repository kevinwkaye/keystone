#pragma once
// Keystone controller: everything between the engine and the outside world that isn't
// audio or hardware. Song presets and the setlist, footswitch jobs, Keep, captures and
// the link protocol the phone speaks. Portable: the Windows app uses it now, the Pi later.
//
// Thread safety: call every method from one control thread (the host's message thread).

#include "Engine.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ks
{
class Controller
{
public:
    void saveGlobals() const { saveSettings(); }   // after the app changes a global setting itself
    static constexpr int kMaxSongs = 15;
    static constexpr const char* kVersion = "0.3.2";

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
    std::function<bool()> keeperLiveTag;
    std::function<void (bool)> setKeeperLiveTag;
    std::function<int()> lastKeepNumber;
    std::function<void (const std::string&)> onLog;
    std::function<void()> onRestartAudio;
    std::function<void()> onAsioPanel;   // opens the ASIO driver's own settings on the laptop

    // songs
    int songCount() const;
    int currentSong() const;
    std::string songName (int i) const;
    void selectSong (int i);
    void stepSong (int delta);
    bool edited() const;

    // for tests
    void setValue (P p, float v, bool broadcast = true);
    float value (P p) const { return engine.getParam (p); }
    bool tunerOn() const;
    void notify (const std::string& text) { queueToast (text); }
    std::string lastToast() const { return toast; }

private:
    struct Song;
    struct State;
    std::unique_ptr<State> s;
    Engine& engine;
    std::string dataDir, capturesDir;
    std::string toast;

    void onFootswitch (int sw, int event);
    void handleMessageInner (const std::string& line, const std::function<void (const std::string&)>& reply);
    void applySong (int i);
    void saveSetlist() const;
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
