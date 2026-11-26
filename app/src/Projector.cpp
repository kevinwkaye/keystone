#include "Projector.h"

#if JUCE_LINUX
 #include <sys/socket.h>
 #include <sys/un.h>
 #include <unistd.h>
 #include <fcntl.h>
 #include <cerrno>
 #include <cstring>
#endif

using namespace juce;

Projector::Projector (File visualsFolder, File cardsFolder, File settings)
    : Thread ("Projector"), folder (visualsFolder), cards (cardsFolder), settingsFile (settings) {}

Projector::~Projector() { stop(); }

bool Projector::isVideo (const File& f)
{
    const auto e = f.getFileExtension().toLowerCase();
    return ! f.getFileName().startsWithChar ('.') && (e == ".mp4" || e == ".mkv" || e == ".mov" || e == ".webm");
}

void Projector::start()
{
    folder.createDirectory();
    load();
   #if JUCE_LINUX
    startThread (Thread::Priority::low);
   #else
    setProblem ("The projector runs on the Pi");
   #endif
}

void Projector::stop()
{
    signalThreadShouldExit();
    notify();
    stopThread (4000);
}

void Projector::post (std::function<void()> job)
{
    { std::lock_guard<std::mutex> l (queueLock); jobs.push_back (std::move (job)); }
    notify();
}

void Projector::setProblem (const String& p)
{
    std::lock_guard<std::mutex> l (statusLock);
    if (p != problem && p.isNotEmpty() && log) log ("Projector: " + p);
    problem = p;
}

void Projector::handleMessage (const std::string& lineJson)
{
    const auto m = JSON::parse (String::fromUTF8 (lineJson.c_str()));
    const auto op = m["op"].toString();
    post ([this, m, op]
    {
        if (op == "mode")
        {
            const auto v = m["mode"].toString();
            if (v != "loop" && v != "black" && v != "align") return;
            { std::lock_guard<std::mutex> l (statusLock); mode = v; }
            show();
        }
        else if (op == "setting")
        {
            const auto v = m["v"].toString();
            if (v != "auto" && v != "off") return;
            { std::lock_guard<std::mutex> l (statusLock); setting = v; }
            lastTry = -100;
            save();
        }
        else if (op == "scene")
        {
            const int i = (int) m["i"];
            const auto v = m["v"].toString();
            if (i < 0 || i >= (int) ks::visuals::loopScenes().size()) return;
            if (v != "auto" && v != "off" && v != "subtle" && v != "full") return;
            { std::lock_guard<std::mutex> l (statusLock); if (v == "auto") sceneSettings.erase (i); else sceneSettings[i] = v; }
            save();
        }
        else if (op == "video")
        {
            const auto name = m["file"].toString();
            if (name.containsAnyOf ("/\\") || ! isVideo (folder.getChildFile (name)) || ! folder.getChildFile (name).existsAsFile()) return;
            { std::lock_guard<std::mutex> l (statusLock); chosen = name; mode = "loop"; }
            save();
            show();
        }
    });
}

StringArray Projector::videos() const
{
    StringArray out;
    for (const auto& f : folder.findChildFiles (File::findFiles, false))
        if (isVideo (f)) out.add (f.getFileName());
    out.sort (true);
    return out;
}

// the one to loop: the one picked on the phone, or one with "loop" in its name, or the first
File Projector::video() const
{
    const auto all = videos();
    String c;
    { std::lock_guard<std::mutex> l (statusLock); c = chosen; }
    if (c.isNotEmpty() && all.contains (c)) return folder.getChildFile (c);
    for (const auto& n : all) if (n.containsIgnoreCase ("loop")) return folder.getChildFile (n);
    return all.isEmpty() ? File() : folder.getChildFile (all[0]);
}

juce::String Projector::statusJson() const
{
    const auto all = videos();
    std::lock_guard<std::mutex> l (statusLock);
    auto* o = new DynamicObject();
    o->setProperty ("on", running);
    o->setProperty ("setting", setting);
    o->setProperty ("mode", mode);
    o->setProperty ("video", showing);
    o->setProperty ("chosen", chosen);
    Array<var> list;
    for (const auto& n : all) list.add (n);
    o->setProperty ("videos", list);
    o->setProperty ("screen", screen);
    o->setProperty ("player", havePlayer);
    o->setProperty ("problem", problem);
    // the reactive layer (0.6.1)
    o->setProperty ("reactive", reactive);
    o->setProperty ("scene", scene);
    if (scene >= 0)
    {
        const auto& sc = ks::visuals::loopScenes()[(size_t) scene];
        o->setProperty ("sceneName", String::fromUTF8 (sc.name));
        o->setProperty ("family", ks::visuals::familyName (sc.family));
    }
    auto* ss = new DynamicObject();
    for (const auto& [i, v] : sceneSettings) ss->setProperty (String (i), v);
    o->setProperty ("scenes", var (ss));
    return JSON::toString (var (o), true);
}

void Projector::load()
{
    const auto v = JSON::parse (settingsFile);
    std::lock_guard<std::mutex> l (statusLock);
    if (v["setting"].toString() == "off") setting = "off";
    chosen = v["video"].toString();
    if (auto* ss = v["scenes"].getDynamicObject())
        for (const auto& p : ss->getProperties())
        {
            const int i = p.name.toString().getIntValue();
            const auto val = p.value.toString();
            if (i >= 0 && i < (int) ks::visuals::loopScenes().size() && (val == "off" || val == "subtle" || val == "full")) sceneSettings[i] = val;
        }
}

void Projector::save() const
{
    auto* o = new DynamicObject();
    {
        std::lock_guard<std::mutex> l (statusLock);
        o->setProperty ("setting", setting);
        o->setProperty ("video", chosen);
        auto* ss = new DynamicObject();
        for (const auto& [i, v] : sceneSettings) ss->setProperty (String (i), v);
        o->setProperty ("scenes", var (ss));
    }
    settingsFile.replaceWithText (JSON::toString (var (o)));
}

// ---------------------------------------------------------------------------------------
// the projector thread (Linux)

#if JUCE_LINUX

// an HDMI port with something plugged in: /sys/class/drm/card1-HDMI-A-1/status says "connected"
bool Projector::findScreen (String& device, String& connector) const
{
    for (const auto& d : File ("/sys/class/drm").findChildFiles (File::findDirectories, false, "card*-*"))
    {
        if (d.getChildFile ("status").loadFileAsString().trim() != "connected") continue;
        const auto name = d.getFileName();   // card1-HDMI-A-1
        device = "/dev/dri/" + name.upToFirstOccurrenceOf ("-", false, false);
        connector = name.fromFirstOccurrenceOf ("-", false, false);
        return true;
    }
    return false;
}

bool Projector::launch()
{
    String device, connector;
    if (! findScreen (device, connector)) return false;
    sockPath = File::getSpecialLocation (File::tempDirectory).getChildFile ("keystone-mpv-" + String (getpid())).getFullPathName();
    ::unlink (sockPath.toRawUTF8());

    StringArray args;
    // lowest priority there is, and never on the last core, so the audio always comes first
    if (File ("/usr/bin/chrt").existsAsFile()) args.addArray ({ "/usr/bin/chrt", "--idle", "0" });
    if (File ("/usr/bin/taskset").existsAsFile() && SystemStats::getNumCpus() >= 4)
        args.addArray ({ "/usr/bin/taskset", "-c", "0-" + String (SystemStats::getNumCpus() - 2) });
    args.add ("/usr/bin/mpv");
    // gpu-next carries the reactive shader (its options can change while it plays); gpu and
    // the simple output are the fallbacks, with no reaction
    const auto shader = cards.getChildFile ("keystone-react.glsl");
    if (output == 0)
    {
        args.addArray ({ "--vo=gpu-next", "--gpu-context=drm" });
        if (shader.existsAsFile()) args.add ("--glsl-shaders=" + shader.getFullPathName());
    }
    else if (output == 1) args.addArray ({ "--vo=gpu", "--gpu-context=drm" });
    else args.add ("--vo=drm");
    args.addArray ({ "--drm-device=" + device, "--drm-connector=" + connector,
                     "--hwdec=no", "--no-audio", "--idle=yes", "--force-window=yes", "--keep-open=always",
                     "--loop-file=inf", "--image-display-duration=inf", "--no-osc", "--osd-level=0",
                     "--no-input-default-bindings", "--input-terminal=no", "--really-quiet",
                     "--input-ipc-server=" + sockPath });
    mpv = std::make_unique<ChildProcess>();
    if (! mpv->start (args, 0)) { mpv.reset(); setProblem ("Couldn't start the video player"); return false; }
    launchedAt = Time::getMillisecondCounterHiRes() * 0.001;

    // mpv makes its socket once it's up
    for (int i = 0; i < 50 && sock < 0 && mpv->isRunning() && ! threadShouldExit(); ++i)
    {
        wait (100);
        const int s = ::socket (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (s < 0) continue;
        sockaddr_un a {};
        a.sun_family = AF_UNIX;
        std::strncpy (a.sun_path, sockPath.toRawUTF8(), sizeof (a.sun_path) - 1);
        if (::connect (s, (sockaddr*) &a, sizeof (a)) == 0) { ::fcntl (s, F_SETFL, O_NONBLOCK); sock = s; }
        else ::close (s);
    }
    if (sock < 0) { quit(); return false; }
    // where the loop is, so the reaction can follow it scene by scene
    send ("{\"command\":[\"observe_property\",1,\"time-pos\"]}");
    send ("{\"command\":[\"observe_property\",2,\"duration\"]}");
    {
        std::lock_guard<std::mutex> l (statusLock);
        running = true;
        reactive = output == 0 && shader.existsAsFile() && (bool) feed;
    }
    timePos = -1; duration = 0; paused = false; lastOpts = {};
    if (listening) listening (reactive);
    if (log) log ("Projector: playing on " + connector + (output == 0 ? (reactive ? " (reacting)" : "") : output == 1 ? " (no reaction)" : " (simple output)"));
    return true;
}

void Projector::quit()
{
    if (sock >= 0) { send ("{\"command\":[\"quit\"]}"); ::close (sock); sock = -1; }
    if (mpv)
    {
        for (int i = 0; i < 20 && mpv->isRunning(); ++i) Thread::sleep (50);
        if (mpv->isRunning()) mpv->kill();
        mpv.reset();
    }
    if (sockPath.isNotEmpty()) ::unlink (sockPath.toRawUTF8());
    if (listening) listening (false);
    std::lock_guard<std::mutex> l (statusLock);
    running = false;
    reactive = false;
    scene = -1;
    showing = {};
}

bool Projector::send (const String& json)
{
    if (sock < 0) return false;
    const auto line = json + "\n";
    const char* p = line.toRawUTF8();
    size_t left = std::strlen (p);
    for (int tries = 0; left > 0 && tries < 50; ++tries)
    {
        const auto n = ::send (sock, p, left, MSG_NOSIGNAL);
        if (n > 0) { p += n; left -= (size_t) n; continue; }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { Thread::sleep (2); continue; }
        return false;
    }
    return left == 0;
}

void Projector::drain()   // mpv's replies and events: the playback position and length, the rest dropped
{
    char buf[4096];
    for (;;)
    {
        if (sock < 0) return;
        const auto n = ::recv (sock, buf, sizeof (buf), MSG_DONTWAIT);
        if (n <= 0) break;
        readBuf += String::fromUTF8 (buf, (int) n);
        for (int nl = readBuf.indexOfChar ('\n'); nl >= 0; nl = readBuf.indexOfChar ('\n'))
        {
            takeLine (readBuf.substring (0, nl));
            readBuf = readBuf.substring (nl + 1);
        }
        if (readBuf.length() > 65536) readBuf = {};
    }
}

void Projector::takeLine (const String& line)
{
    if (! line.contains ("property-change")) return;
    const auto v = JSON::parse (line);
    const int id = (int) v["id"];
    const auto& d = v["data"];
    if (id == 1) timePos = d.isVoid() ? -1.0 : (double) d;
    else if (id == 2) duration = d.isVoid() ? 0.0 : (double) d;
}

// The reactive layer: about 30 times a second while the loop plays on gpu-next
void Projector::react (double now)
{
    String m, playing;
    { std::lock_guard<std::mutex> l (statusLock); m = mode; playing = showing; }
    const bool looping = m == "loop" && playing.isNotEmpty();
    const double dt = lastReact > 0 ? now - lastReact : 1.0 / 30.0;
    lastReact = now;
    auto f = feed ? feed() : ks::visuals::Feed();
    if (! looping) f = ks::visuals::Feed();   // Blackout and the card stay as they are

    // the scene, when it's the Successpit loop; any other video reacts as one calm scene
    int sc = -1, family = ks::visuals::Calm;
    String set = "auto";
    if (looping && ks::visuals::isLoop (duration) && timePos >= 0)
    {
        sc = ks::visuals::sceneAt (timePos);
        family = ks::visuals::loopScenes()[(size_t) sc].family;
        std::lock_guard<std::mutex> l (statusLock);
        if (auto it = sceneSettings.find (sc); it != sceneSettings.end()) set = it->second;
    }
    { std::lock_guard<std::mutex> l (statusLock); scene = sc; }
    const auto o = reactor.step (f, (float) dt, family, looping ? ks::visuals::strengthFor (family, set.toStdString()) : 0.0f);

    // Speckle Hold freezes the picture: pause the video, and play on when it's let go
    if (looping && o.pause != paused)
    {
        paused = o.pause;
        send (String ("{\"command\":[\"set_property\",\"pause\",") + (paused ? "true" : "false") + "]}");
    }
    else if (! looping && paused) { paused = false; send ("{\"command\":[\"set_property\",\"pause\",false]}"); }

    auto num = [] (float x) { return String (x, 3); };
    const String opts = "ks_on=" + String (looping ? 1 : 0) + ",ks_bright=" + num (o.bright) + ",ks_pulse=" + num (o.pulse)
                      + ",ks_tr=" + num (o.tr) + ",ks_tg=" + num (o.tg) + ",ks_tb=" + num (o.tb) + ",ks_tint=" + num (o.tint)
                      + ",ks_glitch=" + num (o.glitch) + ",ks_seed=" + num (o.seed) + ",ks_smear=" + num (o.smear)
                      + ",ks_drift=" + num (o.drift) + ",ks_bloom=" + num (o.bloom) + ",ks_haze=" + num (o.haze);
    if (opts == lastOpts) return;
    lastOpts = opts;
    Array<var> cmd { "change-list", "glsl-shader-opts", "set", opts };
    auto* c = new DynamicObject();
    c->setProperty ("command", cmd);
    send (JSON::toString (var (c), true));
}

void Projector::show()
{
    if (sock < 0) return;
    String m;
    { std::lock_guard<std::mutex> l (statusLock); m = mode; }
    File f;
    String name;
    if (m == "black") { f = cards.getChildFile ("black.png"); name = "Blackout"; }
    else if (m == "align") { f = cards.getChildFile ("alignment_card.png"); name = "Alignment card"; }
    else { f = video(); name = f.getFileName(); }
    if (! f.existsAsFile())
    {
        f = cards.getChildFile ("black.png");
        setProblem (m == "loop" ? "No video yet: send one from Rig, Send files" : "A card is missing: update firmware");
    }
    else setProblem ({});
    Array<var> cmd { "loadfile", f.getFullPathName(), "replace" };
    auto* o = new DynamicObject();
    o->setProperty ("command", cmd);
    if (send (JSON::toString (var (o), true)))
    {
        std::lock_guard<std::mutex> l (statusLock);
        showing = (m == "loop" && f.getFileName() == "black.png") ? String() : name;
    }
}

void Projector::run()
{
    double lastCheck = -10;
    startedAt = Time::getMillisecondCounterHiRes() * 0.001;
    while (! threadShouldExit())
    {
        const double now = Time::getMillisecondCounterHiRes() * 0.001;
        if (now - lastCheck >= 2)
        {
            lastCheck = now;
            String device, connector;
            const bool hasScreen = findScreen (device, connector), player = File ("/usr/bin/mpv").existsAsFile();
            String set;
            { std::lock_guard<std::mutex> l (statusLock); screen = hasScreen; havePlayer = player; set = setting; }

            if (mpv && (! mpv->isRunning() || sock < 0))
            {
                // gone straight away twice on the GPU: use the simple output instead
                if (now - launchedAt < 8 && ++quickExits >= 2 && output < 2) { ++output; quickExits = 0; if (log) log ("Projector: falling back to " + String (output == 1 ? "gpu (no reaction)" : "the simple output")); }
                if (log) log ("Projector: the video player stopped, starting it again");
                quit();
            }
            const bool wanted = set == "auto" && hasScreen && player;
            if (! player)
            {
                // the package carries it (0.6.0, pi/player from 0.6.3): ask the Pi's helper once, a
                // little after start, and show what it says in player.txt (0.6.3)
                const auto pi = File::getSpecialLocation (File::currentExecutableFile).getSiblingFile ("pi");
                const bool carried = pi.getChildFile ("player").isDirectory() || pi.getChildFile ("debs").isDirectory();
                if (carried && ! asked && now - startedAt > 20 && askForPlayer) { asked = true; askForPlayer(); if (log) log ("Projector: asking the Pi to install the video player"); }
                const auto said = folder.getParentDirectory().getChildFile ("Updates").getChildFile ("player.txt").loadFileAsString().trim();
                if (said.startsWith ("failed")) setProblem ("The video player couldn't install (" + said.fromFirstOccurrenceOf ("failed:", false, false).trim().substring (0, 160) + "). Restart the Pi to try again");
                else if (said == "installing") setProblem ("Installing the video player, a minute or two");
                else setProblem (carried ? (asked ? "Installing the video player, a minute or two" : "The video player goes in shortly")
                                         : "The video player isn't installed: update firmware");
            }
            else if (set == "auto" && ! hasScreen && ! running) setProblem ({});
            if (wanted && ! mpv && now - lastTry > 5)
            {
                lastTry = now;
                if (launch()) show();
            }
            else if (! wanted && mpv) quit();

            // a video that arrived, or the chosen one sent again: loop it
            if (running)
            {
                String m, s;
                { std::lock_guard<std::mutex> l (statusLock); m = mode; s = showing; }
                if (m == "loop" && video().getFileName() != s) show();
            }
        }
        for (;;)
        {
            std::function<void()> job;
            { std::lock_guard<std::mutex> l (queueLock); if (jobs.empty()) break; job = std::move (jobs.front()); jobs.pop_front(); }
            job();
        }
        drain();
        bool react_;
        { std::lock_guard<std::mutex> l (statusLock); react_ = running && reactive; }
        if (react_ && sock >= 0) react (now);
        wait (react_ ? 33 : 200);
    }
    quit();
}

#else   // not the Pi: nothing to drive

bool Projector::findScreen (String&, String&) const { return false; }
bool Projector::launch() { return false; }
void Projector::quit() {}
bool Projector::send (const String&) { return false; }
void Projector::drain() {}
void Projector::takeLine (const String&) {}
void Projector::react (double) {}
void Projector::show() {}
void Projector::run() {}

#endif
