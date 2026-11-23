#include "Projector.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

using namespace juce;

// The Successpit loop (8 min 1 s): where each scene starts, from the projection brief.
const std::vector<Projector::Scene>& Projector::scenes()
{
    static const std::vector<Scene> s {
        { 0, "TV static" }, { 3, "Test card" }, { 17, "Square tunnel" }, { 33, "Blocky plasma" },
        { 47, "Copper bars" }, { 61, "Hypnotic rings" }, { 75, "Checker floor" }, { 91, "Static" },
        { 93, "Logo wave" }, { 103, "Pixel kaleidoscope" }, { 117, "Checker tunnel" }, { 133, "Oscilloscope" },
        { 145, "Starfield" }, { 159, "Synthwave sun" }, { 173, "Moiré ripples" }, { 187, "Pixel rain" },
        { 199, "Channel flicking" }, { 209, "Equaliser" }, { 221, "Logo rotozoomer" }, { 233, "Double spiral" },
        { 247, "Plasma 2" }, { 261, "Synthwave mountains" }, { 277, "Broken test card" }, { 287, "Reverse tunnel" },
        { 301, "Wobbling copper bars" }, { 313, "Checkerboard floor" }, { 325, "Static" }, { 327, "Logo wave 2" },
        { 335, "Checker tunnel 2" }, { 347, "Moiré 2" }, { 359, "Kaleidoscope 2" }, { 371, "Starfield 2" },
        { 383, "Plasma 3" }, { 397, "Equaliser 2" }, { 407, "Rings 2" }, { 419, "Mountains 2" },
        { 433, "Synthwave grid" }, { 447, "Spiral 2" }, { 459, "Channel flicking 2" }, { 467, "Logo rotozoomer 2" },
        { 477, "Static" } };
    return s;
}
static constexpr double kLoopLength = 481.0;

static double sceneStart (int n) { const auto& s = Projector::scenes(); return s[(size_t) jlimit (1, (int) s.size(), n) - 1].start; }
static double sceneEnd (int n) { const auto& s = Projector::scenes(); return n >= (int) s.size() ? kLoopLength : s[(size_t) n].start; }

Projector::Projector (File visualsFolder) : Thread ("Projector"), folder (visualsFolder) {}

Projector::~Projector() { stop(); }

void Projector::start()
{
    folder.createDirectory();
    const auto readme = folder.getChildFile ("Put the visuals here.txt");
    if (! readme.existsAsFile())
        readme.replaceWithText ("The projector plays what's in this folder on the laptop's second screen.\r\n"
                                "loop.mp4: the main loop (any video with \"loop\" in its name works too).\r\n"
                                "alignment_card.png and calibration_grid.png: the setup cards.\r\n"
                                "Any other videos: clips you can give a song on the phone (Setlist, Visuals).\r\n");
    loadSettings();
    startThread (Thread::Priority::low);
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

void Projector::handleMessage (const std::string& lineJson)
{
    const auto m = JSON::parse (String::fromUTF8 (lineJson.c_str()));
    const auto op = m["op"].toString();
    post ([this, m, op]
    {
        if (op == "mode") setMode (m["mode"].toString());
        else if (op == "scene")
        {
            { std::lock_guard<std::mutex> l (statusLock); dropoutBlack = false; pendingVisual = var(); }
            auto* o = new DynamicObject(); o->setProperty ("kind", "scene"); o->setProperty ("n", (int) m["n"]);
            applyVisual (var (o));
        }
        else if (op == "next")
        {
            auto* o = new DynamicObject(); o->setProperty ("kind", "next");
            applyVisual (var (o));
        }
        else if (op == "apply")   // Preview from the song's Visuals sheet
        {
            { std::lock_guard<std::mutex> l (statusLock); dropoutBlack = false; pendingVisual = var(); }
            if (mode != "loop" && mode != "clip") { std::lock_guard<std::mutex> l (statusLock); mode = "loop"; }
            applyVisual (m["v"]);
        }
        else if (op == "setting")
        {
            const auto v = m["v"].toString();
            if (v == "auto" || v == "on" || v == "off") { { std::lock_guard<std::mutex> l (statusLock); setting = v; } saveSettings(); }
            lastLaunchTry = -100;
        }
    });
}

void Projector::songVisual (const std::string& visualJson)
{
    const auto v = JSON::parse (String::fromUTF8 (visualJson.c_str()));
    post ([this, v]
    {
        // blacked out or on a card: the song's visual waits for Loop
        if (! running || mode == "black" || mode == "align" || mode == "grid") { std::lock_guard<std::mutex> l (statusLock); pendingVisual = v; return; }
        applyVisual (v);
    });
}

void Projector::dropout()
{
    post ([this]
    {
        if (! running || mode == "black") return;
        setMode ("black");
        std::lock_guard<std::mutex> l (statusLock);
        dropoutBlack = true;
        note = "Blacked out after an audio dropout at " + Time::getCurrentTime().toString (false, true, false, true);
        if (log) log ("Projector: " + note);
    });
}

std::string Projector::statusJson() const
{
    std::lock_guard<std::mutex> l (statusLock);
    auto* o = new DynamicObject();
    o->setProperty ("on", running);
    o->setProperty ("mode", mode);
    o->setProperty ("scene", scene);
    o->setProperty ("a", rangeA);
    o->setProperty ("b", rangeB);
    o->setProperty ("clip", clip);
    o->setProperty ("setting", setting);
    o->setProperty ("screens", screens);
    o->setProperty ("dropout", dropoutBlack);
    o->setProperty ("note", note);
    o->setProperty ("problem", problem);
    return JSON::toString (var (o), true).toStdString();
}

// ---------------------------------------------------------------------------------------
// the projector thread

void Projector::run()
{
    double lastPoll = 0, lastScreens = -10;
    while (! threadShouldExit())
    {
        const double now = Time::getMillisecondCounterHiRes() * 0.001;
        if (now - lastScreens > 2)
        {
            lastScreens = now;
            int count = 1;
            pickScreen (count);
            std::lock_guard<std::mutex> l (statusLock);
            screens = count;
        }
        const bool wanted = setting == "on" || (setting == "auto" && screens >= 2);
        if (running && (mpv == nullptr || ! mpv->isRunning() || pipe == nullptr))
        {
            if (log) log ("Projector: mpv stopped, starting it again");
            quitMpv();
        }
        if (wanted && ! running && now - lastLaunchTry > 5)
        {
            lastLaunchTry = now;
            if (launch())
            {
                // pick up where it was: the loop, or a waiting song
                const auto m = mode;
                var waiting;
                { std::lock_guard<std::mutex> l (statusLock); waiting = pendingVisual; }
                if (m == "black") setMode ("black");
                else if (m == "align" || m == "grid") setMode (m);
                else if (waiting.isObject()) { { std::lock_guard<std::mutex> l (statusLock); pendingVisual = var(); } applyVisual (waiting); }
                else playLoopAt (loopPos);
            }
        }
        else if (! wanted && running) quitMpv();

        for (;;)
        {
            std::function<void()> job;
            { std::lock_guard<std::mutex> l (queueLock); if (jobs.empty()) break; job = std::move (jobs.front()); jobs.pop_front(); }
            job();   // with mpv off, commands do nothing and the state still follows
        }
        if (running && now - lastPoll > 0.25) { lastPoll = now; poll(); }
        wait (100);
    }
    quitMpv();
}

int Projector::pickScreen (int& count) const
{
    // mpv numbers screens in the order Windows lists them; use the first one that isn't
    // the laptop's main screen, or the only one there is
#if JUCE_WINDOWS
    struct Ctx { int n = 0, pick = -1; } ctx;
    EnumDisplayMonitors (nullptr, nullptr, [] (HMONITOR h, HDC, LPRECT, LPARAM p) -> BOOL
    {
        auto& c = *reinterpret_cast<Ctx*> (p);
        MONITORINFO mi { sizeof (mi) };
        if (GetMonitorInfo (h, &mi) && ! (mi.dwFlags & MONITORINFOF_PRIMARY) && c.pick < 0) c.pick = c.n;
        ++c.n;
        return TRUE;
    }, (LPARAM) &ctx);
    count = jmax (1, ctx.n);
    return ctx.pick < 0 ? 0 : ctx.pick;
#else
    count = 1;
    return 0;
#endif
}

bool Projector::launch()
{
    const auto exe = File::getSpecialLocation (File::currentExecutableFile).getSiblingFile ("mpv.exe");
    if (! exe.existsAsFile())
    {
        std::lock_guard<std::mutex> l (statusLock);
        problem = "mpv.exe isn't next to Keystone. Reinstall Keystone.";
        return false;
    }
#if JUCE_WINDOWS
    int count = 1;
    const int screen = pickScreen (count);
    const String pipeName = "\\\\.\\pipe\\keystone-mpv-" + String ((int) GetCurrentProcessId());
    StringArray args { exe.getFullPathName(),
        "--input-ipc-server=" + pipeName,
        "--fs", "--fs-screen=" + String (screen), "--screen=" + String (screen),
        "--idle=yes", "--force-window=yes", "--keep-open=yes", "--loop-file=inf",
        "--image-display-duration=inf", "--background=color", "--background-color=#000000",
        "--no-audio", "--osd-level=0", "--no-osc", "--no-osd-bar", "--osd-on-seek=no",
        "--no-input-default-bindings", "--input-vo-keyboard=no", "--input-cursor=no",
        "--cursor-autohide=always", "--no-border", "--ontop", "--no-terminal", "--title=Keystone visuals",
        "--hwdec=auto-safe", "--priority=belownormal", "--hr-seek=yes", "--no-config" };
    mpv = std::make_unique<ChildProcess>();
    if (! mpv->start (args, 0))
    {
        mpv.reset();
        std::lock_guard<std::mutex> l (statusLock);
        problem = "Couldn't start mpv";
        return false;
    }
    // mpv makes its pipe a moment after it starts
    for (int i = 0; i < 50 && pipe == nullptr && ! threadShouldExit(); ++i)
    {
        HANDLE h = CreateFileW (pipeName.toWideCharPointer(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) pipe = h;
        else Thread::sleep (100);
    }
    if (pipe == nullptr)
    {
        mpv->kill();
        mpv.reset();
        std::lock_guard<std::mutex> l (statusLock);
        problem = "mpv started but didn't answer";
        return false;
    }
    {
        std::lock_guard<std::mutex> l (statusLock);
        running = true;
        problem = loopFile().existsAsFile() ? String() : String ("No loop video yet: put it in Documents\\Keystone\\Visuals");
    }
    if (log) log ("Projector: mpv on screen " + String (screen + 1) + " of " + String (count));
    return true;
#else
    std::lock_guard<std::mutex> l (statusLock);
    problem = "The projector isn't on the Pi yet";
    return false;
#endif
}

void Projector::dropPipe()
{
#if JUCE_WINDOWS
    if (pipe != nullptr) CloseHandle ((HANDLE) pipe);
#endif
    pipe = nullptr;
}

void Projector::quitMpv()
{
    if (pipe != nullptr) command (var (Array<var> { "quit" }));
    dropPipe();
    if (mpv != nullptr) { mpv->waitForProcessToFinish (1000); if (mpv->isRunning()) mpv->kill(); mpv.reset(); }
    readBuffer.clear();
    std::lock_guard<std::mutex> l (statusLock);
    running = false;
}

// One command and its answer. mpv also sends events on the same pipe; those are skipped.
bool Projector::command (const var& args, var* result)
{
#if JUCE_WINDOWS
    if (pipe == nullptr) return false;
    const int id = ++requestId;
    auto* o = new DynamicObject();
    o->setProperty ("command", args);
    o->setProperty ("request_id", id);
    const auto line = (JSON::toString (var (o), true) + "\n").toStdString();
    DWORD written = 0;
    if (! WriteFile ((HANDLE) pipe, line.data(), (DWORD) line.size(), &written, nullptr)) { dropPipe(); return false; }
    const double deadline = Time::getMillisecondCounterHiRes() + 2000;
    while (Time::getMillisecondCounterHiRes() < deadline)
    {
        int nl;
        while ((nl = readBuffer.indexOfChar ('\n')) >= 0)
        {
            const auto reply = JSON::parse (readBuffer.substring (0, nl));
            readBuffer = readBuffer.substring (nl + 1);
            if ((int) reply["request_id"] == id)
            {
                if (result != nullptr) *result = reply["data"];
                return reply["error"].toString() == "success";
            }
        }
        DWORD avail = 0;
        if (! PeekNamedPipe ((HANDLE) pipe, nullptr, 0, nullptr, &avail, nullptr)) { dropPipe(); return false; }
        if (avail == 0) { Thread::sleep (5); continue; }
        char buf[4096];
        DWORD got = 0;
        if (! ReadFile ((HANDLE) pipe, buf, jmin<DWORD> (avail, (DWORD) sizeof (buf)), &got, nullptr)) { dropPipe(); return false; }
        readBuffer += String::fromUTF8 (buf, (int) got);
    }
    return false;
#else
    ignoreUnused (args, result);
    return false;
#endif
}

bool Projector::getProperty (const char* name, var& out)
{
    return command (var (Array<var> { "get_property", name }), &out);
}

File Projector::loopFile() const
{
    if (folder.getChildFile ("loop.mp4").existsAsFile()) return folder.getChildFile ("loop.mp4");
    for (auto& f : folder.findChildFiles (File::findFiles, false, "*.mp4;*.mkv;*.mov;*.webm"))
        if (f.getFileName().containsIgnoreCase ("loop")) return f;
    return {};
}

String Projector::cardSource (bool grid) const
{
    const auto png = folder.getChildFile (grid ? "calibration_grid.png" : "alignment_card.png");
    if (png.existsAsFile()) return png.getFullPathName();
    const auto mp4 = folder.getChildFile (grid ? "calibration_grid.mp4" : "alignment_card.mp4");
    if (mp4.existsAsFile()) return mp4.getFullPathName();
    // no card from the kit yet: a plain stand-in drawn by mpv
    return grid ? "av://lavfi:color=c=black:s=1024x768:r=25,drawgrid=w=64:h=64:t=4:c=white"
                : "av://lavfi:color=c=black:s=1024x768:r=25,drawbox=x=0:y=0:w=1024:h=768:t=24:c=white";
}

// after loadfile: wait up to 3 s for the file to open, so a seek lands
bool Projector::waitLoaded()
{
    for (int i = 0; i < 60 && pipe != nullptr; ++i)
    {
        var d;
        if (getProperty ("duration", d) && ! d.isVoid() && (double) d > 0) return true;
        Thread::sleep (50);
    }
    return false;
}

void Projector::playLoopAt (double seconds, bool clearRange)
{
    const auto f = loopFile();
    if (! f.existsAsFile())
    {
        command (var (Array<var> { "set_property", "vid", "no" }));
        std::lock_guard<std::mutex> l (statusLock);
        problem = "No loop video yet: put it in Documents\\Keystone\\Visuals";
        mode = "loop";
        return;
    }
    var path;
    getProperty ("path", path);
    if (path.toString() != f.getFullPathName())
    {
        command (var (Array<var> { "loadfile", f.getFullPathName(), "replace" }));
        waitLoaded();
    }
    if (clearRange)
    {
        command (var (Array<var> { "set_property", "ab-loop-a", "no" }));
        command (var (Array<var> { "set_property", "ab-loop-b", "no" }));
        std::lock_guard<std::mutex> l (statusLock);
        rangeA = rangeB = 0;
    }
    command (var (Array<var> { "set_property", "vid", "auto" }));
    command (var (Array<var> { "seek", seconds, "absolute" }));
    command (var (Array<var> { "set_property", "pause", false }));
    std::lock_guard<std::mutex> l (statusLock);
    mode = "loop";
    clip.clear();
    problem.clear();
    pos = seconds;
    scene = sceneAt (seconds);
}

void Projector::setMode (const String& m)
{
    if (mode == "loop")
    {
        var t;
        if (getProperty ("time-pos", t) && ! t.isVoid()) loopPos = (double) t;
    }
    if (m == "black")
    {
        command (var (Array<var> { "set_property", "pause", true }));
        command (var (Array<var> { "set_property", "vid", "no" }));
        std::lock_guard<std::mutex> l (statusLock);
        mode = "black";
    }
    else if (m == "align" || m == "grid")
    {
        command (var (Array<var> { "loadfile", cardSource (m == "grid"), "replace" }));
        command (var (Array<var> { "set_property", "vid", "auto" }));
        command (var (Array<var> { "set_property", "pause", false }));
        std::lock_guard<std::mutex> l (statusLock);
        mode = m;
        dropoutBlack = false;
    }
    else if (m == "loop")
    {
        var waiting;
        {
            std::lock_guard<std::mutex> l (statusLock);
            dropoutBlack = false;
            note.clear();
            waiting = pendingVisual;
            pendingVisual = var();
        }
        if (waiting.isObject()) { { std::lock_guard<std::mutex> l (statusLock); mode = "loop"; } applyVisual (waiting); }
        else if (mode == "black" && clip.isNotEmpty())
        {
            command (var (Array<var> { "set_property", "vid", "auto" }));
            command (var (Array<var> { "set_property", "pause", false }));
            std::lock_guard<std::mutex> l (statusLock);
            mode = "clip";
        }
        else if (mode == "black")
        {
            // back where it was, range and all
            command (var (Array<var> { "set_property", "vid", "auto" }));
            command (var (Array<var> { "set_property", "pause", false }));
            std::lock_guard<std::mutex> l (statusLock);
            mode = "loop";
        }
        else playLoopAt (loopPos);
    }
}

void Projector::applyVisual (const var& v)
{
    const auto kind = v["kind"].toString();
    if (kind == "scene") playLoopAt (sceneStart ((int) v["n"]));
    else if (kind == "range")
    {
        int a = (int) v["a"], b = (int) v["b"];
        if (b < a) std::swap (a, b);
        playLoopAt (sceneStart (a));
        command (var (Array<var> { "set_property", "ab-loop-a", sceneStart (a) }));
        command (var (Array<var> { "set_property", "ab-loop-b", sceneEnd (b) }));
        std::lock_guard<std::mutex> l (statusLock);
        rangeA = a; rangeB = b;
    }
    else if (kind == "clip")
    {
        const auto f = folder.getChildFile (File::createLegalFileName (v["file"].toString()));
        if (! f.existsAsFile())
        {
            std::lock_guard<std::mutex> l (statusLock);
            note = "Clip not found: " + f.getFileName();
            return;
        }
        if (mode == "loop") { var t; if (getProperty ("time-pos", t) && ! t.isVoid()) loopPos = (double) t; }
        command (var (Array<var> { "loadfile", f.getFullPathName(), "replace" }));
        command (var (Array<var> { "set_property", "vid", "auto" }));
        command (var (Array<var> { "set_property", "pause", false }));
        std::lock_guard<std::mutex> l (statusLock);
        mode = "clip";
        clip = f.getFileName();
        rangeA = rangeB = 0;
    }
    else   // next: on to the next scene of the loop
    {
        double from = loopPos;
        if (mode == "loop") { var t; if (getProperty ("time-pos", t) && ! t.isVoid()) from = (double) t; }
        const int n = sceneAt (from) % (int) scenes().size() + 1;
        playLoopAt (sceneStart (n));
    }
}

int Projector::sceneAt (double t) const
{
    const auto& s = scenes();
    int n = 1;
    for (size_t i = 0; i < s.size(); ++i) if (t + 0.05 >= s[i].start) n = (int) i + 1;
    return n;
}

void Projector::poll()
{
    if (mode != "loop") return;
    var t;
    if (getProperty ("time-pos", t) && ! t.isVoid())
    {
        std::lock_guard<std::mutex> l (statusLock);
        pos = (double) t;
        scene = sceneAt (pos);
    }
}

void Projector::loadSettings()
{
    const auto j = JSON::parse (folder.getParentDirectory().getChildFile ("projector.json"));
    const auto v = j["setting"].toString();
    if (v == "auto" || v == "on" || v == "off") setting = v;
}

void Projector::saveSettings() const
{
    auto* o = new DynamicObject();
    o->setProperty ("setting", setting);
    folder.getParentDirectory().getChildFile ("projector.json").replaceWithText (JSON::toString (var (o)));
}
