#pragma once
// The link to the phone: newline-delimited JSON over TCP (port 47801, Nagle off for low
// latency), plus discovery over UDP (port 47800). Keystone broadcasts a small beacon every
// second on every network it's on, and answers "KEYSTONE?" probes, so the phone finds it
// on its own hotspot with no internet and no typing of addresses.
//
// Incoming lines are handed to the message thread; outgoing lines are queued per client
// and written on that client's own thread, so a slow phone never stalls anything.

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <functional>
#include <map>
#include <memory>

class LinkServer : private juce::Thread
{
public:
    static constexpr int kTcpPort = 47801, kUdpPort = 47800;

    LinkServer();
    ~LinkServer() override;

    // Called on the message thread.
    std::function<void (int client, const juce::String& line)> onMessage;
    std::function<void (int client)> onConnect, onDisconnect;

    bool start();
    void stop();
    void send (int client, const juce::String& line);
    void broadcast (const juce::String& line, bool droppable = false);
    int clientCount() const;
    juce::String addresses() const;

private:
    struct Client;
    class Discovery;

    void run() override;   // accepts connections
    void removeDead();

    std::unique_ptr<juce::StreamingSocket> listener;
    std::unique_ptr<Discovery> discovery;
    std::map<int, std::shared_ptr<Client>> clients;
    mutable juce::CriticalSection clientsLock;
    int nextId = 1;
};
