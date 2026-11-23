#pragma once
// The projector: the Successpit loop and cards on the amp grille, played by mpv on the
// laptop's second screen (or its only one, with the lid shut and Projector set to On).
// mpv runs as its own low-priority process and Keystone talks to it over mpv's IPC pipe
// from a thread of its own, so nothing here can hold up the audio.
//
// Everything lives in Documents\Keystone\Visuals: the loop (loop.mp4, or any video with
// "loop" in its name), alignment_card.png, calibration_grid.png and any clips for songs.

#include <juce_core/juce_core.h>

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

class Projector : private juce::Thread
{
public:
    explicit Projector (juce::File visualsFolder);
    ~Projector() override;

    void start();   // reads the saved setting and starts mpv if it should be on
    void stop();

    // From the phone ("proj" messages) and from song changes. Any thread.
    void handleMessage (const std::string& lineJson);
    void songVisual (const std::string& visualJson);
    void dropout();   // Keystone heard an audio dropout: black out and say so

    std::string statusJson() const;   // small, sent with the meters
    std::function<void (const juce::String&)> log;

    struct Scene { double start; const char* name; };
    static const std::vector<Scene>& scenes();

private:
    void run() override;

    // mpv's side, all on the projector thread
    bool launch();
    void quitMpv();
    void dropPipe();
    bool waitLoaded();
    bool command (const juce::var& args, juce::var* result = nullptr);
    bool getProperty (const char* name, juce::var& out);
    void setMode (const juce::String& mode);
    void applyVisual (const juce::var& v);
    void playLoopAt (double seconds, bool clearRange = true);
    void poll();
    int sceneAt (double t) const;
    juce::File loopFile() const;
    juce::String cardSource (bool grid) const;
    int pickScreen (int& count) const;
    void loadSettings();
    void saveSettings() const;

    juce::File folder;
    std::mutex queueLock;
    std::deque<std::function<void()>> jobs;
    void post (std::function<void()> job);

    std::unique_ptr<juce::ChildProcess> mpv;
    void* pipe = nullptr;   // HANDLE on Windows
    int requestId = 0;
    juce::String readBuffer;

    // what's showing; written on the projector thread, read by statusJson under statusLock
    mutable std::mutex statusLock;
    juce::String mode { "loop" };   // loop, black, align, grid, clip
    juce::String setting { "auto" };   // auto (a second screen), on, off
    juce::String clip, note, problem;
    int scene = 1, rangeA = 0, rangeB = 0, screens = 1;
    double pos = 0, loopPos = 0;
    bool running = false, dropoutBlack = false;
    juce::var pendingVisual;   // a song's visual that waits while blacked out or on a card
    double lastLaunchTry = -100;
};
