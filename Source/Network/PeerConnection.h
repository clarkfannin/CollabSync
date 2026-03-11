#pragma once
#include <JuceHeader.h>
#include <cstdint>
#include <functional>
#include <memory>

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
    Listener* listener = nullptr;

    std::unique_ptr<juce::DatagramSocket> udpSocket;

    std::unique_ptr<juce::Thread> receiveThread;
    std::unique_ptr<juce::Thread> signalingThread;

    juce::String peerHost;
    int          peerPort     = 0;
    int          localUdpPort = 0;

    uint32_t audioSeq = 0;
    uint32_t midiSeq  = 0;

    std::atomic<bool>  connected { false };
    std::atomic<float> rttMs     { 0.0f };
    juce::String       lastStatus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PeerConnection)
};
