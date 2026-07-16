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
    jitterBuffer = std::make_unique<JitterBuffer> (20, 200); // 20ms target, 200ms max (WiFi headroom)
    recorder   = std::make_unique<Recorder>();

    // Pre-allocate the lock-free MIDI recording queue
    midiRecordQueue.resize (2048);

    // Wire MIDI capture to send over network.
    // Recording goes through a lock-free FIFO (drained in processBlock)
    // so this callback never blocks on the Recorder mutex.
    midiCapture->setSendCallback ([this] (const MidiPacket& pkt)
    {
        if (peer && peer->isConnected())
            peer->sendMidi (reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));

        if (recorder->isRecording())
        {
            const auto scope = midiRecordFifo.write (1);
            if (scope.blockSize1 > 0)
                midiRecordQueue[static_cast<size_t> (scope.startIndex1)] = pkt;
            else if (scope.blockSize2 > 0)
                midiRecordQueue[static_cast<size_t> (scope.startIndex2)] = pkt;
        }
    });

    // NOTE: MIDI inputs are opened in prepareToPlay(), not here. Opening system
    // MIDI hardware from the constructor makes the plugin fail host validation
    // scans (e.g. FL Studio on Windows silently rejects it), because the scanner
    // constructs the plugin in a sandbox where grabbing all MIDI devices hangs or
    // fails. Deferring to prepareToPlay means scanning never touches hardware.
}

CollabSyncProcessor::~CollabSyncProcessor()
{
    closeMidiInputs();
    if (peer) peer->disconnect();
}

//==============================================================================
void CollabSyncProcessor::openMidiInputs()
{
    closeMidiInputs();
    auto devices = juce::MidiInput::getAvailableDevices();
    for (auto& dev : devices)
    {
        auto input = juce::MidiInput::openDevice (dev.identifier, this);
        if (input)
        {
            DBG ("Opened MIDI device: " << dev.name);
            input->start();
            midiInputDevices.push_back (std::move (input));
        }
    }
}

void CollabSyncProcessor::closeMidiInputs()
{
    for (auto& input : midiInputDevices)
        input->stop();
    midiInputDevices.clear();
    for (auto& n : midiNoteState) n.store (false, std::memory_order_relaxed);
}

void CollabSyncProcessor::handleIncomingMidiMessage (juce::MidiInput* /*source*/,
                                                      const juce::MidiMessage& msg)
{
    // Per-note tracking for the MIDI activity light (before the
    // potentially-blocking captureDirect call so the UI stays responsive)
    if (msg.isNoteOn())
        midiNoteState[msg.getNoteNumber()].store (true, std::memory_order_relaxed);
    else if (msg.isNoteOff())
        midiNoteState[msg.getNoteNumber()].store (false, std::memory_order_relaxed);

    // Send over network + record via the same path as host MIDI
    midiCapture->captureDirect (msg, clock->sessionNow());
}

void CollabSyncProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize  = samplesPerBlock;

    // Open all system MIDI inputs here (not in the constructor) — bypasses DAW
    // routing. Deferred out of the constructor so host validation scans, which
    // construct the plugin without ever calling prepareToPlay, don't touch MIDI
    // hardware and reject the plugin. Idempotent: openMidiInputs() closes first.
    openMidiInputs();

    int capacitySamples = static_cast<int>(sampleRate * 1.0); // 1 second ring buffer
    sendBuffer    = SharedAudioBuffer (capacitySamples);
    receiveBuffer = SharedAudioBuffer (capacitySamples);

    try
    {
        // 20ms frames: 50 packets/sec instead of 200. Each lost packet matters
        // less because Opus FEC can recover one and PLC fills the rest. Net latency
        // bump ~15ms, but reliability over WiFi is dramatically better.
        encoder = std::make_unique<OpusEncoder> (48000, 2, 20.0f);
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

    // Pre-allocate buffers used on the real-time audio thread and receive thread.
    // Heap allocations in audio callbacks cause priority inversion on macOS.
    int maxBlock = samplesPerBlock;
    processBlockInterleaved.resize (static_cast<size_t> (maxBlock * 2));
    processBlockRemote.resize      (static_cast<size_t> (maxBlock * 2));
    // Discard buffer: large enough for the worst-case overflow drain
    int overflowMax = (int) (sampleRate * 0.200 * 2.5) * 2;  // maxMs (200) * 2.5, stereo
    processBlockDiscard.resize (static_cast<size_t> (overflowMax));
    // Decode buffer: sized for one 20ms Opus frame at 48kHz (960 stereo samples)
    decodePcm.resize (static_cast<size_t> (2 * 960));

    // Pre-allocate receive-thread resample scratch buffers — Opus always decodes
    // at 48kHz, so the resampler outputs (frames * sampleRate / 48000) samples.
    int recvOpusFrames = 960; // 20ms at 48kHz
    int recvDawFrames  = (int) std::ceil (recvOpusFrames * sampleRate / 48000.0) + 4;
    recvResampleInL.assign (static_cast<size_t> (recvOpusFrames), 0.0f);
    recvResampleInR.assign (static_cast<size_t> (recvOpusFrames), 0.0f);
    recvResampleOutL.assign (static_cast<size_t> (recvDawFrames), 0.0f);
    recvResampleOutR.assign (static_cast<size_t> (recvDawFrames), 0.0f);
    recvResampleInterleaved.assign (static_cast<size_t> (recvDawFrames * 2), 0.0f);

    // Reset sequence-tracked PLC/FEC state
    haveLastSeq          = false;
    lastDecodedSeq       = 0;
    consecutiveUnderruns = 0;

    // Audio send thread: drains sendBuffer, encodes, sends.
    // Uses a condition variable instead of spin-wait so the OS can schedule
    // precisely instead of coalescing our microsecond sleeps.
    sendThreadRunning.store (true);
    audioSendThread = std::thread ([this]
    {
        if (! encoder) return;
        int opusFrameSamples = encoder->getFrameSizeSamples(); // 240 @ 48kHz/5ms
        int daqFrame         = daqFrameSamples;
        int readSize         = daqFrame * 2; // stereo interleaved
        std::vector<float> frame (static_cast<size_t> (readSize));
        std::vector<float> opusFrame;
        std::vector<float> inL, inR, outL, outR;
        if (needsResampling)
        {
            opusFrame.resize (static_cast<size_t> (opusFrameSamples * 2));
            inL.resize  (static_cast<size_t> (daqFrame));
            inR.resize  (static_cast<size_t> (daqFrame));
            outL.resize (static_cast<size_t> (opusFrameSamples));
            outR.resize (static_cast<size_t> (opusFrameSamples));
        }
        std::vector<uint8_t> pkt;
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

                if (encoderResetPending.exchange (false))
                    encoder->resetState();

                auto encoded = encoder->encode (encodeData, encodeSamps);
                if (! encoded.empty() && peer && peer->isConnected())
                {
                    uint64_t t = clock->sessionNow();
                    pkt.resize (sizeof(uint32_t) + sizeof(uint64_t) + encoded.size());
                    std::memcpy (pkt.data(), &seq, 4);
                    std::memcpy (pkt.data() + 4, &t, 8);
                    std::memcpy (pkt.data() + 12, encoded.data(), encoded.size());
                    peer->sendAudio (pkt.data(), pkt.size());
                    ++seq;
                }
            }
            else
            {
                // Wait for processBlock to signal that new audio data is available.
                // Condition variable wakes precisely — no macOS timer coalescing issues.
                std::unique_lock<std::mutex> lk (sendCvMutex);
                sendCv.wait_for (lk, std::chrono::milliseconds (5));
            }
        }
    });
}

void CollabSyncProcessor::releaseResources()
{
    // Release system MIDI devices when the host stops the plugin (paired with the
    // openMidiInputs() call in prepareToPlay). The destructor closes them too, as
    // a backstop for hosts that destroy without calling releaseResources.
    closeMidiInputs();

    sendThreadRunning.store (false);
    sendCv.notify_one();  // wake send thread so it can see the flag and exit
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
        const float* L = buffer.getReadPointer (0);
        const float* R = totalChannels > 1 ? buffer.getReadPointer (1) : L;
        for (int i = 0; i < numSamples; ++i)
        {
            processBlockInterleaved[static_cast<size_t>(i * 2)]     = L[i];
            processBlockInterleaved[static_cast<size_t>(i * 2 + 1)] = R[i];
        }
        sendBuffer.write (processBlockInterleaved.data(), numSamples * 2);
        sendCv.notify_one();  // wake send thread — data is available

        if (recorder->isRecording())
            recorder->appendLocalAudio (processBlockInterleaved.data(), numSamples);

        // UI-only: decaying peak of local input, read by the editor to drive the
        // "Audio" activity indicator during recording. Additive — see getInputAudioLevel().
        float blockPeak = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            blockPeak = std::max (blockPeak, std::max (std::abs (L[i]), std::abs (R[i])));
        float decayed = inputAudioLevel.load (std::memory_order_relaxed) * 0.9f;
        inputAudioLevel.store (std::max (decayed, blockPeak), std::memory_order_relaxed);
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
                    int toDrain = std::min (excess, (int) processBlockDiscard.size());
                    receiveBuffer.read (processBlockDiscard.data(), toDrain);
                    available -= toDrain;
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
                int toDrain = std::min (skip, (int) processBlockDiscard.size());
                receiveBuffer.read (processBlockDiscard.data(), toDrain);
                available -= toDrain;
            }
        }
        else if (available < needed)
        {
            // Buffer underrun. Skip mixing this block (remote stays silent for
            // ~one block) and bump the adaptive jitter target. Only fully
            // re-prime after several consecutive underruns — a single hiccup
            // shouldn't cascade into a long silent period while the buffer refills.
            jitterBuffer->notifyDrop (nowMs);
            bufferUnderruns.fetch_add (1, std::memory_order_relaxed);
            ++consecutiveUnderruns;
            if (consecutiveUnderruns >= 3)
            {
                receiveBufferPrimed.store (false, std::memory_order_relaxed);
                consecutiveUnderruns = 0;
            }
        }
        else
        {
            consecutiveUnderruns = 0;
        }

        recvBufferLevel.store (available, std::memory_order_relaxed);

        if (receiveBufferPrimed.load (std::memory_order_relaxed) && available >= needed)
        {
            receiveBuffer.read (processBlockRemote.data(), needed);

            float* L = buffer.getWritePointer (0);
            float* R = totalChannels > 1 ? buffer.getWritePointer (1) : nullptr;
            for (int i = 0; i < numSamples; ++i)
            {
                L[i] += processBlockRemote[static_cast<size_t> (i * 2)];
                if (R) R[i] += processBlockRemote[static_cast<size_t> (i * 2 + 1)];
            }
        }
    }

    // --- Capture local MIDI → network ---
    // Skip host-routed MIDI when direct system MIDI inputs are active to avoid duplicates.
    if (! midiMessages.isEmpty() && midiInputDevices.empty())
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

    // --- Drain MIDI recording FIFO into Recorder (lock-free read) ---
    {
        const auto scope = midiRecordFifo.read (midiRecordFifo.getNumReady());
        for (int i = 0; i < scope.blockSize1; ++i)
            recorder->appendLocalMidi (midiRecordQueue[static_cast<size_t> (scope.startIndex1 + i)]);
        for (int i = 0; i < scope.blockSize2; ++i)
            recorder->appendLocalMidi (midiRecordQueue[static_cast<size_t> (scope.startIndex2 + i)]);
    }
}

//==============================================================================
void CollabSyncProcessor::onAudioPacketReceived (const uint8_t* data, size_t size, uint64_t /*sessionTimeNs*/)
{
    packetsReceived.fetch_add (1, std::memory_order_relaxed);

    if (size < 12 || ! decoder) return;

    // Payload layout: [4-byte seq][8-byte timestamp][opus frame...]
    uint32_t       seq      = 0;
    std::memcpy (&seq, data, 4);
    const uint8_t* opusData = data + 12;
    int            opusSize = (int) size - 12;
    if (opusSize <= 0) return;

    if (decoderResetPending.exchange (false))
    {
        decoder->resetState();
        haveLastSeq = false;
    }

    auto pushDecoded = [this] (int frames)
    {
        if (frames <= 0) return;
        if (needsResampling)
        {
            int outSamples = (int) (frames * currentSampleRate / 48000.0);
            for (int i = 0; i < frames; ++i)
            {
                recvResampleInL[static_cast<size_t> (i)] = decodePcm[static_cast<size_t> (i * 2)];
                recvResampleInR[static_cast<size_t> (i)] = decodePcm[static_cast<size_t> (i * 2 + 1)];
            }
            double ratio = 48000.0 / currentSampleRate;
            recvResamplerL.process (ratio, recvResampleInL.data(), recvResampleOutL.data(),
                                    outSamples, frames, 0);
            recvResamplerR.process (ratio, recvResampleInR.data(), recvResampleOutR.data(),
                                    outSamples, frames, 0);
            for (int i = 0; i < outSamples; ++i)
            {
                recvResampleInterleaved[static_cast<size_t> (i * 2)]     = recvResampleOutL[static_cast<size_t> (i)];
                recvResampleInterleaved[static_cast<size_t> (i * 2 + 1)] = recvResampleOutR[static_cast<size_t> (i)];
            }
            receiveBuffer.write (recvResampleInterleaved.data(), outSamples * 2);
            if (recorder->isRecording())
                recorder->appendRemoteAudio (recvResampleInterleaved.data(), outSamples);
        }
        else
        {
            receiveBuffer.write (decodePcm.data(), frames * 2);
            if (recorder->isRecording())
                recorder->appendRemoteAudio (decodePcm.data(), frames);
        }
    };

    // --- Gap recovery: if seq jumped past lastDecodedSeq+1, fill the missing
    //     frames with FEC (most recent missing, recovered from THIS packet's
    //     redundancy payload) and PLC (earlier missing, synthesized).
    //     Skips out-of-order or duplicate packets — Opus state would corrupt.
    if (haveLastSeq)
    {
        if (seq <= lastDecodedSeq)
            return; // late or duplicate; ignore

        uint32_t missing = seq - lastDecodedSeq - 1;
        if (missing > 0)
        {
            // Cap concealment work — beyond ~10 lost packets the link is in
            // genuine trouble and synthesizing more just smears artifacts.
            uint32_t plcCount = std::min<uint32_t> (missing > 1 ? missing - 1 : 0, 10);

            for (uint32_t i = 0; i < plcCount; ++i)
            {
                int f = decoder->decode (nullptr, 0, decodePcm.data(), 960, /*plc*/ true);
                pushDecoded (f);
            }
            // FEC the most recent missing using THIS packet's redundancy
            int fecFrames = decoder->decode (opusData, opusSize, decodePcm.data(), 960,
                                             /*plc*/ false, /*fec*/ true);
            pushDecoded (fecFrames);
        }
    }

    int frames = decoder->decode (opusData, opusSize, decodePcm.data(), 960);
    if (frames <= 0)
    {
        decodeFailures.fetch_add (1, std::memory_order_relaxed);
        return;
    }
    decodeSuccesses.fetch_add (1, std::memory_order_relaxed);

    pushDecoded (frames);

    lastDecodedSeq = seq;
    haveLastSeq    = true;
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
    sendBuffer.reset();
    jitterBuffer->reset();
    // Signal the owning threads to reset — direct calls here race with the
    // send thread (encoder) and UDP receive thread (decoder).
    encoderResetPending.store (true);
    decoderResetPending.store (true);
    haveLastSeq          = false;
    lastDecodedSeq       = 0;
    consecutiveUnderruns = 0;
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
    for (auto& n : midiNoteState) n.store (false, std::memory_order_relaxed);
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
