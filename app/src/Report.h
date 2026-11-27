#pragma once
// Send report (0.6.12): a black box for fault finding.
//
// HealthHistory keeps a line of Health every 5 s, in memory and in
// Documents/Keystone/Reports/health-history.jsonl (written once a minute, rolled at 3 MB, so the
// last few hours survive a crash or a restart).
//
// ReportMaker zips up everything worth reading when something goes wrong: the log, the health
// history, the settings (passwords blanked), the updater's log, and on the Pi the journal, the
// kernel log and a page of system facts. It runs on its own low-priority thread and never
// touches the audio. The phone asks for it with {"t":"report","op":"make","note":"..."}, hears
// {"t":"reportReady",...} when it's done, then fetches it in pieces with
// {"t":"report","op":"get","name":"...","offset":n} and shares it (Drive, email, anywhere).

#include <juce_core/juce_core.h>
#include <deque>
#include <functional>
#include <mutex>

struct HealthHistory
{
    juce::File file;                      // Reports/health-history.jsonl
    static constexpr int keepLines = 2880;      // 4 hours at one line every 5 s, in memory
    static constexpr juce::int64 rollBytes = 3 * 1024 * 1024;

    void add (const juce::String& healthJson)
    {
        const auto line = "{\"at\":\"" + juce::Time::getCurrentTime().toISO8601 (true) + "\",\"h\":" + healthJson + "}";
        std::lock_guard<std::mutex> g (lock);
        lines.push_back (line);
        while ((int) lines.size() > keepLines) lines.pop_front();
        pending.add (line);
        if (pending.size() >= 12) writeLocked();   // once a minute
    }

    // everything still in memory onto the disk (before a report, and when Keystone stops)
    void flush() { std::lock_guard<std::mutex> g (lock); writeLocked(); }

private:
    void writeLocked()
    {
        if (pending.isEmpty() || file == juce::File()) return;
        file.getParentDirectory().createDirectory();
        if (file.getSize() > rollBytes)
        {
            const auto older = file.getSiblingFile (file.getFileNameWithoutExtension() + "-older.jsonl");
            older.deleteFile();
            file.moveFileTo (older);
        }
        file.appendText (pending.joinIntoString ("\n") + "\n", false, false, "\n");
        pending.clear();
    }

    std::mutex lock;
    std::deque<juce::String> lines;
    juce::StringArray pending;
};

class ReportMaker : private juce::Thread
{
public:
    juce::File home;                       // Documents/Keystone
    juce::Array<juce::File> extraFiles;    // anything else worth a look (the footswitch settings)
    HealthHistory* history = nullptr;
    std::function<juce::String()> healthNow;               // the latest Health, as JSON
    std::function<void (const juce::String& json)> onDone; // {"t":"reportReady",...}, from this thread
    std::function<void (const juce::String&)> log;

    ReportMaker() : juce::Thread ("report") {}
    ~ReportMaker() override { stop(); }

    juce::File folder() const { return home.getChildFile ("Reports"); }
    void stop() { stopThread (20000); }

    static bool validName (const juce::String& n)
    {
        return n.startsWith ("keystone-report-") && n.endsWith (".zip") && n.length() < 80
               && ! n.containsAnyOf ("/\\:") && ! n.contains ("..");
    }

    // One link message: make, get or list. Returns the reply for the phone that asked.
    juce::String handle (const juce::String& line)
    {
        const auto m = juce::JSON::parse (line);
        const auto op = m.getProperty ("op", "").toString();
        auto reply = [] (const juce::String& t, std::initializer_list<std::pair<const char*, juce::var>> items)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("t", t);
            for (auto& [k, v] : items) o->setProperty (k, v);
            return juce::JSON::toString (juce::var (o), true);
        };
        if (op == "make")
        {
            if (isThreadRunning()) return reply ("reportAck", { { "op", "make" }, { "ok", true }, { "busy", true } });
            note = m.getProperty ("note", "").toString().substring (0, 2000);
            from = m.getProperty ("from", "").toString().substring (0, 200);
            startThread (juce::Thread::Priority::background);
            return reply ("reportAck", { { "op", "make" }, { "ok", true } });
        }
        if (op == "get")
        {
            const auto name = m.getProperty ("name", "").toString();
            const juce::int64 offset = juce::jmax<juce::int64> (0, (juce::int64) m.getProperty ("offset", 0));
            if (! validName (name)) return reply ("reportChunk", { { "ok", false }, { "why", "That isn't a report" } });
            juce::FileInputStream in (folder().getChildFile (name));
            if (! in.openedOk()) return reply ("reportChunk", { { "ok", false }, { "name", name }, { "why", "That report has gone. Make a new one" } });
            const auto total = in.getTotalLength();
            juce::MemoryBlock buf;
            in.setPosition (offset);
            in.readIntoMemoryBlock (buf, juce::jmin<juce::int64> (96 * 1024, juce::jmax<juce::int64> (0, total - offset)));
            return reply ("reportChunk", { { "ok", true }, { "name", name }, { "offset", offset }, { "total", total },
                                           { "data", juce::Base64::toBase64 (buf.getData(), buf.getSize()) } });
        }
        return reply ("reportAck", { { "op", op }, { "ok", false }, { "why", "Keystone doesn't know that" } });
    }

    // passwords, keys and tokens blanked anywhere in a JSON file
    static juce::var redact (const juce::var& v)
    {
        if (auto* o = v.getDynamicObject())
        {
            auto* copy = new juce::DynamicObject();
            for (auto& p : o->getProperties())
            {
                const auto k = p.name.toString().toLowerCase();
                const bool secret = k.contains ("pass") || k.contains ("psk") || k.contains ("secret") || k.contains ("token") || k == "key" || k == "apikey" || k == "pin";
                copy->setProperty (p.name, secret && ! p.value.isObject() && ! p.value.isArray() ? juce::var ("(hidden)") : redact (p.value));
            }
            return juce::var (copy);
        }
        if (auto* a = v.getArray())
        {
            juce::Array<juce::var> out;
            for (auto& x : *a) out.add (redact (x));
            return out;
        }
        return v;
    }

private:
    juce::String note, from;

    void run() override
    {
        const auto started = juce::Time::getCurrentTime();
        const auto name = "keystone-report-" + started.formatted ("%Y-%m-%d-%H%M%S") + ".zip";
        juce::String why;
        juce::int64 bytes = 0;
        try
        {
            folder().createDirectory();
            if (history) history->flush();
            juce::ZipFile::Builder zip;
            auto addText = [&zip, started] (const juce::String& path, const juce::String& text)
            {
                zip.addEntry (new juce::MemoryInputStream (juce::MemoryBlock (text.toRawUTF8(), text.getNumBytesAsUTF8()), true), 6, path, started);
            };
            auto addFile = [&zip] (const juce::File& f, const juce::String& path)
            {
                if (f.existsAsFile() && f.getSize() < 32 * 1024 * 1024) zip.addFile (f, 6, path);
            };

            const auto health = healthNow ? healthNow() : juce::String();
            addText ("README.txt", readme (started, health));
            addText ("health-now.json", health);
            addFile (home.getChildFile ("keystone.log"), "keystone.log");
            addFile (folder().getChildFile ("health-history.jsonl"), "health-history.jsonl");
            addFile (folder().getChildFile ("health-history-older.jsonl"), "health-history-older.jsonl");
            addFile (home.getChildFile ("Updates/update.log"), "update.log");
            addFile (home.getChildFile ("Updates/player.txt"), "player.txt");
            for (auto& f : home.findChildFiles (juce::File::findFiles, false, "*.json"))
                addSettings (zip, started, f);
            for (auto& f : extraFiles)
                if (f.hasFileExtension ("json")) addSettings (zip, started, f); else addFile (f, "settings/" + f.getFileName());
            addText ("folders.txt", folders());
           #if JUCE_LINUX
            // each of these can take a few seconds: Keystone stopping doesn't wait for them all
            if (! threadShouldExit()) addText ("system.txt", systemFacts());
            if (! threadShouldExit()) addText ("journal.txt", cmd ({ "journalctl", "-u", "keystone", "-u", "keystone-update", "-u", "keystone-wifi", "-u", "keystone-power",
                                           "-u", "keystone-performance", "--since", "-12h", "-n", "6000", "--no-pager", "-o", "short-iso" }, 15));
            if (! threadShouldExit()) addText ("kernel.txt", cmd ({ "journalctl", "-k", "-b", "-n", "1500", "--no-pager", "-o", "short-iso" }, 15));
            if (! threadShouldExit()) addText ("previous-boot.txt", cmd ({ "journalctl", "-b", "-1", "-n", "400", "--no-pager", "-o", "short-iso" }, 15));
           #endif

            if (threadShouldExit()) return;   // Keystone is stopping: no half-made report
            const auto tmp = folder().getChildFile (name + ".part");
            tmp.deleteFile();
            {
                juce::FileOutputStream out (tmp);
                if (! out.openedOk() || ! zip.writeToStream (out, nullptr)) why = "Couldn't write the report (is the card full?)";
            }
            if (why.isEmpty() && ! tmp.moveFileTo (folder().getChildFile (name))) why = "Couldn't save the report";
            tmp.deleteFile();
            bytes = folder().getChildFile (name).getSize();
            tidy();
        }
        catch (const std::exception& e) { why = juce::String ("Couldn't make the report: ") + e.what(); }

        if (log) log (why.isEmpty() ? "Report: made " + name + " (" + juce::String (bytes / 1024) + " KB)" : "Report: " + why);
        auto* o = new juce::DynamicObject();
        o->setProperty ("t", "reportReady");
        o->setProperty ("ok", why.isEmpty());
        if (why.isEmpty()) { o->setProperty ("name", name); o->setProperty ("bytes", bytes); }
        else o->setProperty ("why", why);
        if (onDone) onDone (juce::JSON::toString (juce::var (o), true));
    }

    void addSettings (juce::ZipFile::Builder& zip, juce::Time t, const juce::File& f)
    {
        if (! f.existsAsFile() || f.getSize() > 4 * 1024 * 1024) return;
        const auto parsed = juce::JSON::parse (f.loadFileAsString());
        const auto text = parsed.isVoid() ? juce::String ("(couldn't read this file as JSON)") : juce::JSON::toString (redact (parsed));
        zip.addEntry (new juce::MemoryInputStream (juce::MemoryBlock (text.toRawUTF8(), text.getNumBytesAsUTF8()), true), 6, "settings/" + f.getFileName(), t);
    }

    juce::String readme (juce::Time t, const juce::String& health) const
    {
        const auto h = juce::JSON::parse (health);
        juce::String s;
        s << "Keystone report\n"
          << "Made: " << t.toISO8601 (true) << "\n"
          << "Version: " << h.getProperty ("version", "?").toString() << " (" << h.getProperty ("device", "?").toString() << ")\n"
          << "Running for: " << juce::String ((int) h.getProperty ("uptime", 0) / 60) << " min\n"
          << "Sent from: " << (from.isEmpty() ? juce::String ("the phone") : from) << "\n\n"
          << "What happened, in Kevin's words:\n" << (note.trim().isEmpty() ? juce::String ("(nothing written)") : note.trim()) << "\n\n"
          << "In here:\n"
          << "  keystone.log                 Keystone's own log (the newest at the bottom)\n"
          << "  health-now.json              the Health page right now\n"
          << "  health-history.jsonl         a Health line every 5 s, newest at the bottom (older file too)\n"
          << "  update.log, player.txt       the updater and the video player install\n"
          << "  settings/                    settings, setlist, projector, footswitches (passwords hidden)\n"
          << "  system.txt                   power, temperature, USB, audio cards, network, processes (Pi)\n"
          << "  journal.txt, kernel.txt      the system journal for Keystone's services and the kernel (Pi)\n"
          << "  previous-boot.txt            the end of the last boot, if the Pi lost power or crashed (Pi)\n"
          << "  folders.txt                  what's in Captures, Spaces, Visuals and Updates\n";
        return s;
    }

    juce::String folders() const
    {
        juce::String s;
        for (auto sub : { "Captures", "Spaces", "Visuals", "Updates", "Reports" })
        {
            s << "== " << sub << "\n";
            for (auto& f : home.getChildFile (sub).findChildFiles (juce::File::findFiles, false))
                s << "  " << f.getFileName() << "  " << juce::File::descriptionOfSizeInBytes (f.getSize()) << "  " << f.getLastModificationTime().toISO8601 (true) << "\n";
        }
        return s;
    }

    // a command's output, given up after a few seconds so a stuck tool can't hold the report
    static juce::String cmd (const juce::StringArray& args, int seconds = 5)
    {
        juce::StringArray a { "timeout", juce::String (seconds) };
        a.addArray (args);
        juce::ChildProcess p;
        if (! p.start (a, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr)) return "(couldn't run " + args[0] + ")\n";
        auto out = p.readAllProcessOutput();
        p.waitForProcessToFinish (2000);
        if (out.length() > 2 * 1024 * 1024) out = "(cut to the last 2 MB)\n" + out.getLastCharacters (2 * 1024 * 1024);
        return out;
    }

    static juce::String systemFacts()
    {
        juce::String s;
        auto section = [&s] (const juce::String& title, const juce::String& body) { s << "== " << title << "\n" << body.trimEnd() << "\n\n"; };
        auto read = [] (const char* path) { return juce::File (path).loadFileAsString(); };
        section ("uname", cmd ({ "uname", "-a" }));
        section ("os", read ("/etc/os-release"));
        section ("model", read ("/proc/device-tree/model").removeCharacters (juce::String::charToString (0)));
        section ("uptime", cmd ({ "uptime" }));
        section ("power (0x0 is good; 0x50000 means it dipped since boot)", cmd ({ "vcgencmd", "get_throttled" }));
        section ("supply voltage", cmd ({ "vcgencmd", "pmic_read_adc", "EXT5V_V" }));
        section ("temperature", cmd ({ "vcgencmd", "measure_temp" }));
        section ("core volts and clock", cmd ({ "vcgencmd", "measure_volts", "core" }) + cmd ({ "vcgencmd", "measure_clock", "arm" }));
        section ("memory", cmd ({ "free", "-m" }));
        section ("storage", cmd ({ "df", "-h", "-x", "tmpfs", "-x", "devtmpfs" }));
        section ("usb", cmd ({ "lsusb" }));
        section ("sound cards", read ("/proc/asound/cards"));
        section ("playback devices", cmd ({ "aplay", "-l" }));
        section ("capture devices", cmd ({ "arecord", "-l" }));
        section ("network", cmd ({ "ip", "-brief", "addr" }) + cmd ({ "nmcli", "-t", "-f", "DEVICE,TYPE,STATE,CONNECTION", "device" }));
        section ("failed services", cmd ({ "systemctl", "--failed", "--no-pager" }));
        section ("keystone service", cmd ({ "systemctl", "status", "keystone", "--no-pager", "-n", "0" }));
        {
            juce::StringArray l;
            l.addLines (cmd ({ "ps", "-eo", "pid,ni,rtprio,psr,pcpu,pmem,etime,comm", "--sort=-pcpu" }));
            l.removeRange (25, l.size());
            section ("busiest processes", l.joinIntoString ("\n"));
        }
        section ("who Keystone runs as", cmd ({ "id" }));
        section ("installed copies", cmd ({ "ls", "-la", "/opt/keystone" }));
        return s;
    }

    // the five newest reports stay; older ones go
    void tidy()
    {
        auto all = folder().findChildFiles (juce::File::findFiles, false, "keystone-report-*.zip");
        std::sort (all.begin(), all.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName() > b.getFileName(); });
        for (int i = 5; i < all.size(); ++i) all.getReference (i).deleteFile();
    }
};
