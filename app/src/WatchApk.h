#pragma once
// The watch updates itself from Keystone (0.6.6). Keystone's package carries the watch app
// (Watch/Keystone-Watch.apk beside the program). When a watch says hello on an older version,
// Keystone tells it {"t":"watchUpdate","v","size","port"}; the watch opens a plain TCP
// connection to that port and Keystone writes the APK's bytes and closes. No HTTP, no
// internet, nothing for the watch to type. Runs at low priority so the audio never notices.

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

class WatchApk : private juce::Thread
{
public:
    static constexpr int kPort = 47802;

    explicit WatchApk (juce::File apkFile) : juce::Thread ("Keystone watch update"), apk (std::move (apkFile)) {}
    ~WatchApk() override { stop(); }

    bool available() const { return apk.existsAsFile() && apk.getSize() > 100000; }
    juce::int64 size() const { return apk.getSize(); }

    // "0.6.6" newer than "0.6.5"; an empty or unreadable version counts as older
    static bool newer (const juce::String& a, const juce::String& b)
    {
        auto pa = juce::StringArray::fromTokens (a, ".", ""), pb = juce::StringArray::fromTokens (b, ".", "");
        for (int i = 0; i < juce::jmax (pa.size(), pb.size()); ++i)
        {
            const int x = i < pa.size() ? pa[i].getIntValue() : 0, y = i < pb.size() ? pb[i].getIntValue() : 0;
            if (x != y) return x > y;
        }
        return false;
    }

    // whether a watch on this version should be offered the update. Watches older than 0.6.6
    // can't update themselves (they ignore the offer), so the phone's way still covers them.
    bool shouldOffer (const juce::String& keystoneVersion, const juce::String& watchVersion) const
    {
        return available() && watchVersion.isNotEmpty() && newer (keystoneVersion, watchVersion);
    }

    juce::String offerJson (const juce::String& keystoneVersion)
    {
        start();
        return "{\"t\":\"watchUpdate\",\"v\":\"" + keystoneVersion + "\",\"size\":" + juce::String (size())
               + ",\"port\":" + juce::String (kPort) + "}";
    }

    void stop()
    {
        signalThreadShouldExit();
        if (listener) listener->close();
        stopThread (3000);
        listener.reset();
    }

    std::function<void (const juce::String&)> log;

private:
    // listens only once a watch has been offered the update
    void start()
    {
        if (isThreadRunning()) return;
        listener = std::make_unique<juce::StreamingSocket>();
        if (! listener->createListener (kPort))
        {
            if (log) log ("Watch update: couldn't listen on TCP " + juce::String (kPort));
            listener.reset();
            return;
        }
        startThread (juce::Thread::Priority::low);
    }

    void run() override
    {
        while (! threadShouldExit() && listener)
        {
            std::unique_ptr<juce::StreamingSocket> c (listener->waitForNextConnection());
            if (c == nullptr || threadShouldExit()) continue;
            sendTo (*c);
        }
    }

    void sendTo (juce::StreamingSocket& c)
    {
        juce::FileInputStream in (apk);
        if (! in.openedOk()) { if (log) log ("Watch update: couldn't open " + apk.getFullPathName()); return; }
        std::vector<char> buf (32768);
        juce::int64 sent = 0;
        while (! threadShouldExit())
        {
            const int n = in.read (buf.data(), (int) buf.size());
            if (n <= 0) break;
            int off = 0;
            while (off < n)
            {
                if (c.waitUntilReady (false, 15000) != 1) { if (log) log ("Watch update: the watch stopped reading"); return; }
                const int w = c.write (buf.data() + off, n - off);
                if (w <= 0) { if (log) log ("Watch update: the watch went away after " + juce::String (sent) + " bytes"); return; }
                off += w;
                sent += w;
            }
        }
        if (log) log ("Watch update: sent the watch app (" + juce::String (sent) + " bytes)");
    }

    juce::File apk;
    std::unique_ptr<juce::StreamingSocket> listener;
};
