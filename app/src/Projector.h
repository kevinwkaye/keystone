#pragma once
// The projector on the Pi (0.6.0): the loop on the amp's grille, played by mpv straight to
// the HDMI port (no desktop), with Blackout and the alignment card.
//
// mpv runs as its own process at the lowest priority, kept off the last core, and Keystone
// talks to it over mpv's IPC socket from a thread of its own, so nothing here can hold up
// the audio. With no screen plugged in, or the projector set to Off, mpv isn't running.
//
// Videos live in Documents/Keystone/Visuals (Send files puts them there). The cards
// (alignment_card.png, black.png) ship with Keystone, in Visuals next to the program.
// Elsewhere (the Windows laptop) it does nothing and says so.
//
// 0.6.1: the picture reacts. mpv runs on gpu-next with keystone-react.glsl (shipped beside
// the cards), and about 30 times a second this thread reads the bass and the effects (feed),
// works out the picture's numbers (ks::visuals::Reactor) and sets them as the shader's
// options. It follows the loop scene by scene from mpv's playback position, and each scene
// can be set to Auto, Off, Subtle or Full from the phone. Speckle Hold pauses the video.
// If gpu-next won't start it falls back to gpu, then the simple output, with no reaction.

#include <juce_core/juce_core.h>
#include "keystone/Visuals.h"

#include <deque>
#include <map>
#include <functional>
#include <mutex>
#include <string>

class Projector : private juce::Thread
{
public:
    Projector (juce::File visualsFolder, juce::File cardsFolder, juce::File settingsFile);
    ~Projector() override;

    void start();
    void stop();

    // From the phone: {"t":"proj","op":"mode","mode":"loop|black|align"},
    // {"op":"setting","v":"auto|off"}, {"op":"video","file":"..."},
    // {"op":"scene","i":<0..40>,"v":"auto|off|subtle|full"}. Any thread.
    void handleMessage (const std::string& lineJson);

    juce::String statusJson() const;   // goes in Health as "proj"
    std::function<void (const juce::String&)> log;
    std::function<void()> askForPlayer;   // the Pi installs the video player from the package (Main: touches wifi.json)
    std::function<ks::visuals::Feed()> feed;   // what the bass and the effects are doing (0.6.1)
    std::function<void (bool)> listening;     // tell the engine to work out the note while we react

    static bool isVideo (const juce::File& f);

private:
    void run() override;

    void post (std::function<void()> job);
    bool launch();
    void quit();
    bool send (const juce::String& json);
    void drain();
    void show();   // loads what the mode says
    juce::File video() const;
    juce::StringArray videos() const;
    bool findScreen (juce::String& device, juce::String& connector) const;
    void load();
    void save() const;
    void setProblem (const juce::String& p);
    void react (double now);   // the reactive layer, about 30 times a second
    void takeLine (const juce::String& line);

    const juce::File folder, cards, settingsFile;
    std::mutex queueLock;
    std::deque<std::function<void()>> jobs;

    std::unique_ptr<juce::ChildProcess> mpv;
    int sock = -1;
    juce::String sockPath;
    int output = 0;   // 0 gpu-next with the reactive shader, 1 gpu, 2 the simple output
    bool asked = false;
    double startedAt = 0;
    int quickExits = 0;
    double launchedAt = 0, lastTry = -100;

    mutable std::mutex statusLock;
    juce::String mode { "loop" }, setting { "auto" }, chosen, showing, problem;
    bool running = false, screen = false, havePlayer = false;

    // the reactive layer
    ks::visuals::Reactor reactor;
    std::map<int, juce::String> sceneSettings;   // scene index: off, subtle or full (auto when missing)
    juce::String readBuf;
    double timePos = -1, duration = 0, lastReact = 0;
    int scene = -1;
    bool paused = false, reactive = false;
    juce::String lastOpts;
};
