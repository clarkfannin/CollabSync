#include "SignalingClient.h"

bool SignalingClient::connect (const juce::String& /*serverUrl*/,
                               const juce::String& roomCode,
                               Callbacks cbs)
{
    // Phase 1 stub: P2P connection info is derived locally from room code.
    // Phase 2 will open a real WebSocket to the signaling server.
    callbacks   = std::move (cbs);
    currentRoom = roomCode;
    return true;
}

void SignalingClient::disconnect() {}

bool SignalingClient::sendJson (const juce::var& /*obj*/) { return true; }
