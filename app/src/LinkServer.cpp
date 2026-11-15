#include "LinkServer.h"

#include "keystone/Controller.h"

#include <deque>

using namespace juce;

// ---------------------------------------------------------------------------------------
struct LinkServer::Client : public Thread
{
    Client (int id_, std::unique_ptr<StreamingSocket> s, LinkServer& owner_)
        : Thread ("Link client " + String (id_)), id (id_), socket (std::move (s)), owner (owner_), writer (*this)
    {
    }
    ~Client() override
    {
        alive = false;
        socket->close();
        writer.stopThread (2000);
        stopThread (2000);
    }

    // reader
    void run() override
    {
        writer.startThread();
        MemoryBlock pending;
        char buf[4096];
        while (! threadShouldExit() && alive)
        {
            const int ready = socket->waitUntilReady (true, 500);
            if (ready < 0) break;
            if (ready == 0) continue;
            const int got = socket->read (buf, sizeof (buf), false);
            if (got <= 0) break;
            pending.append (buf, (size_t) got);
            // split into lines
            const char* data = static_cast<const char*> (pending.getData());
            size_t start = 0;
            for (size_t i = 0; i < pending.getSize(); ++i)
            {
                if (data[i] != '\n') continue;
                String line (CharPointer_UTF8 (data + start), CharPointer_UTF8 (data + i));
                start = i + 1;
                line = line.trim();
                if (line.isEmpty()) continue;
                const int cid = id;
                LinkServer* o = &owner;
                MessageManager::callAsync ([o, cid, line]() { if (o->onMessage) o->onMessage (cid, line); });
            }
            if (start > 0) pending.removeSection (0, start);
            if (pending.getSize() > 1 << 20) break;   // nobody sends a megabyte line
        }
        alive = false;
        const int cid = id;
        LinkServer* o = &owner;
        MessageManager::callAsync ([o, cid]() { if (o->onDisconnect) o->onDisconnect (cid); o->removeDead(); });
    }

    void enqueue (const String& line, bool droppable)
    {
        {
            const ScopedLock l (queueLock);
            // a phone that can't keep up loses meter frames, never state changes
            if (droppable && queue.size() > 50) return;
            queue.push_back (line + "\n");
        }
        writer.notify();
    }

    struct Writer : public Thread
    {
        explicit Writer (Client& c) : Thread ("Link writer"), client (c) {}
        void run() override
        {
            while (! threadShouldExit() && client.alive)
            {
                String next;
                {
                    const ScopedLock l (client.queueLock);
                    if (! client.queue.empty()) { next = client.queue.front(); client.queue.pop_front(); }
                }
                if (next.isEmpty()) { wait (200); continue; }
                const auto utf8 = next.toUTF8();
                const int len = (int) std::strlen (utf8);
                int sent = 0;
                // no waitUntilReady here: JUCE shares that lock with read(), so waiting to
                // write would starve the reader. A full send buffer just gets retried.
                int stalls = 0;
                while (sent < len && client.alive)
                {
                    const int w = client.socket->write (utf8 + sent, len - sent);
                    if (w > 0) { sent += w; stalls = 0; continue; }
                    if (++stalls > 200 || ! client.socket->isConnected()) { client.alive = false; break; }
                    Thread::sleep (5);
                }
            }
        }
        Client& client;
    };

    const int id;
    std::unique_ptr<StreamingSocket> socket;
    LinkServer& owner;
    std::atomic<bool> alive { true };
    CriticalSection queueLock;
    std::deque<String> queue;
    Writer writer;
};

// ---------------------------------------------------------------------------------------
class LinkServer::Discovery : public Thread
{
public:
    Discovery() : Thread ("Link discovery") {}
    ~Discovery() override { stopThread (3000); }

    bool open()
    {
        socket = std::make_unique<DatagramSocket> (true);
        if (! socket->bindToPort (kUdpPort))
        {
            // something else holds the port: beacons still go out from a free one
            socket = std::make_unique<DatagramSocket> (true);
            socket->bindToPort (0);
        }
        return true;
    }

    String beacon() const
    {
        return "{\"t\":\"keystone\",\"port\":" + String (kTcpPort) + ",\"name\":\"" + SystemStats::getComputerName().replace ("\"", "")
               + "\",\"version\":\"" + ks::Controller::kVersion + "\"}";
    }

    void run() override
    {
        char buf[512];
        double lastBeacon = 0;
        while (! threadShouldExit())
        {
            const double now = Time::getMillisecondCounterHiRes();
            if (now - lastBeacon > 1000.0)
            {
                lastBeacon = now;
                sendBeacons();
            }
            if (socket->waitUntilReady (true, 250) > 0)
            {
                String from;
                int port = 0;
                const int got = socket->read (buf, sizeof (buf) - 1, false, from, port);
                if (got > 0)
                {
                    buf[got] = 0;
                    if (String (buf).startsWith ("KEYSTONE?"))
                    {
                        const auto b = beacon();
                        socket->write (from, port, b.toRawUTF8(), (int) b.getNumBytesAsUTF8());
                    }
                }
            }
        }
    }

    void sendBeacons()
    {
        const auto b = beacon();
        StringArray targets { "255.255.255.255" };
        for (const auto& a : IPAddress::getAllAddresses (false))
        {
            if (a.toString().startsWith ("127.")) continue;
            const auto bc = IPAddress::getInterfaceBroadcastAddress (a);
            if (! bc.isNull()) targets.addIfNotAlreadyThere (bc.toString());
        }
        for (const auto& t : targets) socket->write (t, kUdpPort, b.toRawUTF8(), (int) b.getNumBytesAsUTF8());
    }

private:
    std::unique_ptr<DatagramSocket> socket;
};

// ---------------------------------------------------------------------------------------
LinkServer::LinkServer() : Thread ("Link server") {}

LinkServer::~LinkServer() { stop(); }

bool LinkServer::start()
{
    listener = std::make_unique<StreamingSocket>();
    if (! listener->createListener (kTcpPort))
    {
        Logger::writeToLog ("Link: couldn't listen on TCP " + String (kTcpPort));
        return false;
    }
    discovery = std::make_unique<Discovery>();
    discovery->open();
    discovery->startThread();
    startThread();
    Logger::writeToLog ("Link: listening on TCP " + String (kTcpPort) + ", discovery on UDP " + String (kUdpPort) + " (" + addresses() + ")");
    return true;
}

void LinkServer::stop()
{
    signalThreadShouldExit();
    if (listener) listener->close();
    stopThread (3000);
    discovery.reset();
    const ScopedLock l (clientsLock);
    clients.clear();
}

void LinkServer::run()
{
    while (! threadShouldExit())
    {
        std::unique_ptr<StreamingSocket> s (listener->waitForNextConnection());
        if (s == nullptr) { if (! threadShouldExit()) Thread::sleep (100); continue; }
        std::shared_ptr<Client> c;
        int id;
        {
            const ScopedLock l (clientsLock);
            id = nextId++;
            c = std::make_shared<Client> (id, std::move (s), *this);
            clients[id] = c;
        }
        c->startThread();
        LinkServer* self = this;
        MessageManager::callAsync ([self, id]() { if (self->onConnect) self->onConnect (id); });
    }
}

void LinkServer::removeDead()
{
    std::vector<std::shared_ptr<Client>> dead;
    {
        const ScopedLock l (clientsLock);
        for (auto it = clients.begin(); it != clients.end();)
        {
            if (! it->second->alive) { dead.push_back (it->second); it = clients.erase (it); }
            else ++it;
        }
    }
    // dead clients' threads are stopped outside the lock
}

void LinkServer::send (int client, const String& line)
{
    const ScopedLock l (clientsLock);
    auto it = clients.find (client);
    if (it != clients.end() && it->second->alive) it->second->enqueue (line, false);
}

void LinkServer::broadcast (const String& line, bool droppable)
{
    const ScopedLock l (clientsLock);
    for (auto& [id, c] : clients)
        if (c->alive) c->enqueue (line, droppable);
}

int LinkServer::clientCount() const
{
    const ScopedLock l (clientsLock);
    int n = 0;
    for (auto& [id, c] : clients) n += c->alive ? 1 : 0;
    return n;
}

String LinkServer::addresses() const
{
    StringArray a;
    for (const auto& ip : IPAddress::getAllAddresses (false))
        if (! ip.toString().startsWith ("127.")) a.add (ip.toString());
    return a.joinIntoString (", ");
}
