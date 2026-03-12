#include "SignalingServer.h"

#if JUCE_MAC || JUCE_LINUX
 #include <ifaddrs.h>
 #include <netinet/in.h>
 #include <arpa/inet.h>
 #include <sys/socket.h>
#endif

SignalingServer::SignalingServer() : juce::Thread ("CollabSync Signaling") {}

SignalingServer::~SignalingServer()
{
    stop();
}

//==============================================================================
void SignalingServer::start()
{
    if (isThreadRunning()) return;

    joinedCount.store (0);
    state.store (State::Stopped);
    readyEvent.reset();

    {
        std::lock_guard<std::mutex> lock (roomsMutex);
        rooms.clear();
    }

    startThread();
    readyEvent.wait (1000); // block until listening or error (usually <5ms)
}

void SignalingServer::stop()
{
    signalThreadShouldExit();

    {
        std::lock_guard<std::mutex> lock (clientsMutex);
        for (auto* s : clientSockets)
            s->close();
        clientSockets.clear();
    }

    stopThread (2000);
    state.store (State::Stopped);
    joinedCount.store (0);
}

juce::String SignalingServer::getTailscaleIP() const
{
    std::lock_guard<std::mutex> lock (metaMutex);
    return tailscaleIP;
}

juce::String SignalingServer::getErrorMessage() const
{
    std::lock_guard<std::mutex> lock (metaMutex);
    return errorMessage;
}

//==============================================================================
void SignalingServer::run()
{
    {
        std::lock_guard<std::mutex> lock (metaMutex);
        tailscaleIP = detectTailscaleIP();
    }

    juce::StreamingSocket serverSock;

    if (! serverSock.createListener (8765))
    {
        std::lock_guard<std::mutex> lock (metaMutex);
        errorMessage = "Port 8765 already in use";
        state.store (State::Error);
        readyEvent.signal();
        return;
    }

    state.store (State::Listening);
    readyEvent.signal();

    while (! threadShouldExit())
    {
        if (serverSock.waitUntilReady (true, 100) != 1)
            continue;

        auto* client = serverSock.waitForNextConnection();
        if (! client) continue;

        {
            std::lock_guard<std::mutex> lock (clientsMutex);
            clientSockets.push_back (client);
        }

        std::thread ([this, client]()
        {
            serveClient (client);

            {
                std::lock_guard<std::mutex> lock (clientsMutex);
                clientSockets.erase (
                    std::remove (clientSockets.begin(), clientSockets.end(), client),
                    clientSockets.end());
            }

            delete client;
        }).detach();
    }
}

//==============================================================================
void SignalingServer::serveClient (juce::StreamingSocket* sock)
{
    juce::String lineBuffer;
    juce::String myRoom;
    char chunk[512];

    // Phase 1: read until we receive a valid JOIN
    while (sock->isConnected() && ! threadShouldExit() && myRoom.isEmpty())
    {
        int ready = sock->waitUntilReady (true, 200);
        if (ready == -1) return;
        if (ready == 0)  continue;

        int bytes = sock->read (chunk, (int) sizeof (chunk) - 1, false);
        if (bytes <= 0) return;

        chunk[bytes] = '\0';
        lineBuffer += juce::String::fromUTF8 (chunk, bytes);

        int nl;
        while ((nl = lineBuffer.indexOfChar ('\n')) >= 0)
        {
            juce::String line = lineBuffer.substring (0, nl).trim();
            lineBuffer = lineBuffer.substring (nl + 1);

            juce::StringArray parts = juce::StringArray::fromTokens (line, " ", "");
            if (parts.size() < 3 || parts[0] != "JOIN") continue;

            juce::String roomCode = parts[1].toUpperCase();
            int          udpPort  = parts[2].getIntValue();

            if (udpPort < 1024 || udpPort > 65535)
            {
                sendLine (sock, "ERROR invalid_port");
                continue;
            }

            juce::String myIP = getRemoteIP (sock);
            // If the client connected from localhost (host auto-connecting to their
            // own server), the guest would receive "127.0.0.1" as the peer address
            // and send audio to their own loopback instead of the host's machine.
            // Substitute with the server's actual external IP so both sides reach each other.
            if (myIP.isEmpty() || myIP == "127.0.0.1" || myIP == "::1")
            {
                std::lock_guard<std::mutex> metaLock (metaMutex);
                if (tailscaleIP.isNotEmpty())
                    myIP = tailscaleIP;
                else
                    myIP = "127.0.0.1"; // last resort — same machine
            }

            {
                std::lock_guard<std::mutex> lock (roomsMutex);
                auto& peers = rooms[roomCode];

                if ((int) peers.size() >= 2)
                {
                    sendLine (sock, "ERROR room_full");
                    continue;
                }

                for (auto& peer : peers)
                {
                    sendLine (sock,          "PEER " + peer.ip + " " + juce::String (peer.udpPort));
                    sendLine (peer.socket,   "PEER " + myIP    + " " + juce::String (udpPort));
                }

                if (peers.empty())
                    sendLine (sock, "WAIT");

                peers.push_back ({ sock, udpPort, myIP });
                myRoom = roomCode;
                joinedCount.fetch_add (1);
            }
        }
    }

    // Phase 2: hold connection open, watching for disconnect
    while (sock->isConnected() && ! threadShouldExit())
    {
        int ready = sock->waitUntilReady (true, 500);
        if (ready == -1) break;
        if (ready == 1)
        {
            char probe[1];
            if (sock->read (probe, 1, false) <= 0) break;
        }
    }

    // Remove from room
    if (myRoom.isNotEmpty())
    {
        std::lock_guard<std::mutex> lock (roomsMutex);
        auto it = rooms.find (myRoom);
        if (it != rooms.end())
        {
            auto& peers = it->second;
            peers.erase (std::remove_if (peers.begin(), peers.end(),
                [sock] (const PeerSlot& p) { return p.socket == sock; }),
                peers.end());
            if (peers.empty()) rooms.erase (it);
        }
        joinedCount.fetch_sub (1);
    }
}

//==============================================================================
void SignalingServer::sendLine (juce::StreamingSocket* sock, const juce::String& msg)
{
    if (! sock || ! sock->isConnected()) return;
    juce::String line = msg + "\n";
    sock->write (line.toRawUTF8(), (int) line.getNumBytesAsUTF8());
}

juce::String SignalingServer::getRemoteIP (juce::StreamingSocket* sock)
{
#if JUCE_MAC || JUCE_LINUX
    auto fd = sock->getRawSocketHandle();
    if (fd < 0) return {};

    struct sockaddr_storage addr {};
    socklen_t len = sizeof (addr);
    if (getpeername (fd, (struct sockaddr*) &addr, &len) != 0) return {};

    char buf[INET6_ADDRSTRLEN] = {};
    if (addr.ss_family == AF_INET)
        inet_ntop (AF_INET, &((struct sockaddr_in*) &addr)->sin_addr, buf, sizeof (buf));
    else
        inet_ntop (AF_INET6, &((struct sockaddr_in6*) &addr)->sin6_addr, buf, sizeof (buf));

    juce::String ip (buf);
    if (ip.startsWith ("::ffff:")) ip = ip.substring (7);
    return ip;
#else
    juce::ignoreUnused (sock);
    return {};
#endif
}

juce::String SignalingServer::detectTailscaleIP()
{
#if JUCE_MAC || JUCE_LINUX
    struct ifaddrs* ifap = nullptr;
    if (getifaddrs (&ifap) != 0) return {};

    juce::String result;
    for (auto* p = ifap; p != nullptr; p = p->ifa_next)
    {
        if (p->ifa_addr == nullptr || p->ifa_addr->sa_family != AF_INET) continue;

        auto*    sin  = reinterpret_cast<struct sockaddr_in*> (p->ifa_addr);
        uint32_t addr = ntohl (sin->sin_addr.s_addr);

        // Tailscale allocates from 100.64.0.0/10 (CGNAT range)
        if ((addr & 0xFFC00000u) == 0x64400000u)
        {
            char buf[INET_ADDRSTRLEN] = {};
            inet_ntop (AF_INET, &sin->sin_addr, buf, sizeof (buf));
            result = buf;
            break;
        }
    }

    freeifaddrs (ifap);
    return result;
#else
    return {};
#endif
}
