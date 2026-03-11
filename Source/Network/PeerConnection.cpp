#include "PeerConnection.h"
#include "../Clock/SessionClock.h"
#include <cstring>

static constexpr uint8_t PKT_AUDIO     = 0x01;
static constexpr uint8_t PKT_MIDI      = 0x02;
static constexpr uint8_t PKT_COUNTDOWN = 0x03;
static constexpr uint8_t PKT_STOP      = 0x04;

#pragma pack(push, 1)
struct PacketHeader
{
    uint8_t  type;
    uint32_t sequence;
    uint64_t sessionTimeNs;
    uint16_t payloadSize;
};
#pragma pack(pop)

//==============================================================================
// Background thread: reads from UDP socket and dispatches to listener
class UdpReceiveThread : public juce::Thread
{
public:
    UdpReceiveThread (juce::DatagramSocket* sock, PeerConnection::Listener* listener)
        : juce::Thread ("CollabSync UDP Recv"), socket (sock), listener (listener) {}

    void run() override
    {
        std::vector<uint8_t> buf (65536);
        while (! threadShouldExit())
        {
            if (socket->waitUntilReady (true, 5) != 1) continue;

            juce::String senderHost;
            int senderPort = 0;
            int received = socket->read (buf.data(), (int) buf.size(), false,
                                         senderHost, senderPort);

            if (received < (int) sizeof (PacketHeader)) continue;

            auto* hdr           = reinterpret_cast<const PacketHeader*> (buf.data());
            const uint8_t* payload = buf.data() + sizeof (PacketHeader);
            size_t payloadSize  = hdr->payloadSize;

            if (hdr->type == PKT_AUDIO)
                listener->onAudioPacketReceived (payload, payloadSize, hdr->sessionTimeNs);
            else if (hdr->type == PKT_MIDI)
                listener->onMidiPacketReceived  (payload, payloadSize, hdr->sessionTimeNs);
            else if (hdr->type == PKT_COUNTDOWN && payloadSize >= 4)
            {
                float bpm = 0.0f;
                std::memcpy (&bpm, payload, 4);
                listener->onCountdownReceived (bpm);
            }
            else if (hdr->type == PKT_STOP)
                listener->onStopReceived();
        }
    }

private:
    juce::DatagramSocket* socket;
    PeerConnection::Listener* listener;
};

//==============================================================================
// Background thread: connects to signaling server, waits for peer info
class SignalingThread : public juce::Thread
{
public:
    SignalingThread (const juce::String& host, int port,
                     const juce::String& room, int myUdpPort,
                     std::function<void(juce::String, int)> onPeer,
                     std::function<void(juce::String)>      onStatus)
        : juce::Thread ("CollabSync Signaling")
        , sigHost (host), sigPort (port), room (room), myUdpPort (myUdpPort)
        , onPeer (std::move (onPeer)), onStatus (std::move (onStatus)) {}

    void run() override
    {
        juce::StreamingSocket sock;

        onStatus ("Connecting to signaling server...");

        if (! sock.connect (sigHost, sigPort, 5000))
        {
            onStatus ("ERROR: Cannot reach signaling server");
            return;
        }

        // Send JOIN message
        juce::String joinMsg = "JOIN " + room + " " + juce::String (myUdpPort) + "\n";
        sock.write (joinMsg.toRawUTF8(), (int) joinMsg.getNumBytesAsUTF8());

        onStatus ("Waiting for peer...");

        // Read lines from server
        juce::String lineBuffer;
        while (! threadShouldExit())
        {
            if (sock.waitUntilReady (true, 100) != 1) continue;

            char ch = 0;
            if (sock.read (&ch, 1, false) <= 0) break;

            if (ch == '\n')
            {
                juce::String line = lineBuffer.trim();
                lineBuffer.clear();

                if (line.startsWith ("PEER "))
                {
                    // PEER <ip> <port>
                    juce::StringArray parts = juce::StringArray::fromTokens (line, " ", "");
                    if (parts.size() >= 3)
                    {
                        juce::String peerIp   = parts[1];
                        int          peerPort = parts[2].getIntValue();
                        onPeer (peerIp, peerPort);
                    }
                    return; // done — peer found
                }
                else if (line.startsWith ("WAIT"))
                {
                    onStatus ("Waiting for peer to join room...");
                }
                else if (line.startsWith ("ERROR"))
                {
                    onStatus ("ERROR: " + line.fromFirstOccurrenceOf (" ", false, false));
                    return;
                }
            }
            else
            {
                lineBuffer += ch;
            }
        }
    }

private:
    juce::String sigHost;
    int          sigPort;
    juce::String room;
    int          myUdpPort;
    std::function<void(juce::String, int)> onPeer;
    std::function<void(juce::String)>      onStatus;
};

PeerConnection::~PeerConnection() { disconnect(); }

//==============================================================================
bool PeerConnection::connect (const juce::String& signalingServerUrl, const juce::String& roomCode)
{
    // Pick a random local UDP port in range 40000–49000
    localUdpPort = 40000 + (std::abs (roomCode.hashCode()) % 9000);

    udpSocket = std::make_unique<juce::DatagramSocket>();
    // Try binding; if port taken increment until we find one free
    while (! udpSocket->bindToPort (localUdpPort))
    {
        ++localUdpPort;
        if (localUdpPort > 49999) { udpSocket.reset(); return false; }
    }

    // Parse signaling server URL: "host:port" or just "host" (default port 8765)
    juce::String sigHost = signalingServerUrl;
    int          sigPort = 8765;
    if (signalingServerUrl.contains (":"))
    {
        sigHost = signalingServerUrl.upToLastOccurrenceOf (":", false, false);
        sigPort = signalingServerUrl.fromLastOccurrenceOf (":", false, false).getIntValue();
    }

    signalingThread = std::make_unique<SignalingThread> (
        sigHost, sigPort, roomCode, localUdpPort,
        [this] (juce::String peerIp, int peerPort)      // onPeer
        {
            this->peerHost = peerIp;
            this->peerPort = peerPort;

            // Start UDP receive thread now we know who to expect
            receiveThread = std::make_unique<UdpReceiveThread> (udpSocket.get(), listener);
            receiveThread->startThread (juce::Thread::Priority::high);

            connected.store (true);
            listener->onPeerConnected();
        },
        [this] (juce::String status)                    // onStatus
        {
            lastStatus = status;
            juce::MessageManager::callAsync ([this, status] {
                listener->onStatusChanged (status);
            });
        }
    );
    signalingThread->startThread();
    return true;
}

void PeerConnection::disconnect()
{
    connected.store (false);

    if (signalingThread) { signalingThread->stopThread (1000); signalingThread.reset(); }
    if (receiveThread)   { receiveThread->stopThread   (1000); receiveThread.reset(); }
    udpSocket.reset();

    listener->onPeerDisconnected();
}

bool PeerConnection::sendAudio (const uint8_t* data, size_t size)
{
    if (! connected.load() || ! udpSocket || peerHost.isEmpty()) return false;

    std::vector<uint8_t> pkt (sizeof (PacketHeader) + size);
    auto* hdr           = reinterpret_cast<PacketHeader*> (pkt.data());
    hdr->type           = PKT_AUDIO;
    hdr->sequence       = ++audioSeq;
    hdr->sessionTimeNs  = SessionClock::localNow();
    hdr->payloadSize    = (uint16_t) size;
    std::memcpy (pkt.data() + sizeof (PacketHeader), data, size);

    return udpSocket->write (peerHost, peerPort, pkt.data(), (int) pkt.size()) > 0;
}

bool PeerConnection::sendCountdown (float bpm)
{
    if (! connected.load() || ! udpSocket || peerHost.isEmpty()) return false;
    std::vector<uint8_t> pkt (sizeof (PacketHeader) + 4);
    auto* hdr          = reinterpret_cast<PacketHeader*> (pkt.data());
    hdr->type          = PKT_COUNTDOWN;
    hdr->sequence      = 0;
    hdr->sessionTimeNs = SessionClock::localNow();
    hdr->payloadSize   = 4;
    std::memcpy (pkt.data() + sizeof (PacketHeader), &bpm, 4);
    return udpSocket->write (peerHost, peerPort, pkt.data(), (int) pkt.size()) > 0;
}

bool PeerConnection::sendStop()
{
    if (! connected.load() || ! udpSocket || peerHost.isEmpty()) return false;
    std::vector<uint8_t> pkt (sizeof (PacketHeader));
    auto* hdr          = reinterpret_cast<PacketHeader*> (pkt.data());
    hdr->type          = PKT_STOP;
    hdr->sequence      = 0;
    hdr->sessionTimeNs = SessionClock::localNow();
    hdr->payloadSize   = 0;
    return udpSocket->write (peerHost, peerPort, pkt.data(), (int) pkt.size()) > 0;
}

bool PeerConnection::sendMidi (const uint8_t* data, size_t size)
{
    if (! connected.load() || ! udpSocket || peerHost.isEmpty()) return false;

    std::vector<uint8_t> pkt (sizeof (PacketHeader) + size);
    auto* hdr           = reinterpret_cast<PacketHeader*> (pkt.data());
    hdr->type           = PKT_MIDI;
    hdr->sequence       = ++midiSeq;
    hdr->sessionTimeNs  = SessionClock::localNow();
    hdr->payloadSize    = (uint16_t) size;
    std::memcpy (pkt.data() + sizeof (PacketHeader), data, size);

    return udpSocket->write (peerHost, peerPort, pkt.data(), (int) pkt.size()) > 0;
}
