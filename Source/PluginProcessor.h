#pragma once

#define COLLABSYNC_VERSION "0.7.0"

// Default Cloudflare Worker signaling endpoint (up to and including "/rtc").
// Override at build time with -DCOLLABSYNC_SIGNALING_URL="wss://.../rtc", or
// type a ws(s):// URL into the plugin's host field to override at runtime.
#ifndef COLLABSYNC_SIGNALING_URL
 #define COLLABSYNC_SIGNALING_URL "wss://collabsync-signaling.example.workers.dev/rtc"
#endif

#include <JuceHeader.h>
#include "Network/PeerConnection.h"
#include "Audio/OpusCodec.h"
#include "Audio/JitterBuffer.h"
#include "Audio/SharedAudioBuffer.h"
#include "Midi/MidiCapture.h"
#include "Clock/SessionClock.h"
#include "Session/Recorder.h"
#include <mutex>
#include <thread>
#include <condition_variable>

class CollabSyncProcessor : public juce::AudioProcessor,
                             public PeerConnection::Listener,
                             public juce::MidiInputCallback
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

    // Session control (called from editor). `host` is the signaling endpoint:
    // if it is a ws(s):// URL it overrides the configured Worker URL; otherwise
    // the configured Worker URL is used. Both peers share the same roomCode.
    void connect       (const juce::String& roomCode, const juce::String& host);
    juce::String signalingHost { COLLABSYNC_SIGNALING_URL };
    void disconnect    ();
    void triggerRecord (); // sends countdown to both peers, then starts recording
    void triggerStop   (); // stops recording on both peers

    // Session hosting. With the Cloudflare Worker model there is no local server
    // to run — "hosting" simply means we joined the room first (ICE controlling
    // side). These keep the previous editor's API; some values are vestigial in
    // the new model (see PluginProcessor.cpp) pending the UI redesign merge.
    void startSessionServer();
    void stopSessionServer();
    bool isHostingSession()    const;
    int  getSessionPeerCount() const;
    juce::String getSessionTailscaleIP()  const; // vestigial: no per-peer IP in the ICE model
    juce::String getSessionErrorMessage() const;
    juce::String getLocalTailscaleIP()    const; // vestigial: room code replaces IP sharing

    // PeerConnection::Listener — countdown/stop
    void onCountdownReceived (float bpm) override;
    void onStopReceived      ()          override;

    // MidiInputCallback — direct system MIDI (CoreMIDI, bypasses DAW)
    void handleIncomingMidiMessage (juce::MidiInput* source,
                                    const juce::MidiMessage& message) override;

    // Direct MIDI device management
    void openMidiInputs();
    void closeMidiInputs();
    bool hasDirectMidiInputs() const { return ! midiInputDevices.empty(); }

    // State queries
    bool  isConnected()      const { return peerConnected.load(); }
    bool  isRecording()      const { return recording.load(); }
    bool  isCountingDown()   const { return countdownBeat.load() > 0; }
    int   getCountdownBeat() const { return countdownBeat.load(); }
    float getLatencyMs()     const;
    bool  anyMidiNoteHeld() const
    {
        for (int i = 0; i < 128; ++i)
            if (midiNoteState[i].load (std::memory_order_relaxed)) return true;
        return false;
    }

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

    // Lock-free queue: MIDI callback → processBlock → Recorder
    // Keeps the MIDI callback non-blocking so CoreMIDI doesn't drop note-offs.
    juce::AbstractFifo       midiRecordFifo  { 2048 };
    std::vector<MidiPacket>  midiRecordQueue;

    // Core components
    std::unique_ptr<PeerConnection>   peer;
    std::atomic<bool>                 hosting { false }; // we initiated the room (ICE controlling)
    std::unique_ptr<OpusEncoder>    encoder;
    std::unique_ptr<OpusDecoder>    decoder;
    std::unique_ptr<JitterBuffer>   jitterBuffer;
    std::unique_ptr<MidiCapture>    midiCapture;
    std::unique_ptr<SessionClock>   clock;
    std::unique_ptr<Recorder>       recorder;

    // Background thread for encoding + sending audio
    std::atomic<bool> sendThreadRunning { false };
    std::thread       audioSendThread;
    std::mutex              sendCvMutex;
    std::condition_variable sendCv;       // notified from processBlock when data is written

    // Pending remote MIDI to inject in next processBlock
    std::mutex remoteMidiMutex;
    std::vector<juce::MidiMessage> pendingRemoteMidi;

    std::atomic<bool> peerConnected       { false };
    std::atomic<bool> recording           { false };
    std::atomic<int>  countdownBeat       { 0 };   // 4,3,2,1 then 0 = recording
    bool              disconnecting       { false }; // re-entrancy guard for connect/disconnect
    std::atomic<bool> encoderResetPending { false }; // set by onPeerConnected, consumed by send thread
    std::atomic<bool> decoderResetPending { false }; // set by onPeerConnected, consumed by recv thread

    std::unique_ptr<juce::Thread> countdownThread;

    // Direct system MIDI input (CoreMIDI)
    std::vector<std::unique_ptr<juce::MidiInput>> midiInputDevices;

    // Sample rate resampling (Opus always operates at 48kHz)
    bool needsResampling = false;
    int  daqFrameSamples = 480;  // Opus frame size in DAW-rate samples
    juce::LagrangeInterpolator sendResamplerL, sendResamplerR;
    juce::LagrangeInterpolator recvResamplerL, recvResamplerR;

    // Pre-buffering: accumulate remote audio before playback to avoid choppy output
    std::atomic<bool> receiveBufferPrimed { false };

    // Pre-allocated buffers to avoid heap allocations on audio/network threads
    std::vector<float> processBlockInterleaved;   // processBlock: capture local audio
    std::vector<float> processBlockRemote;         // processBlock: receive remote audio
    std::vector<float> processBlockDiscard;        // processBlock: overflow/drain discard
    std::vector<float> decodePcm;                  // onAudioPacketReceived: decode output
    std::vector<float> recvResampleInL, recvResampleInR;   // recv-thread resampler scratch
    std::vector<float> recvResampleOutL, recvResampleOutR;
    std::vector<float> recvResampleInterleaved;

    // Sequence tracking on the receive side — drives PLC/FEC for missing frames
    uint32_t lastDecodedSeq    = 0;
    bool     haveLastSeq       = false;
    int      consecutiveUnderruns = 0; // smaller bumps are just skipped, not re-primed

public:
    // Diagnostics (public for editor access)
    std::atomic<uint32_t> packetsReceived   { 0 };
    std::atomic<uint32_t> decodeSuccesses   { 0 };
    std::atomic<uint32_t> decodeFailures    { 0 };
    std::atomic<uint32_t> bufferUnderruns   { 0 };
    std::atomic<int>      recvBufferLevel   { 0 };  // current fill in floats
    std::atomic<bool>     midiNoteState[128] {};  // per-note held tracking (lock-free)
private:

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CollabSyncProcessor)
};
