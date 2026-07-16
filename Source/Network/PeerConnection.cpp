#include "PeerConnection.h"
#include "../Clock/SessionClock.h"
#include <cstring>

// Public STUN server used for server-reflexive candidate discovery (finding
// each peer's public IP:port so the two NATs can be hole-punched). STUN is
// stateless and free to use; no account or hosting required.
static constexpr const char* kStunHost = "stun.l.google.com";
static constexpr uint16_t    kStunPort = 19302;

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
// libjuice C callback trampolines — dispatch to the owning PeerConnection.
void PeerConnection::cbState (juice_agent_t*, juice_state_t state, void* user)
{
    static_cast<PeerConnection*> (user)->onIceStateChanged ((int) state);
}

void PeerConnection::cbCandidate (juice_agent_t*, const char* sdp, void* user)
{
    auto* self = static_cast<PeerConnection*> (user);
    if (self->signaling)
        self->signaling->sendCandidate (sdp);
}

void PeerConnection::cbGathering (juice_agent_t*, void* /*user*/)
{
    // Local gathering finished. We rely on ICE connectivity checks rather than
    // an explicit end-of-candidates signal, so nothing to do here.
}

void PeerConnection::cbRecv (juice_agent_t*, const char* data, size_t size, void* user)
{
    static_cast<PeerConnection*> (user)->onDatagram (
        reinterpret_cast<const uint8_t*> (data), size);
}

//==============================================================================
PeerConnection::~PeerConnection()
{
    alive->store (false); // neutralise any deferred work still queued
    disconnect();
}

bool PeerConnection::connect (const juce::String& signalingServerUrl, const juce::String& roomCodeIn)
{
    if (! PeerCrypto::init())
    {
        setStatus ("ERROR: crypto init failed");
        return false;
    }

    roomCode = roomCodeIn.toUpperCase();
    controlling.store (false);
    iceConnected.store (false);
    remoteDescSet.store (false);
    announced.store (false);
    connected.store (false);
    audioSeq = midiSeq = 0;

    signaling = std::make_unique<SignalingClient>();

    SignalingClient::Callbacks cbs;
    cbs.onRole              = [this] (bool isHost)
    {
        controlling.store (isHost);
        setStatus (isHost ? "Hosting — waiting for peer…" : "Joining…");
    };
    cbs.onWaiting           = [this] { setStatus ("Waiting for peer…"); };
    cbs.onPeerJoined        = [this] { startIce(); };
    cbs.onPeerLeft          = [this]
    {
        setStatus ("Peer left");
        connected.store (false);
        listener->onPeerDisconnected();
    };
    cbs.onRemoteDescription = [this] (const std::string& sdp) { onRemoteDescription (sdp); };
    cbs.onRemoteCandidate   = [this] (const std::string& c)   { onRemoteCandidate (c); };
    cbs.onRemotePubkey      = [this] (const std::vector<uint8_t>& pk) { onRemotePubkey (pk); };
    cbs.onError             = [this] (const std::string& r)
    {
        // room_full is the expected "two people are already in there" case
        // rather than a fault, so it gets plain language instead of a raw code.
        if (r == "room_full")
            setStatus ("Server at capacity — a session is already in progress");
        else
            setStatus ("Signaling error: " + juce::String (r));
    };
    cbs.onClosed            = [] { /* expected once the P2P link is up */ };

    setStatus ("Connecting to signaling server…");
    return signaling->connect (signalingServerUrl, roomCode, std::move (cbs));
}

void PeerConnection::startIce()
{
    std::lock_guard<std::mutex> lock (agentMutex);
    if (agent != nullptr)
        return; // already started

    juice_config_t config;
    std::memset (&config, 0, sizeof (config));
    config.concurrency_mode  = JUICE_CONCURRENCY_MODE_THREAD;
    config.stun_server_host  = kStunHost;
    config.stun_server_port  = kStunPort;
    config.cb_state_changed  = cbState;
    config.cb_candidate      = cbCandidate;
    config.cb_gathering_done = cbGathering;
    config.cb_recv           = cbRecv;
    config.user_ptr          = this;

    agent = juice_create (&config);
    if (agent == nullptr)
    {
        setStatus ("ERROR: could not create ICE agent");
        return;
    }

    // Announce our encryption public key to the peer.
    if (signaling)
        signaling->sendPubkey (crypto.localPublicKey());

    // Send our local ICE description; then gather candidates (which trickle out
    // via cbCandidate).
    char sdp[JUICE_MAX_SDP_STRING_LEN];
    if (juice_get_local_description (agent, sdp, sizeof (sdp)) == 0 && signaling)
        signaling->sendDescription (sdp);

    juice_gather_candidates (agent);
    setStatus ("Negotiating connection…");
}

void PeerConnection::onRemoteDescription (const std::string& sdp)
{
    std::lock_guard<std::mutex> lock (agentMutex);
    if (agent == nullptr)
        return;

    if (juice_set_remote_description (agent, sdp.c_str()) == 0)
    {
        remoteDescSet.store (true);

        // Flush any candidates that arrived before the remote description.
        std::vector<std::string> pend;
        {
            std::lock_guard<std::mutex> pl (pendingMutex);
            pend.swap (pendingCandidates);
        }
        for (const auto& c : pend)
            juice_add_remote_candidate (agent, c.c_str());
    }
}

void PeerConnection::onRemoteCandidate (const std::string& candidate)
{
    if (! remoteDescSet.load())
    {
        // Remote description not set yet — buffer until it is, or juice would
        // reject the candidate.
        std::lock_guard<std::mutex> pl (pendingMutex);
        pendingCandidates.push_back (candidate);
        return;
    }

    std::lock_guard<std::mutex> lock (agentMutex);
    if (agent != nullptr)
        juice_add_remote_candidate (agent, candidate.c_str());
}

void PeerConnection::onRemotePubkey (const std::vector<uint8_t>& pk)
{
    // Key agreement needs only the peer's public key, our keypair, our role,
    // and the shared room code — independent of ICE progress.
    if (crypto.deriveSharedKeys (pk, controlling.load(), roomCode.toStdString()))
        tryAnnounceConnected();
    else
        setStatus ("ERROR: key exchange failed");
}

void PeerConnection::onIceStateChanged (int state)
{
    switch (state)
    {
        case JUICE_STATE_GATHERING:
            setStatus ("Gathering candidates…");
            break;
        case JUICE_STATE_CONNECTING:
            setStatus ("Punching through NAT…");
            break;
        case JUICE_STATE_CONNECTED:
        case JUICE_STATE_COMPLETED:
            iceConnected.store (true);
            tryAnnounceConnected();
            // P2P path fixed — signaling no longer needed. We are on the libjuice
            // agent thread here, and close() joins the WebSocket thread, which may
            // itself be waiting on us: that pair deadlocks. Defer so every close
            // runs on the message thread, one at a time.
            if (state == JUICE_STATE_COMPLETED && signaling)
            {
                juce::MessageManager::callAsync ([this, alive = alive]
                {
                    if (alive->load() && signaling)
                        signaling->close();
                });
            }
            break;
        case JUICE_STATE_FAILED:
            setStatus ("Connection failed (could not reach peer)");
            connected.store (false);
            listener->onPeerDisconnected();
            break;
        case JUICE_STATE_DISCONNECTED:
        default:
            break;
    }
}

void PeerConnection::tryAnnounceConnected()
{
    // Announce a live session only once both the ICE path and the encryption
    // keys are ready.
    bool expected = false;
    if (iceConnected.load() && crypto.isReady()
        && announced.compare_exchange_strong (expected, true))
    {
        connected.store (true);
        setStatus ("Connected");
        listener->onPeerConnected();
    }
}

void PeerConnection::onDatagram (const uint8_t* data, size_t size)
{
    std::vector<uint8_t> plain;
    if (! crypto.decrypt (data, size, plain))
        return; // not yet keyed, or authentication failed — drop

    if (plain.size() < sizeof (PacketHeader))
        return;

    auto* hdr                = reinterpret_cast<const PacketHeader*> (plain.data());
    const uint8_t* payload   = plain.data() + sizeof (PacketHeader);
    const size_t payloadSize = juce::jmin ((size_t) hdr->payloadSize,
                                           plain.size() - sizeof (PacketHeader));

    switch (hdr->type)
    {
        case PKT_AUDIO:
            listener->onAudioPacketReceived (payload, payloadSize, hdr->sessionTimeNs);
            break;
        case PKT_MIDI:
            listener->onMidiPacketReceived (payload, payloadSize, hdr->sessionTimeNs);
            break;
        case PKT_COUNTDOWN:
            if (payloadSize >= 4)
            {
                float bpm = 0.0f;
                std::memcpy (&bpm, payload, 4);
                listener->onCountdownReceived (bpm);
            }
            break;
        case PKT_STOP:
            listener->onStopReceived();
            break;
        default:
            break;
    }
}

void PeerConnection::disconnect()
{
    connected.store (false);

    if (signaling)
    {
        signaling->close();
        signaling.reset();
    }

    {
        std::lock_guard<std::mutex> lock (agentMutex);
        if (agent != nullptr)
        {
            juice_destroy (agent);
            agent = nullptr;
        }
    }

    {
        std::lock_guard<std::mutex> pl (pendingMutex);
        pendingCandidates.clear();
    }

    if (listener)
        listener->onPeerDisconnected();
}

//==============================================================================
bool PeerConnection::sendFramed (uint8_t type, const uint8_t* data, size_t size, uint32_t seq)
{
    if (! connected.load() || ! crypto.isReady())
        return false;

    std::vector<uint8_t> framed (sizeof (PacketHeader) + size);
    auto* hdr          = reinterpret_cast<PacketHeader*> (framed.data());
    hdr->type          = type;
    hdr->sequence      = seq;
    hdr->sessionTimeNs = SessionClock::localNow();
    hdr->payloadSize   = (uint16_t) size;
    if (size > 0)
        std::memcpy (framed.data() + sizeof (PacketHeader), data, size);

    const std::vector<uint8_t> enc = crypto.encrypt (framed.data(), framed.size());
    if (enc.empty())
        return false;

    std::lock_guard<std::mutex> lock (agentMutex);
    if (agent == nullptr)
        return false;
    return juice_send (agent, reinterpret_cast<const char*> (enc.data()), enc.size()) >= 0;
}

bool PeerConnection::sendAudio (const uint8_t* data, size_t size)
{
    return sendFramed (PKT_AUDIO, data, size, ++audioSeq);
}

bool PeerConnection::sendMidi (const uint8_t* data, size_t size)
{
    return sendFramed (PKT_MIDI, data, size, ++midiSeq);
}

bool PeerConnection::sendCountdown (float bpm)
{
    return sendFramed (PKT_COUNTDOWN, reinterpret_cast<const uint8_t*> (&bpm), 4, 0);
}

bool PeerConnection::sendStop()
{
    return sendFramed (PKT_STOP, nullptr, 0, 0);
}

void PeerConnection::setStatus (const juce::String& s)
{
    lastStatus = s;
    juce::MessageManager::callAsync ([this, s]
    {
        if (listener)
            listener->onStatusChanged (s);
    });
}
