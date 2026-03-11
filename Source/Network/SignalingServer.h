#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <mutex>
#include <map>
#include <vector>
#include <thread>

// Embedded TCP signaling server — same wire protocol as the original server.js.
// Runs in-process so users don't need Node.js.
//
// Protocol (text lines):
//   Client → Server:  "JOIN <roomCode> <udpPort>\n"
//   Server → Client:  "WAIT\n"               (first peer in room)
//                     "PEER <ip> <port>\n"    (second peer — sent to both)
//                     "ERROR <reason>\n"

class SignalingServer : private juce::Thread
{
public:
    enum class State { Stopped, Listening, Error };

    SignalingServer();
    ~SignalingServer() override;

    void start();
    void stop();

    State        getState()        const { return state.load(); }
    int          getJoinedCount()  const { return joinedCount.load(); }  // peers in a room
    juce::String getTailscaleIP()  const;
    juce::String getErrorMessage() const;

    // Can be called any time — does not require the server to be running.
    static juce::String detectTailscaleIP();

private:
    void run() override;
    void serveClient (juce::StreamingSocket* sock);

    static void         sendLine    (juce::StreamingSocket* sock, const juce::String& msg);
    static juce::String getRemoteIP (juce::StreamingSocket* sock);

    std::atomic<State> state       { State::Stopped };
    std::atomic<int>   joinedCount { 0 };

    mutable std::mutex metaMutex;
    juce::String       tailscaleIP;
    juce::String       errorMessage;

    struct PeerSlot
    {
        juce::StreamingSocket* socket  = nullptr;
        int                    udpPort = 0;
        juce::String           ip;
    };

    std::mutex                                    roomsMutex;
    std::map<juce::String, std::vector<PeerSlot>> rooms;

    std::mutex                          clientsMutex;
    std::vector<juce::StreamingSocket*> clientSockets;

    juce::WaitableEvent readyEvent { true }; // signalled when listening (or error)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SignalingServer)
};
