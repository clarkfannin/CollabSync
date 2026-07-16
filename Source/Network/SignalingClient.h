#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ix { class WebSocket; }

// WebSocket signaling client for the Cloudflare Worker rendezvous server.
//
// Connects to  wss://<worker>/rtc?room=CODE  and exchanges the messages
// described in signaling/worker/README.md. It brokers ICE setup only: the
// server assigns a host/guest role, tells us when the peer is present, and
// relays the peer's ICE description/candidates and public key. No media ever
// passes through it.
//
// All callbacks are invoked from the IXWebSocket network thread; the owner is
// responsible for marshalling to other threads as needed.
class SignalingClient
{
public:
    struct Callbacks
    {
        std::function<void(bool isHost)>                    onRole;              // role assigned (host = ICE controlling)
        std::function<void()>                               onWaiting;           // first in room, waiting for peer
        std::function<void()>                               onPeerJoined;        // both peers present — begin ICE
        std::function<void()>                               onPeerLeft;          // peer disconnected
        std::function<void(const std::string& sdp)>         onRemoteDescription; // peer's libjuice description
        std::function<void(const std::string& candidate)>   onRemoteCandidate;   // a trickled ICE candidate
        std::function<void(const std::vector<uint8_t>& pk)> onRemotePubkey;      // peer's crypto public key
        std::function<void(const std::string& reason)>      onError;             // server/protocol error
        std::function<void()>                               onClosed;            // socket closed
    };

    SignalingClient();
    ~SignalingClient();

    // baseWssUrl is the Worker endpoint up to and including "/rtc"
    // (e.g. "wss://collabsync-signaling.you.workers.dev/rtc"). The room code is
    // appended as a query parameter internally.
    bool connect (const juce::String& baseWssUrl,
                  const juce::String& roomCode,
                  Callbacks cbs);
    void close();

    bool isOpen() const { return open.load(); }

    void sendDescription (const std::string& sdp);
    void sendCandidate   (const std::string& candidate);
    void sendPubkey      (const std::vector<uint8_t>& publicKey);

private:
    void handleMessage (const std::string& text);
    void sendJson      (const juce::var& obj);

    std::unique_ptr<ix::WebSocket> ws;
    Callbacks         callbacks;
    std::atomic<bool> open { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SignalingClient)
};
