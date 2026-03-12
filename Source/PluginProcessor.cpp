#include "PluginProcessor.h"
#include "PluginEditor.h"

CollabSyncProcessor::CollabSyncProcessor()
    : AudioProcessor (BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    signalingServer = std::make_unique<SignalingServer>();
    clock      = std::make_unique<SessionClock>();
    midiCapture = std::make_unique<MidiCapture>();
    jitterBuffer = std::make_unique<JitterBuffer> (20, 80); // 20ms target, 80ms max
    recorder   = std::make_unique<Recorder>();

    // Wire MIDI capture to send over network
    midiCapture->setSendCallback ([this] (const MidiPacket& pkt)
    {
        if (peer && peer->isConnected())
            peer->sendMidi (reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
        if (recorder->isRecording())
            recorder->appendLocalMidi (pkt);
    });
}

CollabSyncProcessor::~CollabSyncProcessor()
{
    if (peer) peer->disconnect();
}

void CollabSyncProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize  = samplesPerBlock;

    int capacitySamples = static_cast<int>(sampleRate * 1.0); // 1 second ring buffer
    sendBuffer    = SharedAudioBuffer (capacitySamples);
    receiveBuffer = SharedAudioBuffer (capacitySamples);

    try
    {
        encoder = std::make_unique<OpusEncoder> (48000, 2, 5.0f); // 5ms frames for lowest latency
        decoder = std::make_unique<OpusDecoder> (48000, 2);
    }
    catch (const std::exception& e)
    {
        DBG ("Opus init failed: " << e.what());
    }

    // Set up resampling if DAW sample rate differs from Opus's 48kHz
    needsResampling = (std::abs (sampleRate - 48000.0) > 1.0);
    if (needsResampling && encoder)
    {
        daqFrameSamples = (int) std::ceil (encoder->getFrameSizeSamples() * sampleRate / 48000.0);
        sendResamplerL.reset();
        sendResamplerR.reset();
        recvResamplerL.reset();
        recvResamplerR.reset();
    }
    else if (encoder)
    {
        daqFrameSamples = encoder->getFrameSizeSamples();
    }

    jitterBuffer->prepare (sampleRate, samplesPerBlock);
    receiveBufferPrimed.store (false);

    // Audio send thread: drains sendBuffer, encodes, sends
    sendThreadRunning.store (true);
    audioSendThread = std::thread ([this]
    {
        if (! encoder) return;
        int opusFrameSamples = encoder->getFrameSizeSamples(); // 480 @ 48kHz
        int daqFrame         = daqFrameSamples;
        int readSize         = daqFrame * 2; // stereo interleaved
        std::vector<float> frame (static_cast<size_t> (readSize));
        std::vector<float> opusFrame;
        if (needsResampling)
            opusFrame.resize (static_cast<size_t> (opusFrameSamples * 2));
        uint32_t seq = 0;

        while (sendThreadRunning.load())
        {
            if (sendBuffer.getNumAvailableToRead() >= readSize)
            {
                sendBuffer.read (frame.data(), readSize);

                const float* encodeData  = frame.data();
                int          encodeSamps = daqFrame;

                if (needsResampling)
                {
                    // Deinterleave → resample each channel → reinterleave
                    std::vector<float> inL (static_cast<size_t> (daqFrame));
                    std::vector<float> inR (static_cast<size_t> (daqFrame));
                    std::vector<float> outL (static_cast<size_t> (opusFrameSamples));
                    std::vector<float> outR (static_cast<size_t> (opusFrameSamples));

                    for (int i = 0; i < daqFrame; ++i)
                    {
                        inL[static_cast<size_t> (i)] = frame[static_cast<size_t> (i * 2)];
                        inR[static_cast<size_t> (i)] = frame[static_cast<size_t> (i * 2 + 1)];
                    }

                    double ratio = currentSampleRate / 48000.0;
                    sendResamplerL.process (ratio, inL.data(), outL.data(),
                                           opusFrameSamples, daqFrame, 0);
                    sendResamplerR.process (ratio, inR.data(), outR.data(),
                                           opusFrameSamples, daqFrame, 0);

                    for (int i = 0; i < opusFrameSamples; ++i)
                    {
                        opusFrame[static_cast<size_t> (i * 2)]     = outL[static_cast<size_t> (i)];
                        opusFrame[static_cast<size_t> (i * 2 + 1)] = outR[static_cast<size_t> (i)];
                    }

                    encodeData  = opusFrame.data();
                    encodeSamps = opusFrameSamples;
                }

                auto encoded = encoder->encode (encodeData, encodeSamps);
                if (! encoded.empty() && peer && peer->isConnected())
                {
                    uint64_t t = clock->sessionNow();
                    std::vector<uint8_t> pkt (sizeof(uint32_t) + sizeof(uint64_t) + encoded.size());
                    std::memcpy (pkt.data(), &seq, 4);
                    std::memcpy (pkt.data() + 4, &t, 8);
                    std::memcpy (pkt.data() + 12, encoded.data(), encoded.size());
                    peer->sendAudio (pkt.data(), pkt.size());
                    ++seq;
                }
            }
            else
            {
                // Use microsecond sleep to avoid macOS timer coalescing
                // (sleep_for(1ms) can actually sleep 10-15ms on macOS)
                std::this_thread::sleep_for (std::chrono::microseconds (200));
            }
        }
    });
}

void CollabSyncProcessor::releaseResources()
{
    sendThreadRunning.store (false);
    if (audioSendThread.joinable())
        audioSendThread.join();
    encoder.reset();
    decoder.reset();
}

void CollabSyncProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalChannels = getTotalNumOutputChannels();

    // --- Capture local audio → send ring buffer (interleaved stereo) ---
    {
        int numSamples = buffer.getNumSamples();
        std::vector<float> interleaved (static_cast<size_t>(numSamples * 2));
        const float* L = buffer.getReadPointer (0);
        const float* R = totalChannels > 1 ? buffer.getReadPointer (1) : L;
        for (int i = 0; i < numSamples; ++i)
        {
            interleaved[static_cast<size_t>(i * 2)]     = L[i];
            interleaved[static_cast<size_t>(i * 2 + 1)] = R[i];
        }
        sendBuffer.write (interleaved.data(), numSamples * 2);

        if (recorder->isRecording())
            recorder->appendLocalAudio (interleaved.data(), numSamples);
    }

    // --- Inject received remote audio from receive ring buffer ---
    {
        int numSamples = buffer.getNumSamples();
        int needed     = numSamples * 2; // stereo interleaved

        double nowMs = juce::Time::getMillisecondCounterHiRes();
        jitterBuffer->onAudioCallback (nowMs);

        float targetMs      = jitterBuffer->getTargetMs();
        int   preBufferStereo = (int) (currentSampleRate * targetMs / 1000.0) * 2;
        int   overflowStereo  = (int) (currentSampleRate * targetMs / 1000.0 * 2.5) * 2;

        // Pre-buffer: accumulate data before starting playback to absorb jitter.
        bool primed   = receiveBufferPrimed.load (std::memory_order_relaxed);
        int  available = receiveBuffer.getNumAvailableToRead();

        if (! primed)
        {
            if (available >= preBufferStereo + needed)
            {
                // Drain excess — keep only the target amount to minimise latency.
                int excess = available - preBufferStereo - needed;
                if (excess > 0)
                {
                    std::vector<float> discard (static_cast<size_t> (excess));
                    receiveBuffer.read (discard.data(), excess);
                    available -= excess;
                }
                receiveBufferPrimed.store (true, std::memory_order_relaxed);
            }
        }
        else if (available > overflowStereo)
        {
            // Latency creeping up — skip ahead to keep it bounded
            int target = preBufferStereo + needed;
            int skip   = available - target;
            if (skip > 0)
            {
                std::vector<float> discard (static_cast<size_t> (skip));
                receiveBuffer.read (discard.data(), skip);
                available -= skip;
            }
        }
        else if (available < needed)
        {
            // Buffer underrun — notify adaptive controller, re-prime.
            // Clean silence is far better than partial reads + zero-fill (crackling).
            jitterBuffer->notifyDrop (nowMs);
            receiveBufferPrimed.store (false, std::memory_order_relaxed);
            bufferUnderruns.fetch_add (1, std::memory_order_relaxed);
        }

        recvBufferLevel.store (available, std::memory_order_relaxed);

        if (receiveBufferPrimed.load (std::memory_order_relaxed) && available >= needed)
        {
            std::vector<float> remote (static_cast<size_t> (needed));
            receiveBuffer.read (remote.data(), needed);

            float* L = buffer.getWritePointer (0);
            float* R = totalChannels > 1 ? buffer.getWritePointer (1) : nullptr;
            for (int i = 0; i < numSamples; ++i)
            {
                L[i] += remote[static_cast<size_t> (i * 2)];
                if (R) R[i] += remote[static_cast<size_t> (i * 2 + 1)];
            }
        }
    }

    // --- Capture local MIDI → network ---
    if (! midiMessages.isEmpty())
    {
        auto pos = getPlayHead() ? getPlayHead()->getPosition() : juce::Optional<juce::AudioPlayHead::PositionInfo>();
        int64_t blockStart = pos.hasValue() ? pos->getTimeInSamples().orFallback(0) : 0;

        midiCapture->capture (midiMessages, blockStart, currentSampleRate,
                              clock->sessionNow(), 0);
    }

    // --- Inject incoming remote MIDI into output buffer ---
    {
        std::unique_lock<std::mutex> lock (remoteMidiMutex, std::try_to_lock);
        if (lock.owns_lock())
        {
            for (auto& msg : pendingRemoteMidi)
                midiMessages.addEvent (msg, 0);
            pendingRemoteMidi.clear();
        }
    }
}

//==============================================================================
void CollabSyncProcessor::onAudioPacketReceived (const uint8_t* data, size_t size, uint64_t /*sessionTimeNs*/)
{
    packetsReceived.fetch_add (1, std::memory_order_relaxed);

    if (size < 12 || ! decoder) return;

    // Opus data starts at byte 12 (after 4-byte seq + 8-byte timestamp in payload)
    const uint8_t* opusData = data + 12;
    int            opusSize = (int) size - 12;
    if (opusSize <= 0) return;

    std::vector<float> pcm (static_cast<size_t> (2 * 5760));
    int frames = decoder->decode (opusData, opusSize, pcm.data(), 5760);
    if (frames <= 0)
    {
        decodeFailures.fetch_add (1, std::memory_order_relaxed);
        return;
    }
    decodeSuccesses.fetch_add (1, std::memory_order_relaxed);

    {
        if (needsResampling)
        {
            // Opus decoded at 48kHz → resample to DAW rate
            int outSamples = (int) (frames * currentSampleRate / 48000.0);
            std::vector<float> inL (static_cast<size_t> (frames));
            std::vector<float> inR (static_cast<size_t> (frames));
            for (int i = 0; i < frames; ++i)
            {
                inL[static_cast<size_t> (i)] = pcm[static_cast<size_t> (i * 2)];
                inR[static_cast<size_t> (i)] = pcm[static_cast<size_t> (i * 2 + 1)];
            }
            std::vector<float> outL (static_cast<size_t> (outSamples));
            std::vector<float> outR (static_cast<size_t> (outSamples));
            double ratio = 48000.0 / currentSampleRate;
            recvResamplerL.process (ratio, inL.data(), outL.data(),
                                   outSamples, frames, 0);
            recvResamplerR.process (ratio, inR.data(), outR.data(),
                                   outSamples, frames, 0);
            std::vector<float> resampled (static_cast<size_t> (outSamples * 2));
            for (int i = 0; i < outSamples; ++i)
            {
                resampled[static_cast<size_t> (i * 2)]     = outL[static_cast<size_t> (i)];
                resampled[static_cast<size_t> (i * 2 + 1)] = outR[static_cast<size_t> (i)];
            }
            receiveBuffer.write (resampled.data(), outSamples * 2);
            if (recorder->isRecording())
                recorder->appendRemoteAudio (resampled.data(), outSamples);
        }
        else
        {
            receiveBuffer.write (pcm.data(), frames * 2);
            if (recorder->isRecording())
                recorder->appendRemoteAudio (pcm.data(), frames);
        }
    }
}

void CollabSyncProcessor::onMidiPacketReceived (const uint8_t* data, size_t size, uint64_t /*sessionTimeNs*/)
{
    if (size < sizeof (MidiPacket)) return;
    auto* pkt = reinterpret_cast<const MidiPacket*>(data);

    if (recorder->isRecording())
        recorder->appendRemoteMidi (*pkt);

    auto msg = MidiCapture::toJuceMessage (*pkt);
    std::lock_guard<std::mutex> lock (remoteMidiMutex);
    pendingRemoteMidi.push_back (msg);
}

void CollabSyncProcessor::onPeerConnected()
{
    peerConnected.store (true);
    receiveBufferPrimed.store (false);
    receiveBuffer.reset();
    jitterBuffer->reset();
    clock->setSessionStart (SessionClock::localNow());
    juce::MessageManager::callAsync ([this]
    {
        stateListeners.call ([] (juce::ChangeListener& l) { l.changeListenerCallback (nullptr); });
    });
}

void CollabSyncProcessor::onPeerDisconnected()
{
    peerConnected.store (false);

    // If we're still hosting, rejoin the signaling server so we're ready
    // for a new friend to connect without the host needing to do anything.
    // Guard: skip if we're already inside connect()/disconnect() to avoid infinite recursion.
    if (! disconnecting && signalingServer->getState() == SignalingServer::State::Listening)
        connect ("SYNC", "localhost");

    juce::MessageManager::callAsync ([this]
    {
        stateListeners.call ([] (juce::ChangeListener& l) { l.changeListenerCallback (nullptr); });
    });
}

void CollabSyncProcessor::onStatusChanged (const juce::String& status)
{
    currentStatus = status;
    juce::MessageManager::callAsync ([this]
    {
        stateListeners.call ([] (juce::ChangeListener& l) { l.changeListenerCallback (nullptr); });
    });
}

//==============================================================================
void CollabSyncProcessor::connect (const juce::String& roomCode, const juce::String& host)
{
    disconnect(); // always tear down cleanly before creating a new connection
    if (host.isNotEmpty()) signalingHost = host;
    peer = std::make_unique<PeerConnection> (this);
    peer->connect (signalingHost + ":8765", roomCode);
}

void CollabSyncProcessor::disconnect()
{
    disconnecting = true;
    if (peer) peer->disconnect();
    peer.reset();
    disconnecting = false;
}

void CollabSyncProcessor::triggerRecord()
{
    if (isCountingDown() || isRecording()) return;

    // Read BPM from DAW, fall back to 120
    float bpm = 120.0f;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                bpm = (float) *b;

    // Tell peer to start the same countdown
    if (peer) peer->sendCountdown (bpm);

    // Start countdown locally
    onCountdownReceived (bpm);
}

void CollabSyncProcessor::onCountdownReceived (float bpm)
{
    if (isCountingDown() || isRecording()) return;

    double beatDurationMs = 60000.0 / (double) bpm;

    // Use a raw std::thread stored in countdownThread via a shim juce::Thread subclass.
    // Simpler: just use std::thread directly and store as a raw thread.
    struct CountdownThread : public juce::Thread
    {
        CountdownThread (CollabSyncProcessor& p, double beatMs)
            : juce::Thread ("CollabSync Countdown"), proc (p), beatDurationMs (beatMs) {}

        void run() override
        {
            for (int beat = 4; beat >= 1; --beat)
            {
                if (threadShouldExit()) return;
                proc.countdownBeat.store (beat);
                juce::MessageManager::callAsync ([&proc = proc.stateListeners] {
                    proc.call ([] (juce::ChangeListener& l) { l.changeListenerCallback (nullptr); });
                });
                juce::Thread::sleep ((int) beatDurationMs);
            }
            if (threadShouldExit()) return;

            proc.countdownBeat.store (0);
            auto dir = juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                            .getChildFile ("CollabSync Sessions")
                            .getChildFile (juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H-%M-%S"));
            proc.recorder->startRecording (proc.currentSampleRate, 2, dir);
            proc.recording.store (true);
            juce::MessageManager::callAsync ([&proc = proc.stateListeners] {
                proc.call ([] (juce::ChangeListener& l) { l.changeListenerCallback (nullptr); });
            });
        }

        CollabSyncProcessor& proc;
        double beatDurationMs;
    };

    countdownThread = std::make_unique<CountdownThread> (*this, beatDurationMs);
    countdownThread->startThread();
}

void CollabSyncProcessor::triggerStop()
{
    if (! isRecording()) return;
    if (peer) peer->sendStop();
    onStopReceived();
}

void CollabSyncProcessor::onStopReceived()
{
    if (! isRecording()) return;
    recording.store (false);
    lastSessionDir = recorder->stopRecording();
    juce::MessageManager::callAsync ([this] {
        stateListeners.call ([] (juce::ChangeListener& l) { l.changeListenerCallback (nullptr); });
    });
}

float CollabSyncProcessor::getLatencyMs() const
{
    return peer ? peer->getRttMs() / 2.0f : 0.0f;
}

juce::AudioProcessorEditor* CollabSyncProcessor::createEditor()
{
    return new CollabSyncEditor (*this);
}

//==============================================================================
void CollabSyncProcessor::startSessionServer()
{
    signalingServer->start(); // blocks until listening (or error)
    if (signalingServer->getState() != SignalingServer::State::Listening) return;

    // Auto-connect the host to their own server with a fixed room code.
    // The guest uses the same fixed code — no manual entry needed.
    connect ("SYNC", "localhost");
}

void CollabSyncProcessor::stopSessionServer()
{
    signalingServer->stop(); // stop first so onPeerDisconnected doesn't auto-rejoin
    disconnect();
}

bool CollabSyncProcessor::isHostingSession() const
{
    return signalingServer->getState() == SignalingServer::State::Listening;
}

int CollabSyncProcessor::getSessionPeerCount() const
{
    return signalingServer->getJoinedCount();
}

juce::String CollabSyncProcessor::getSessionTailscaleIP() const
{
    return signalingServer->getTailscaleIP();
}

juce::String CollabSyncProcessor::getSessionErrorMessage() const
{
    return signalingServer->getErrorMessage();
}

juce::String CollabSyncProcessor::getLocalTailscaleIP() const
{
    return SignalingServer::detectTailscaleIP();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CollabSyncProcessor();
}
