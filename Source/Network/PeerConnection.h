#pragma once
#include <JuceHeader.h>
#include "SignalingClient.h"
#include "PeerCrypto.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <juice/juice.h>

// Peer-to-peer transport between two CollabSync instances.
//
// Establishes a direct UDP path across NATs using ICE/STUN (libjuice), with the
// two peers introduced by the Cloudflare Worker signaling server. All media is
// encrypted with libsodium (see PeerCrypto). The public interface is unchanged
// from the previous Tailscale-based implementation, so the processor is
// agnostic to how the bytes get across.
class PeerConnection
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void onAudioPacketReceived (const uint8_t* data, size_t size, uint64_t sessionTimeNs) = 0;
        virtual void onMidiPacketReceived  (const uint8_t* data, size_t size, uint64_t sessionTimeNs) = 0;
        virtual void onCountdownReceived   (float bpm) = 0;
        virtual void onStopReceived        () = 0;
        virtual void onPeerConnected()    = 0;
        virtual void onPeerDisconnected() = 0;
        virtual void onStatusChanged (const juce::String& status) = 0;
    };

    PeerConnection (Listener* listener) : listener (listener) {}
    ~PeerConnection();

    // signalingServerUrl is the Worker WSS endpoint up to "/rtc"
    // (e.g. "wss://collabsync-signaling.you.workers.dev/rtc"). Both peers pass
    // the same roomCode; the server pairs them.
    bool connect (const juce::String& signalingServerUrl, const juce::String& roomCode);
    void disconnect();

    bool sendAudio     (const uint8_t* data, size_t size);
    bool sendMidi      (const uint8_t* data, size_t size);
    bool sendCountdown (float bpm);
    bool sendStop      ();

    bool         isConnected() const { return connected.load(); }
    float        getRttMs()    const { return rttMs.load(); }
    juce::String getStatus()   const { return lastStatus; }

private:
    // libjuice C callbacks (trampoline to member functions via user_ptr).
    static void cbState     (juice_agent_t*, juice_state_t state, void* user);
    static void cbCandidate (juice_agent_t*, const char* sdp, void* user);
    static void cbGathering (juice_agent_t*, void* user);
    static void cbRecv      (juice_agent_t*, const char* data, size_t size, void* user);

    void startIce();
    void onRemoteDescription (const std::string& sdp);
    void onRemoteCandidate   (const std::string& candidate);
    void onRemotePubkey      (const std::vector<uint8_t>& pk);
    void onIceStateChanged   (int state);
    void onDatagram          (const uint8_t* data, size_t size);
    void tryAnnounceConnected();
    void setStatus (const juce::String& s);
    bool sendFramed (uint8_t type, const uint8_t* data, size_t size, uint32_t seq);

    Listener* listener = nullptr;

    std::unique_ptr<SignalingClient> signaling;
    PeerCrypto crypto;

    std::mutex   agentMutex;         // guards agent lifetime vs. the audio send path
    juice_agent* agent = nullptr;

    juce::String roomCode;

    std::atomic<bool> controlling     { false }; // host = ICE controlling side
    std::atomic<bool> iceConnected    { false };
    std::atomic<bool> remoteDescSet   { false };
    std::atomic<bool> announced       { false };
    std::atomic<bool> connected       { false };
    std::atomic<float> rttMs          { 0.0f };

    std::mutex               pendingMutex;
    std::vector<std::string> pendingCandidates; // buffered until remote desc is set

    uint32_t audioSeq = 0;
    uint32_t midiSeq  = 0;

    juce::String lastStatus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeerConnection)
};
