// Keystone headless app.
//
// No window: it starts with Windows, runs with the lid closed, plays the bass through the
// audio interface, listens to the footswitches and expression pedal over USB MIDI, and
// talks to the phone over the local network. On Windows a tray icon shows its status for
// anyone with a screen. Logs go to Documents/Keystone/keystone.log.

#include "AudioHost.h"
#include "Keeper.h"
#include "LinkServer.h"
#include "PedalControl.h"

#include "keystone/Controller.h"
#include "keystone/Engine.h"

#include <juce_events/juce_events.h>

#if KEYSTONE_TRAY
#include <juce_gui_extra/juce_gui_extra.h>
#endif

#if JUCE_WINDOWS
#include <windows.h>
#endif

using namespace juce;

class KeystoneApp;
static KeystoneApp* theApp = nullptr;
static void midiSink (const MidiMessage& m);

// If Keystone ever crashes, the log says where.
static void crashHandler (void*)
{
    Logger::writeToLog ("CRASH. Stack:\n" + SystemStats::getStackBacktrace());
}

// Notices if the main thread stops answering, so a freeze shows in the log.
class Watchdog : public Thread
{
public:
    Watchdog() : Thread ("Keystone watchdog") {}
    std::atomic<int64_t> lastBeat { Time::currentTimeMillis() };
    void run() override
    {
        bool reported = false;
        while (! threadShouldExit())
        {
            wait (1000);
            const auto stalled = Time::currentTimeMillis() - lastBeat.load();
            if (stalled > 5000 && ! reported) { Logger::writeToLog ("WARNING: the main thread hasn't answered for " + String (stalled / 1000) + " s"); reported = true; }
            if (stalled < 2000 && reported) { Logger::writeToLog ("Main thread answering again"); reported = false; }
        }
    }
};

// ---------------------------------------------------------------------------------------
// Runs engine.idle() off the audio and message threads: tuner, drone analysis, freeing
// old captures.
class Housekeeping : public Thread
{
public:
    explicit Housekeeping (ks::Engine& e) : Thread ("Keystone housekeeping"), engine (e) {}
    void run() override
    {
        while (! threadShouldExit())
        {
            engine.idle();
            wait (10);
        }
    }
    ks::Engine& engine;
};

#if KEYSTONE_TRAY
// A small Pebble in the tray, with the status on hover and a menu.
class Tray : public SystemTrayIconComponent
{
public:
    std::function<String()> tooltip;
    std::function<void (int)> onMenu;

    Tray()
    {
        Image img (Image::ARGB, 32, 32, true);
        Graphics g (img);
        g.setColour (Colour (0xff4b57ff));
        g.fillEllipse (2, 3, 28, 27);
        g.setColour (Colours::white);
        g.fillEllipse (9, 10, 6, 6);
        g.fillEllipse (17, 10, 6, 6);
        g.setColour (Colour (0xff0b0b12));
        g.fillEllipse (11, 12, 3, 3);
        g.fillEllipse (19, 12, 3, 3);
        setIconImage (img, img);
        setIconTooltip ("Keystone");
    }
    void mouseEnter (const MouseEvent&) override { if (tooltip) setIconTooltip (tooltip()); }
    void mouseDown (const MouseEvent&) override
    {
        if (tooltip) setIconTooltip (tooltip());
        PopupMenu m;
        m.addSectionHeader (tooltip ? tooltip().upToFirstOccurrenceOf ("\n", false, false) : "Keystone");
        m.addItem (1, "Open the Keystone folder");
        m.addItem (2, "Open the Keepers folder");
        m.addItem (3, "Export Keepers for Claude");
        m.addSeparator();
        m.addItem (9, "Quit Keystone");
        m.showMenuAsync (PopupMenu::Options(), [this] (int r) { if (r != 0 && onMenu) onMenu (r); });
    }
};
#endif

// ---------------------------------------------------------------------------------------
class KeystoneApp : public JUCEApplicationBase, private Timer
{
public:
    const String getApplicationName() override { return "Keystone"; }
    const String getApplicationVersion() override { return ks::Controller::kVersion; }
    bool moreThanOneInstanceAllowed() override { return false; }
    void anotherInstanceStarted (const String&) override {}
    void suspended() override {}
    void resumed() override {}
    void systemRequestedQuit() override { quit(); }
    void unhandledException (const std::exception* e, const String& file, int line) override
    {
        Logger::writeToLog ("Unhandled exception " + String (e ? e->what() : "") + " at " + file + ":" + String (line));
    }

    void initialise (const String& commandLine) override
    {
        theApp = this;
        home = File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Keystone");
        captures = home.getChildFile ("Captures");
        captures.createDirectory();
        const auto readme = captures.getChildFile ("Put your NAM captures here.txt");
        if (! readme.existsAsFile())
            readme.replaceWithText ("Drop .nam capture files (NAM A1 or A2) in this folder.\r\n"
                                    "Keystone loads the one you pick on the phone (Tone, Drive, Source), or the first one here.\r\n"
                                    "With no capture, the Drive stage uses its modelled fallback.\r\n");
        logger = std::make_unique<FileLogger> (home.getChildFile ("keystone.log"), "Keystone " + getApplicationVersion() + " started", 512 * 1024);
        Logger::setCurrentLogger (logger.get());
        SystemStats::setApplicationCrashHandler (crashHandler);
        std::set_terminate ([] { Logger::writeToLog ("TERMINATE: an unhandled error stopped Keystone. Stack:\n" + SystemStats::getStackBacktrace()); std::abort(); });
        watchdog.startThread();

#if JUCE_WINDOWS
        // keep the laptop awake with the lid closed and give audio priority
        SetThreadExecutionState (ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED);
        SetPriorityClass (GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#endif

        // command line: --device "part of a name" --buffer 128 --list-devices
        StringArray args;
        args.addTokens (commandLine, true);
        String wantDevice;
        int wantBuffer = 0;
        for (int i = 0; i < args.size(); ++i)
        {
            if (args[i] == "--device" && i + 1 < args.size()) wantDevice = args[++i].unquoted();
            else if (args[i] == "--buffer" && i + 1 < args.size()) wantBuffer = args[++i].getIntValue();
        }

        pedals.load();
        controller = std::make_unique<ks::Controller> (engine, home.getFullPathName().toStdString(), captures.getFullPathName().toStdString());
        wireController();
        controller->load();

        audio = std::make_unique<AudioHost> (engine, keeper);
        if (args.contains ("--list-devices")) Logger::writeToLog ("Audio devices:\n" + audio->deviceList());
        audio->start (wantDevice, wantBuffer);

        midi = std::make_unique<DirectMidiInput>();
        midi->setSink (&midiSink);
        midi->setWanted (pedals.footDevice);

        housekeeping = std::make_unique<Housekeeping> (engine);
        housekeeping->startThread (Thread::Priority::low);

        controller->onLog = [] (const std::string& msg) { Logger::writeToLog (String (msg)); };
        link.onMessage = [this] (int client, const String& line)
        {
            try
            {
                controller->handleMessage (line.toStdString(), [this, client] (const std::string& r) { link.send (client, String::fromUTF8 (r.c_str())); });
                flush();
            }
            catch (const std::exception& e) { Logger::writeToLog ("Link: error handling a message: " + String (e.what())); }
        };
        link.onConnect = [] (int client) { Logger::writeToLog ("Link: phone " + String (client) + " ready"); };
        link.onDisconnect = [] (int client) { Logger::writeToLog ("Link: phone " + String (client) + " left"); };
        link.start();

#if KEYSTONE_TRAY
        tray = std::make_unique<Tray>();
        tray->tooltip = [this] { return statusText(); };
        tray->onMenu = [this] (int item)
        {
            if (item == 1) home.startAsProcess();
            else if (item == 2) Keeper::folder().startAsProcess();
            else if (item == 3) { auto z = keeper.exportZip(); if (z.existsAsFile()) z.revealToUser(); }
            else if (item == 9) quit();
        };
#endif
        startTimer (10);
        Logger::writeToLog ("Ready. " + statusText().replace ("\n", ". "));
    }

    void shutdown() override
    {
        stopTimer();
        watchdog.stopThread (2000);
        link.stop();
        if (audio) audio->stop();
        if (housekeeping) housekeeping->stopThread (2000);
        midi.reset();
#if KEYSTONE_TRAY
        tray.reset();
#endif
        audio.reset();
        controller.reset();
        theApp = nullptr;
        Logger::writeToLog ("Keystone stopped");
        Logger::setCurrentLogger (nullptr);
        logger.reset();
#if JUCE_WINDOWS
        SetThreadExecutionState (ES_CONTINUOUS);
#endif
    }

    // ---- MIDI, from the MIDI thread ----
    void handleMidi (const MidiMessage& m)
    {
        if (! m.isController()) return;
        const int cc = m.getControllerNumber(), val = m.getControllerValue(), ch = m.getChannel();

        // Learn: the next controller moved takes the job
        const int learning = learnTarget.load();
        if (learning >= 0 && (learning == 4 || val >= 64))
        {
            learnTarget.store (-1);
            MessageManager::callAsync ([this, learning, ch, cc]
            {
                if (learning == 4) { pedals.expChannel = ch; pedals.expCC = cc; }
                else { pedals.fsChannel[(size_t) learning] = ch; pedals.fsCC[(size_t) learning] = cc; }
                pedals.save();
                controller->notify ((learning == 4 ? String ("EXPRESSION LEARNT") : String ("FOOTSWITCH ") + String::charToString ((juce_wchar) ('A' + learning)) + " LEARNT").toStdString());
                flush();
            });
            return;
        }

        // Expression: the Bloom sweep, after heel and toe calibration
        if (cc == pedals.expCC)
        {
            lastExpRaw.store (val);
            engine.setBloom (pedals.expressionValue (val));
            return;
        }
        // Footswitches: plain press and release. Read directly, any channel counts.
        for (int sw = 0; sw < 4; ++sw)
        {
            if (cc != pedals.fsCC[(size_t) sw]) continue;
            const bool down = val >= 64;
            const double now = Time::getMillisecondCounterHiRes() * 0.001;
            MessageManager::callAsync ([this, sw, down, now]
            {
                if (down) controller->footswitchPress (sw, now);
                else controller->footswitchRelease (sw, now);
                flush();
            });
            return;
        }
        // Keeper on a learnt switch of its own
        if (pedals.keeperCC >= 0 && cc == pedals.keeperCC && val >= 64)
            MessageManager::callAsync ([this] { controller->handleMessage ("{\"t\":\"keep\"}", [] (const std::string&) {}); flush(); });
    }

private:
    void wireController()
    {
        controller->onKeep = [this] (const std::string& entryJson)
        {
            auto v = JSON::parse (String (entryJson));
            if (auto* obj = v.getDynamicObject())
            {
                DynamicObject::Ptr e (obj);
                e->setProperty ("date", Time::getCurrentTime().toISO8601 (true));
                e->setProperty ("context", pedals.keeperLive ? "Live" : "Studio");
                e->setProperty ("host", "Keystone app");
                if (audio) e->setProperty ("audioDevice", audio->status());
                keeper.keep (e);
            }
        };
        controller->onKeepNote = [this] (int number, const std::string& note) { keeper.addNote (number > 0 ? number : keeper.lastNumber(), String (note)); };
        controller->lastKeepNumber = [this] { return keeper.lastNumber(); };
        controller->keeperLiveTag = [this] { return pedals.keeperLive; };
        controller->setKeeperLiveTag = [this] (bool live) { pedals.keeperLive = live; pedals.save(); };
        controller->onLearnFootswitch = [this] (int sw)
        {
            learnTarget.store (juce::jlimit (0, 4, sw));
            controller->notify ((sw == 4 ? String ("MOVE THE PEDAL") : String ("PRESS FOOTSWITCH ") + String::charToString ((juce_wchar) ('A' + sw))).toStdString());
        };
        controller->onCalibrate = [this] (int step)
        {
            if (step == 0) { pedals.heel = lastExpRaw.load(); controller->notify ("HEEL SET"); }
            else if (step == 1) { pedals.toe = lastExpRaw.load(); controller->notify ("TOE SET"); }
            else { pedals.heel = 0; pedals.toe = 127; pedals.resetFootswitches(); pedals.expCC = 11; controller->notify ("PEDALS RESET"); }
            pedals.save();
        };
        controller->footswitchStatus = [this] { return midi ? midi->status().toStdString() : std::string(); };
        controller->audioStatus = [this] { return audio ? audio->status().toStdString() : std::string(); };
    }

    void flush()
    {
        for (auto& b : controller->takeBroadcasts()) link.broadcast (String::fromUTF8 (b.c_str()));
    }

    String statusText() const
    {
        return "Keystone " + String (ks::Controller::kVersion) + "\n"
               + (audio ? audio->status() : String()) + "\n"
               + "Footswitches: " + (midi ? midi->status() : String()) + "\n"
               + "Phones connected: " + String (link.clientCount()) + " (" + link.addresses() + ")\n"
               + "Song: " + String (controller->songName (controller->currentSong()));
    }

    void timerCallback() override
    {
        watchdog.lastBeat.store (Time::currentTimeMillis());
        try { tickSafely(); }
        catch (const std::exception& e) { Logger::writeToLog ("Error in the main loop: " + String (e.what())); }
    }

    void tickSafely()
    {
        const double now = Time::getMillisecondCounterHiRes() * 0.001;
        if (++heartbeatTick >= 6000)   // once a minute
        {
            heartbeatTick = 0;
            Logger::writeToLog ("Still running. Phones connected: " + String (link.clientCount()) + ". " + (audio ? audio->status() : String())
                                + ". CPU " + String (audio ? audio->cpuLoad() * 100.0f : 0.0f, 1) + "%");
        }
        controller->tick (now);
        flush();
        // meters about 25 times a second
        if (++meterTick >= 4)
        {
            meterTick = 0;
            if (link.clientCount() > 0)
                link.broadcast (String::fromUTF8 (controller->meterJson (audio->cpuLoad(), audio->bufferSize(), audio->sampleRate(), audio->latencyMs()).c_str()), true);
        }
        // pedal settings saved by another Flip Fella plugin reload within a couple of seconds
        if (++settingsTick >= 200)
        {
            settingsTick = 0;
            const auto t = PedalSettings::file().getLastModificationTime();
            if (t != lastPedalFileTime) { lastPedalFileTime = t; pedals.load(); midi->setWanted (pedals.footDevice); }
#if JUCE_WINDOWS
            SetThreadExecutionState (ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED);
#endif
        }
    }

    File home, captures;
    std::unique_ptr<FileLogger> logger;
    ks::Engine engine;
    Keeper keeper;
    PedalSettings pedals;
    std::unique_ptr<ks::Controller> controller;
    std::unique_ptr<AudioHost> audio;
    std::unique_ptr<DirectMidiInput> midi;
    std::unique_ptr<Housekeeping> housekeeping;
    LinkServer link;
#if KEYSTONE_TRAY
    std::unique_ptr<Tray> tray;
#endif
    std::atomic<int> learnTarget { -1 }, lastExpRaw { 0 };
    int meterTick = 0, settingsTick = 0, heartbeatTick = 0;
    Watchdog watchdog;
    Time lastPedalFileTime;
};

static void midiSink (const MidiMessage& m)
{
    if (theApp != nullptr) theApp->handleMidi (m);
}

START_JUCE_APPLICATION (KeystoneApp)
