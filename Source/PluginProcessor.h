#pragma once

#define COLLABSYNC_VERSION "0.4.2"

#include <JuceHeader.h>
#include "Network/PeerConnection.h"
#include "Network/SignalingServer.h"
#include "Audio/OpusCodec.h"
#include "Audio/JitterBuffer.h"
#include "Audio/SharedAudioBuffer.h"
#include "Midi/MidiCapture.h"
#include "Clock/SessionClock.h"
#include "Session/Recorder.h"
#include <mutex>
#include <thread>

class CollabSyncProcessor : public juce::AudioProcessor,
                             public PeerConnection::Listener
{
public:
    CollabSyncProcessor();
    ~CollabSyncProcessor() override;

    // AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "CollabSync"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    // PeerConnection::Listener
    void onAudioPacketReceived (const uint8_t* data, size_t size, uint64_t sessionTimeNs) override;
    void onMidiPacketReceived  (const uint8_t* data, size_t size, uint64_t sessionTimeNs) override;
    void onPeerConnected()    override;
    void onPeerDisconnected() override;
    void onStatusChanged (const juce::String& status) override;

    // Session control (called from editor)
    void connect       (const juce::String& roomCode, const juce::String& host);
    juce::String signalingHost { "localhost" };
    void disconnect    ();
    void triggerRecord (); // sends countdown to both peers, then starts recording
    void triggerStop   (); // stops recording on both peers

    // Session hosting (built-in signaling server)
    void startSessionServer();
    void stopSessionServer();
    bool isHostingSession()    const;
    int  getSessionPeerCount() const;
    juce::String getSessionTailscaleIP()  const;
    juce::String getSessionErrorMessage() const;
    juce::String getLocalTailscaleIP()    const; // works even when server is not running

    // PeerConnection::Listener — countdown/stop
    void onCountdownReceived (float bpm) override;
    void onStopReceived      ()          override;

    // State queries
    bool  isConnected()      const { return peerConnected.load(); }
    bool  isRecording()      const { return recording.load(); }
    bool  isCountingDown()   const { return countdownBeat.load() > 0; }
    int   getCountdownBeat() const { return countdownBeat.load(); }
    float getLatencyMs()     const;

    juce::ListenerList<juce::ChangeListener> stateListeners;
    juce::String currentStatus;
    juce::File   lastSessionDir;

private:
    void sendAudioLoop();   // background thread body
    void sendMidiLoop();

    double currentSampleRate = 48000.0;
    int    currentBlockSize  = 128;

    // Threading
    SharedAudioBuffer sendBuffer;     // audio thread writes, send thread reads
    SharedAudioBuffer receiveBuffer;  // receive thread writes, audio thread reads

    juce::AbstractFifo midiSendFifo  { 1024 };
    juce::AbstractFifo midiRecvFifo  { 1024 };
    std::vector<uint8_t> midiSendData;
    std::vector<uint8_t> midiRecvData;

    // Core components
    std::unique_ptr<PeerConnection>   peer;
    std::unique_ptr<SignalingServer>  signalingServer;
    std::unique_ptr<OpusEncoder>    encoder;
    std::unique_ptr<OpusDecoder>    decoder;
    std::unique_ptr<JitterBuffer>   jitterBuffer;
    std::unique_ptr<MidiCapture>    midiCapture;
    std::unique_ptr<SessionClock>   clock;
    std::unique_ptr<Recorder>       recorder;

    // Background thread for encoding + sending audio
    std::atomic<bool> sendThreadRunning { false };
    std::thread       audioSendThread;

    // Pending remote MIDI to inject in next processBlock
    std::mutex remoteMidiMutex;
    std::vector<juce::MidiMessage> pendingRemoteMidi;

    std::atomic<bool> peerConnected  { false };
    std::atomic<bool> recording      { false };
    std::atomic<int>  countdownBeat  { 0 };   // 4,3,2,1 then 0 = recording

    std::unique_ptr<juce::Thread> countdownThread;

    // Sample rate resampling (Opus always operates at 48kHz)
    bool needsResampling = false;
    int  daqFrameSamples = 480;  // Opus frame size in DAW-rate samples
    juce::LagrangeInterpolator sendResamplerL, sendResamplerR;
    juce::LagrangeInterpolator recvResamplerL, recvResamplerR;

    // Pre-buffering: accumulate remote audio before playback to avoid choppy output
    std::atomic<bool> receiveBufferPrimed { false };

public:
    // Diagnostics (public for editor access)
    std::atomic<uint32_t> packetsReceived   { 0 };
    std::atomic<uint32_t> decodeSuccesses   { 0 };
    std::atomic<uint32_t> decodeFailures    { 0 };
    std::atomic<uint32_t> bufferUnderruns   { 0 };
    std::atomic<int>      recvBufferLevel   { 0 };  // current fill in floats
private:

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CollabSyncProcessor)
};
