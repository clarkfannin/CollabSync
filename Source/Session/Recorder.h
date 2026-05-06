#pragma once
#include <JuceHeader.h>
#include "../Midi/MidiCapture.h"
#include <vector>
#include <mutex>

// Buffers audio samples and MIDI events during a session.
// On stopRecording(), writes WAV + MIDI files to disk.
class Recorder
{
public:
    Recorder() = default;

    void startRecording (double sampleRate, int numChannels, const juce::File& outputDir)
    {
        this->sampleRate  = sampleRate;
        this->numChannels = numChannels;
        this->outputDir   = outputDir;
        outputDir.createDirectory();

        // Pre-size for ~10 minutes of stereo audio so the audio/network threads
        // don't reallocate (and stall on heap) inside append*() while recording.
        const size_t reserveSamples = static_cast<size_t> (sampleRate) * static_cast<size_t> (numChannels) * 600;

        {
            std::lock_guard<std::mutex> lock (audioMtx);
            audioSamples.clear();
            audioSamples.reserve (reserveSamples);
        }
        {
            std::lock_guard<std::mutex> lock (remoteAudioMtx);
            remoteAudioSamples.clear();
            remoteAudioSamples.reserve (reserveSamples);
        }
        {
            std::lock_guard<std::mutex> lock (midiMtx);
            midiEvents.clear();
            midiEvents.reserve (8192);
        }
        {
            std::lock_guard<std::mutex> lock (remoteMidiMtx);
            remoteMidiEvents.clear();
            remoteMidiEvents.reserve (8192);
        }
        active = true;
    }

    // Call from audio thread — appends local audio.
    // Uses a dedicated mutex so it never contends with the UDP receive thread
    // (appendRemoteAudio). Sharing a mutex caused the audio thread to block
    // on heap-reallocating inserts in the network thread → crackles when both
    // peers were playing simultaneously.
    void appendLocalAudio (const float* interleaved, int numFrames)
    {
        if (! active) return;
        std::lock_guard<std::mutex> lock (audioMtx);
        audioSamples.insert (audioSamples.end(), interleaved, interleaved + numFrames * numChannels);
    }

    // Call from network receive thread — appends remote audio (already decoded)
    void appendRemoteAudio (const float* interleaved, int numFrames)
    {
        if (! active) return;
        std::lock_guard<std::mutex> lock (remoteAudioMtx);
        remoteAudioSamples.insert (remoteAudioSamples.end(), interleaved, interleaved + numFrames * numChannels);
    }

    void appendLocalMidi  (const MidiPacket& pkt)
    {
        if (! active) return;
        std::lock_guard<std::mutex> lock (midiMtx);
        midiEvents.push_back (pkt);
    }

    void appendRemoteMidi (const MidiPacket& pkt)
    {
        if (! active) return;
        std::lock_guard<std::mutex> lock (remoteMidiMtx);
        remoteMidiEvents.push_back (pkt);
    }

    // Stop and write files. Returns the output directory.
    juce::File stopRecording()
    {
        active = false;
        writeFiles();
        return outputDir;
    }

    bool isRecording() const { return active.load(); }

private:
    void writeFiles()
    {
        writeWav (outputDir.getChildFile ("local.wav"),  audioSamples);
        writeWav (outputDir.getChildFile ("remote.wav"), remoteAudioSamples);
        writeMidi (outputDir.getChildFile ("local.mid"),  midiEvents);
        writeMidi (outputDir.getChildFile ("remote.mid"), remoteMidiEvents);
    }

    void writeWav (const juce::File& file, const std::vector<float>& samples)
    {
        if (samples.empty()) return;
        juce::WavAudioFormat wav;
        auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream());
        if (! stream) return;
        auto writer = std::unique_ptr<juce::AudioFormatWriter> (
            wav.createWriterFor (stream.release(), sampleRate, static_cast<unsigned>(numChannels),
                                 24, {}, 0));
        if (! writer) return;

        int numFrames = static_cast<int>(samples.size()) / numChannels;
        // Convert interleaved float to JUCE channel buffer
        juce::AudioBuffer<float> buf (numChannels, numFrames);
        for (int ch = 0; ch < numChannels; ++ch)
            for (int i = 0; i < numFrames; ++i)
                buf.setSample (ch, i, samples[static_cast<size_t>(i * numChannels + ch)]);

        writer->writeFromAudioSampleBuffer (buf, 0, numFrames);
    }

    void writeMidi (const juce::File& file, const std::vector<MidiPacket>& events)
    {
        if (events.empty()) return;
        juce::MidiFile mf;
        mf.setTicksPerQuarterNote (960);
        juce::MidiMessageSequence seq;

        double firstNs = static_cast<double>(events.front().sessionTimeNs);
        for (auto& pkt : events)
        {
            double timeSeconds = (static_cast<double>(pkt.sessionTimeNs) - firstNs) / 1e9;
            double ticks = timeSeconds * 960.0 * (120.0 / 60.0); // assume 120bpm placeholder
            uint8_t status = static_cast<uint8_t>(pkt.status | pkt.channel);
            seq.addEvent (juce::MidiMessage (status, pkt.data1, pkt.data2), ticks);
        }
        mf.addTrack (seq);

        auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream());
        if (stream) mf.writeTo (*stream);
    }

    std::atomic<bool> active { false };
    // Per-stream mutexes — each stream has exactly one writer thread, so these
    // never block the producer. The split prevents the audio thread from
    // contending with the UDP receive thread.
    std::mutex audioMtx;
    std::mutex remoteAudioMtx;
    std::mutex midiMtx;
    std::mutex remoteMidiMtx;

    double sampleRate  = 48000.0;
    int    numChannels = 2;
    juce::File outputDir;

    std::vector<float>      audioSamples;
    std::vector<float>      remoteAudioSamples;
    std::vector<MidiPacket> midiEvents;
    std::vector<MidiPacket> remoteMidiEvents;
};
