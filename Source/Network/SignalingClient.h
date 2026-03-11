#pragma once
#include <JuceHeader.h>
#include <functional>

// Lightweight WebSocket signaling client.
// Connects to the signaling server, joins a room, and exchanges
// ICE candidates / session descriptions for WebRTC setup (Phase 2).
// Phase 1: just exchanges host:port info so both peers can UDP each other.
class SignalingClient
{
public:
    struct Callbacks
    {
        std::function<void(juce::String host, int audioPort, int midiPort)> onPeerInfo;
        std::function<void()> onDisconnected;
    };

    SignalingClient() = default;

    bool connect (const juce::String& serverUrl, const juce::String& roomCode, Callbacks cbs);
    void disconnect();
    bool sendJson (const juce::var& obj);

private:
    // Phase 1: simplified — just use JUCE URL / raw socket for signaling
    // (Full WebSocket signaling deferred to Phase 2 when we add WebRTC)
    Callbacks callbacks;
    juce::String currentRoom;
};
