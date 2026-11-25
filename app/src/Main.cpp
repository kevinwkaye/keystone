// Keystone headless app.
//
// No window: it starts with Windows, runs with the lid closed, plays the bass through the
// audio interface, listens to the footswitches and expression pedal over USB MIDI, and
// talks to the phone over the local network. On Windows a tray icon shows its status for
// anyone with a screen. Logs go to Documents/Keystone/keystone.log.

#include "AudioHost.h"
#include "Health.h"
#include "Keeper.h"
#include "LinkServer.h"
#include "PedalControl.h"

#include "keystone/Controller.h"
#include "keystone/Engine.h"

#include <juce_events/juce_events.h>

#if KEYSTONE_TRAY
#include <juce_gui_extra/juce_gui_extra.h>
#include "StatusWindow.h"
#include "SystemHealth.h"
#endif
#include <cstdlib>
#include <deque>
#include <string>

#if JUCE_WINDOWS
#include <windows.h>
#endif

using namespace juce;

class KeystoneApp;
static KeystoneApp* theApp = nullptr;

// Keystone is a live instrument: ask Windows not to slow it down to save power (it runs
// with no window, which Windows 11 otherwise treats as a background job and moves to the
// slow cores) and to give it a high priority.
static void raiseProcessPriority()
{
#if JUCE_WINDOWS
    SetPriorityClass (GetCurrentProcess(), HIGH_PRIORITY_CLASS);
   #ifdef PROCESS_POWER_THROTTLING_CURRENT_VERSION
    PROCESS_POWER_THROTTLING_STATE state {};
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    state.StateMask = 0;   // never throttle
    const bool ok = SetProcessInformation (GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof (state)) != 0;
    Logger::writeToLog (String ("Priority: high, power saving ") + (ok ? "switched off for Keystone" : "could not be switched off"));
   #else
    Logger::writeToLog ("Priority: high");
   #endif
#endif
}
static void midiSink (const MidiMessage& m);

// Gig watchdog: start Keystone again in a few seconds and end this one. At most three times
// in ten minutes, so a fault that comes straight back doesn't go round for ever. On the Pi,
// systemd starts it again, so it only needs to end.
static void relaunchAndExit (const String& why)
{
    Logger::writeToLog ("WATCHDOG: " + why + ". Starting Keystone again.");
    const auto file = File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Keystone").getChildFile ("restarts.txt");
    const auto now = Time::currentTimeMillis();
    StringArray recent;
    for (auto& l : StringArray::fromLines (file.loadFileAsString()))
        if (l.trim().isNotEmpty() && l.getLargeIntValue() > now - 10 * 60 * 1000) recent.add (l.trim());
    if (recent.size() >= 3)
    {
        Logger::writeToLog ("WATCHDOG: three restarts in ten minutes, so Keystone stays stopped. Look above for what went wrong.");
        std::_Exit (3);
    }
    recent.add (String (now));
    file.replaceWithText (recent.joinIntoString ("\n"));
   #if JUCE_WINDOWS
    // a hidden PowerShell waits for this copy to go, then starts a new one in the tray
    const auto exe = File::getSpecialLocation (File::currentExecutableFile).getFullPathName();
    const String cmd = "powershell.exe -NoProfile -WindowStyle Hidden -Command \"Start-Sleep -Seconds 3; Start-Process -FilePath '"
                       + exe.replace ("'", "''") + "' -ArgumentList '--background','--restarted'\"";
    std::wstring line (cmd.toWideCharPointer());
    STARTUPINFOW si {};
    si.cb = sizeof (si);
    PROCESS_INFORMATION pi {};
    if (CreateProcessW (nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &si, &pi))
    {
        CloseHandle (pi.hThread);
        CloseHandle (pi.hProcess);
    }
    else Logger::writeToLog ("WATCHDOG: couldn't start the new copy (" + String ((int) GetLastError()) + ")");
    TerminateProcess (GetCurrentProcess(), 3);
   #endif
    std::_Exit (3);
}

// If Keystone ever crashes, the log says where, and the watchdog starts it again.
static void crashHandler (void*)
{
    Logger::writeToLog ("CRASH. Stack:\n" + SystemStats::getStackBacktrace());
    relaunchAndExit ("Keystone crashed");
}

// Notices if the main thread stops answering, so a freeze shows in the log. After 20 s it
// starts Keystone again: the phone, footswitches and pedal all go through that thread.
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
            if (stalled > 20000) relaunchAndExit ("the main thread hasn't answered for 20 s");
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
    std::function<void()> onShow;

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
    void mouseDown (const MouseEvent& e) override
    {
        if (tooltip) setIconTooltip (tooltip());
        if (! e.mods.isPopupMenu()) { if (onShow) onShow(); return; }
        PopupMenu m;
        m.addItem (4, "Show Keystone");
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
    void anotherInstanceStarted (const String&) override { showWindow(); }   // opened again from the Start menu
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
        const auto spaces = home.getChildFile ("Spaces");
        spaces.createDirectory();
        const auto spaceReadme = spaces.getChildFile ("Put your impulse responses here.txt");
        if (! spaceReadme.existsAsFile())
            spaceReadme.replaceWithText ("Drop .wav impulse responses (16, 24 or 32-bit, mono or stereo, any sample rate) in this folder.\r\n"
                                         "Pick one on the phone (Tone, Reverb, type Impulse). Keystone plays the first 2 seconds, folded to mono.\r\n");
        logger = std::make_unique<FileLogger> (home.getChildFile ("keystone.log"), "Keystone " + getApplicationVersion() + " started", 512 * 1024);

        Logger::setCurrentLogger (logger.get());
       #ifdef KEYSTONE_AVX2
        const String build = "AVX2 build";
       #else
        const String build = "standard build";
       #endif
        Logger::writeToLog ("Processor: " + SystemStats::getCpuModel() + ", " + String (SystemStats::getNumPhysicalCpus()) + " cores, "
                            + String (SystemStats::getCpuSpeedInMegahertz()) + " MHz, AVX " + (SystemStats::hasAVX() ? "yes" : "no")
                            + ", AVX2 " + (SystemStats::hasAVX2() ? "yes" : "no") + ", FMA " + (SystemStats::hasFMA3() ? "yes" : "no")
                            + ". Keystone " + build + ".");
        raiseProcessPriority();
        SystemStats::setApplicationCrashHandler (crashHandler);
        std::set_terminate ([] { Logger::writeToLog ("TERMINATE: an unhandled error stopped Keystone. Stack:\n" + SystemStats::getStackBacktrace()); relaunchAndExit ("an unhandled error"); });
        if (commandLine.contains ("--restarted")) Logger::writeToLog ("Started again by the watchdog");
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

        // Kaleidoscope: the pedals installed beside Keystone, and any you drop in Documents\Keystone\Pedals
        rack = std::make_unique<PedalRack>();
        {
            const auto mine = home.getChildFile ("Pedals");
            mine.createDirectory();
            Array<File> dirs { mine, File::getSpecialLocation (File::currentExecutableFile).getSiblingFile ("Pedals") };
            rack->setFolders (dirs, mine.getChildFile ("Settings"));
            Logger::writeToLog ("Kaleidoscope: " + String ((int) rack->installed().size()) + " pedals installed");
        }
        pedals.load();
        controller = std::make_unique<ks::Controller> (engine, home.getFullPathName().toStdString(), captures.getFullPathName().toStdString());
        wireController();
        controller->load();
        controller->startWifi();   // Keystone's own network, if it's switched on

        audio = std::make_unique<AudioHost> (engine, keeper, rack.get());
        if (args.contains ("--list-devices")) Logger::writeToLog ("Audio devices:\n" + audio->deviceList());
        audio->onBufferStepLearnt = [this] (int step)
        {
            engine.setParam (ks::P::OutBufferStep, (float) step);
            if (controller) controller->saveGlobals();
        };
        audio->onBufferFound = [this] (int step)
        {
            engine.setParam (ks::P::OutBufferStep, (float) step);
            // Auto, through the controller so the phone's Setup page follows
            if (controller) controller->handleMessage (R"({"t":"set","id":"output.buffer","v":0})", [] (const std::string&) {});
            if (controller) controller->saveGlobals();
        };
        audio->start (wantDevice, wantBuffer);

        midi = std::make_unique<DirectMidiInput>();
        midi->setSink (&midiSink);
        midi->setWanted (pedals.footDevice);

        housekeeping = std::make_unique<Housekeeping> (engine);
        housekeeping->startThread (Thread::Priority::low);

        controller->onLog = [] (const std::string& msg) { Logger::writeToLog (String (msg)); };
        controller->onRestartAudio = [this] { Logger::writeToLog ("Audio: restart asked for"); audio->restart(); };
        controller->onBufferFind = [this] (bool start)
        {
            // the link thread asks; the finder lives on the message thread with the audio
            MessageManager::callAsync ([this, start] { if (audio) { if (start) audio->findBestBuffer(); else audio->stopFindingBuffer(); } });
        };
        controller->bufferFinder = [this] { return audio ? audio->finderJson().toStdString() : std::string(); };
        controller->onAsioPanel = [this] { MessageManager::callAsync ([this] { audio->showDriverPanel(); }); };
        link.onMessage = [this] (int client, const String& line)
        {
            try
            {
                if (line.contains ("\"watchHello\"")) link.markWatch (client, JSON::parse (line).getProperty ("v", var()).toString());
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
        tray->onShow = [this] { showWindow(); };
        if (! args.contains ("--background")) showWindow();   // started at sign-in: stay in the tray
        tray->onMenu = [this] (int item)
        {
            if (item == 1) home.startAsProcess();
            else if (item == 2) Keeper::folder().startAsProcess();
            else if (item == 3) { auto z = keeper.exportZip(); if (z.existsAsFile()) z.revealToUser(); }
            else if (item == 4) showWindow();
            else if (item == 9) quit();
        };
#endif
        startTimer (10);
        Logger::writeToLog ("Ready. " + statusText().replace ("\n", ". "));
    }

    void shutdown() override
    {
        stopTimer();
        stopWifi();
        watchdog.stopThread (2000);
        link.stop();
        if (audio) audio->stop();
        if (rack) rack->shutdown();   // keeps the pedal's settings for next time
        if (housekeeping) housekeeping->stopThread (2000);
        midi.reset();
#if KEYSTONE_TRAY
        window.reset();
        tray.reset();
#endif
        audio.reset();
        rack.reset();
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
        // Footswitches may send controllers or notes; a note counts as controller 128 + note
        int cc, val;
        if (m.isController()) { cc = m.getControllerNumber(); val = m.getControllerValue(); }
        else if (m.isNoteOnOrOff()) { cc = 128 + m.getNoteNumber(); val = m.isNoteOn() ? 127 : 0; }
        else return;
        const int ch = m.getChannel();

        // Learn: the next controller moved takes the job
        const int learning = learnTarget.load();
        if (learning >= 0 && ((learning == 4 && cc < 128) || (learning < 4 && val >= 64)))
        {
            learnTarget.store (-1);
            MessageManager::callAsync ([this, learning, ch, cc]
            {
                if (learning == 4) { pedals.expChannel = ch; pedals.expCC = cc; }
                else { pedals.fsChannel[(size_t) learning] = ch; pedals.fsCC[(size_t) learning] = cc; pedals.fsToggle &= ~(1 << learning); }
                pedals.save();
                controller->notify ((learning == 4 ? String ("EXPRESSION LEARNT") : String ("FOOTSWITCH ") + String::charToString ((juce_wchar) ('A' + learning)) + " LEARNT").toStdString());
                flush();
            });
            return;
        }

        // Kaleidoscope with a pedal loaded: the footswitch and expression belong to the pedal,
        // exactly as if Ableton were sending it the MIDI
        if (rack && rack->takesPedals())
        {
            rack->pushMidi (m);
            if (cc == pedals.expCC && cc < 128) lastExpRaw.store (val);   // calibration still sees it
            return;
        }
        // Expression, after heel and toe calibration: the job of the bank in use (dirt, wah or
        // volume, Bloom), with soft takeover in the engine
        if (cc == pedals.expCC && cc < 128)
        {
            lastExpRaw.store (val);
            lastExpTime.store (Time::getMillisecondCounterHiRes() * 0.001);
            engine.setPedal (pedals.expressionValue (val));
            return;
        }
        // Footswitches: plain press and release. Read directly, any channel counts.
        for (int sw = 0; sw < 4; ++sw)
        {
            if (cc != pedals.fsCC[(size_t) sw]) continue;
            const double now = Time::getMillisecondCounterHiRes() * 0.001;
            if ((pedals.fsToggle >> sw) & 1)
            {
                // a latching switch: every message is one tap
                MessageManager::callAsync ([this, sw, now]
                {
                    lastFsTime = now; lastFsName = String::charToString ((juce_wchar) ('A' + sw)) + " press";
                    controller->footswitchPress (sw, now);
                    controller->footswitchRelease (sw, now + 0.05);
                    flush();
                });
                return;
            }
            const bool down = val >= 64;
            MessageManager::callAsync ([this, sw, down, now]
            {
                if (down) { lastFsTime = now; lastFsName = String::charToString ((juce_wchar) ('A' + sw)) + " press"; }
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
            else if (step == 3)
            {
                pedals.reversed = ! pedals.reversed;
                controller->notify (pedals.reversed ? "PEDAL REVERSED" : "PEDAL NORMAL");
                // move the pedal job straight away if the pedal has been used, so the flip is heard now
                if (lastExpTime.load() > 0) engine.setPedal (pedals.expressionValue (lastExpRaw.load()));
            }
            else { pedals.heel = 0; pedals.toe = 127; pedals.reversed = false; pedals.resetFootswitches(); pedals.expCC = 11; controller->notify ("PEDALS RESET"); }
            pedals.save();
        };
        // Kaleidoscope
        controller->kalInstalled = [this] { return rack ? rack->installed() : std::vector<std::string>(); };
        controller->onKalLoad = [this] (const std::string& id, std::string& error)
        {
            if (! rack) { error = "No pedals here"; return false; }
            const bool ok = rack->load (id, error);
            Logger::writeToLog ("Kaleidoscope: " + (id.empty() ? String ("pedal put away") : (ok ? "loaded " : "couldn't load ") + String (id)
                                + (ok ? String() : ": " + String (error))));
            return ok;
        };
        controller->kalParams = [this] { return rack ? rack->paramsJson() : std::string ("[]"); };
        controller->onKalSet = [this] (int i, float v) { if (rack) rack->setParam (i, v); };
        controller->onKalMode = [this] (bool on) { if (rack) rack->setMode (on); };
        rack->onChanged = [this] (const std::string& c) { controller->kalChanged (c); flush(); };
        rack->onLost = [this] (const std::string& why) { controller->kalLost (why); flush(); };
        controller->footswitchStatus = [this] { return midi ? midi->status().toStdString() : std::string(); };
        controller->audioStatus = [this] { return audio ? audio->status().toStdString() : std::string(); };
        // Keystone's own Wi-Fi. Laptop: a hidden PowerShell runs it for as long as Keystone
        // does. Pi: wifi.json for the keystone-wifi service (pi/keystone-wifi.sh), which runs
        // the access point as root.
        controller->onWifi = [this] (bool on, const std::string& ssid, const std::string& pass)
        {
           #if JUCE_WINDOWS
            stopWifi();
            if (! on) { home.getChildFile ("wifi-status.txt").replaceWithText ("Off"); return; }
            const auto script = File::getSpecialLocation (File::currentExecutableFile).getSiblingFile ("Tools").getChildFile ("Keystone Wi-Fi.ps1");
            if (! script.existsAsFile()) { home.getChildFile ("wifi-status.txt").replaceWithText ("Off: the Wi-Fi script is missing, reinstall Keystone"); return; }
            wifiProcess = std::make_unique<ChildProcess>();
            StringArray cmd { "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden", "-File", script.getFullPathName(),
                              "-Ssid", String (ssid), "-Pass", String (pass), "-StatusFile", home.getChildFile ("wifi-status.txt").getFullPathName(),
                              "-ParentPid", String ((int) GetCurrentProcessId()) };
            if (! wifiProcess->start (cmd, 0)) { wifiProcess.reset(); home.getChildFile ("wifi-status.txt").replaceWithText ("Off: couldn't start PowerShell"); }
            else Logger::writeToLog ("Wi-Fi: starting " + String (ssid));
           #else
            DynamicObject::Ptr w (new DynamicObject());
            w->setProperty ("on", on);
            w->setProperty ("ssid", String (ssid));
            w->setProperty ("pass", String (pass));
            home.getChildFile ("wifi.json").replaceWithText (JSON::toString (var (w.get())));
           #endif
        };
        controller->wifiStatus = [this] { return home.getChildFile ("wifi-status.txt").loadFileAsString().trim().toStdString(); };

       #if JUCE_WINDOWS || JUCE_LINUX
        // Update firmware. Laptop: hand the installer to the "Keystone Update" task (Tools\Update.ps1),
        // which installs it with administrator rights and puts the old version back if the
        // new one doesn't start. Pi: pi/keystone-update.sh, the same with two install slots
        controller->onFirmware = [this] (const std::string& file, const std::string& version) -> std::string
        {
            const File installer (String::fromUTF8 (file.c_str()));
           #if JUCE_WINDOWS
            if (! installer.hasFileExtension ("exe")) return "That update is for the Pi, not the laptop";
           #else
            if (! installer.getFileName().endsWith ("-linux-arm64.tar.gz")) return "That update is for the laptop, not the Pi";
           #endif
            auto* r = new DynamicObject();
            r->setProperty ("installer", installer.getFullPathName());
            r->setProperty ("version", String (version));
            r->setProperty ("from", String (ks::Controller::kVersion));
            if (! installer.getSiblingFile ("request.json").replaceWithText (JSON::toString (var (r))))
                return "Couldn't save the update request";
           #if JUCE_LINUX
            // Pi: keystone-update.path sees request.json and runs the updater (A/B slots)
            Logger::writeToLog ("Update firmware: asked the Pi's updater for " + installer.getFileName());
            return {};
           #else
            ChildProcess task;
            if (! task.start (StringArray { "schtasks.exe", "/run", "/tn", "Keystone Update" }, ChildProcess::wantStdOut | ChildProcess::wantStdErr))
                return "Couldn't start the updater";
            const String said = task.readAllProcessOutput().trim();
            task.waitForProcessToFinish (10000);
            if (task.getExitCode() != 0)
            {
                Logger::writeToLog ("Update firmware: schtasks said " + said);
                installer.getSiblingFile ("request.json").deleteFile();
                // the installer stays in Documents\Keystone\Updates, ready to run by hand
                return "The laptop's updater needs setting up once: on the laptop, open Documents\\Keystone\\Updates and run "
                       + installer.getFileName().toStdString() + ". After that, updates install from the phone by themselves";
            }
            Logger::writeToLog ("Update firmware: handed " + installer.getFileName() + " to the updater");
            return {};
           #endif
        };
       #endif
    }

public:
    Health health()
    {
        Health h;
        const double now = Time::getMillisecondCounterHiRes() * 0.001;
        const auto m = engine.meters();
        h.version = ks::Controller::kVersion;
        h.song = String (controller->songName (controller->currentSong()));
        h.songNumber = controller->currentSong() + 1;
        h.uptimeSec = now - startedAt;
        h.audioOk = audio && audio->running() && audio->onAudioInterface();
        h.audioText = audio ? audio->status() : String();
        h.inDb = Decibels::gainToDecibels (m.inPeak, -100.0f);
        h.inChannel = audio ? audio->inputChannel() : 1;
        h.inputDead = h.audioOk && m.inPeak < 1.0e-6f;
        h.fsOk = midi && midi->connected();
        h.fsText = midi ? midi->status().fromFirstOccurrenceOf ("Connected: ", false, false) : String();
        if (h.fsText.isEmpty() && midi) h.fsText = midi->status();
        h.fsAgoSec = lastFsTime < 0 ? -1 : now - lastFsTime;
        h.fsLast = lastFsName;
        h.expPos = m.pedal < 0 ? 0.0f : m.pedal;
        const double et = lastExpTime.load();
        h.expAgoSec = et < 0 ? -1 : now - et;
        h.expCalibrated = pedals.heel != 0 || pedals.toe != 127 || pedals.reversed;
        h.expReversed = pedals.reversed;
        h.outDb = Decibels::gainToDecibels (m.outPeak, -100.0f);
        h.limDb = m.limiterReduction;
        h.cpu = audio ? audio->cpuLoad() : 0.0f;
        h.dropouts = audio ? audio->dropouts() : 0;
        dropHistory.push_back ({ now, h.dropouts });
        while (! dropHistory.empty() && now - dropHistory.front().first > 60) dropHistory.pop_front();
        h.dropoutsRecent = dropHistory.empty() ? 0 : h.dropouts - dropHistory.front().second;
        h.light = m.light;
        // the machine itself, read every 5 s (vcgencmd is a separate program on the Pi)
        if (now - sysReadAt >= 5.0 || sysReadAt < 0) { sys = SystemHealth::read (home); sysReadAt = now; }
        h.tempC = sys.tempC; h.fanRpm = sys.fanRpm;
        h.hasPower = sys.hasPower; h.underVoltNow = sys.underVoltNow; h.underVoltEver = sys.underVoltEver;
        h.throttledNow = sys.throttledNow; h.throttledEver = sys.throttledEver;
        h.freeMb = sys.freeMb; h.memUsedMb = sys.memUsedMb; h.memTotalMb = sys.memTotalMb;
        h.reconnects = audio ? audio->reconnects() : 0;
        h.phones = link.clientCount();
        h.phoneAddresses = link.clientAddresses().joinIntoString (", ");
        h.laptopAddresses = link.addresses();
        h.watchVersion = link.watchVersion();
        h.watchAddress = link.watchAddress();
        return h;
    }

private:
    void showWindow()
    {
#if KEYSTONE_TRAY
        if (window == nullptr)
        {
            auto* view = new status::View();
            view->getHealth = [this] { return cachedHealth; };
            view->onRestart = [this] { audio->restart(); };
            view->onOpenFolder = [this] { home.startAsProcess(); };
            view->onHide = [this] { if (window) window->setVisible (false); };
            window = std::make_unique<status::Window> (view);
        }
        window->setVisible (true);
        window->toFront (true);
#endif
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
                                + ". CPU " + String (audio ? audio->cpuLoad() * 100.0f : 0.0f, 1) + "%. Bass input peak "
                                + String (Decibels::gainToDecibels (engine.meters().inPeak, -100.0f), 0) + " dB. Dropouts so far "
                                + String (audio ? audio->dropouts() : 0) + (engine.meters().light ? ". Effects in light mode" : ""));
        }
        controller->tick (now);
        flush();
        // health once a second: the status window and the phone's Health page
        if (++healthTick >= 100)
        {
            healthTick = 0;
            cachedHealth = health();
            if (link.clientCount() > 0) link.broadcast (cachedHealth.toJson(), true);
        }
        // meters about 25 times a second
        if (++meterTick >= 4)
        {
            meterTick = 0;
            if (link.clientCount() > 0)
                link.broadcast (String::fromUTF8 (controller->meterJson (audio->cpuLoad(), audio->bufferSize(), audio->sampleRate(), audio->latencyMs()).c_str()), true, true);
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

    void stopWifi()
    {
        if (wifiProcess) { wifiProcess->kill(); wifiProcess.reset(); }
    }

    File home, captures;
    std::unique_ptr<FileLogger> logger;
    ks::Engine engine;
    Keeper keeper;
    PedalSettings pedals;
    std::unique_ptr<ks::Controller> controller;
    std::unique_ptr<ChildProcess> wifiProcess;   // the laptop's own network (Tools\Keystone Wi-Fi.ps1)
    std::unique_ptr<PedalRack> rack;   // Kaleidoscope's pedal (declared before the audio, so it outlives it)
    std::unique_ptr<AudioHost> audio;
    std::unique_ptr<DirectMidiInput> midi;
    std::unique_ptr<Housekeeping> housekeeping;
    LinkServer link;
#if KEYSTONE_TRAY
    std::unique_ptr<Tray> tray;
#endif
    std::atomic<int> learnTarget { -1 }, lastExpRaw { 0 };
    int meterTick = 0, settingsTick = 0, heartbeatTick = 0, healthTick = 95;
    double startedAt = Time::getMillisecondCounterHiRes() * 0.001;
    double lastFsTime = -1;
    SystemHealth sys;
    double sysReadAt = -1;
    String lastFsName;
    std::atomic<double> lastExpTime { -1.0 };
    std::deque<std::pair<double, int>> dropHistory;
    Health cachedHealth;
#if KEYSTONE_TRAY
    std::unique_ptr<status::Window> window;
#endif
    Watchdog watchdog;
    Time lastPedalFileTime;
};

static void midiSink (const MidiMessage& m)
{
    if (theApp != nullptr) theApp->handleMidi (m);
}

START_JUCE_APPLICATION (KeystoneApp)
