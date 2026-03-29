#pragma once
#include <JuceHeader.h>
#include <cstdint>
#include <vector>
#include <functional>

#pragma pack(push, 1)
struct MidiPacket
{
    uint64_t sessionTimeNs;
    uint8_t  status;
    uint8_t  data1;
    uint8_t  data2;
    uint8_t  channel;
};
#pragma pack(pop)

// Converts JUCE MidiBuffer events into timestamped MidiPackets for transmission.
class MidiCapture
{
public:
    using SendCallback = std::function<void(const MidiPacket&)>;

    void setSendCallback (SendCallback cb) { sendCallback = std::move (cb); }

    // Call from processBlock with the host playhead position
    void capture (const juce::MidiBuffer& midi,
                  int64_t blockStartSample,
                  double sampleRate,
                  uint64_t sessionStartNs,
                  int64_t clockOffsetNs)
    {
        for (auto meta : midi)
        {
            auto msg = meta.getMessage();
            int64_t absoluteSample = blockStartSample + meta.samplePosition;
            double  timeSeconds    = absoluteSample / sampleRate;
            auto    timeNs         = static_cast<uint64_t> (timeSeconds * 1e9);
            uint64_t sessionTimeNs = static_cast<uint64_t> (
                static_cast<int64_t>(timeNs - sessionStartNs) + clockOffsetNs);

            MidiPacket pkt;
            pkt.sessionTimeNs = sessionTimeNs;
            pkt.status  = static_cast<uint8_t> (msg.getRawData()[0] & 0xF0);
            pkt.channel = static_cast<uint8_t> (msg.getChannel() - 1);
            pkt.data1   = msg.getRawDataSize() > 1 ? msg.getRawData()[1] : 0;
            pkt.data2   = msg.getRawDataSize() > 2 ? msg.getRawData()[2] : 0;

            if (sendCallback) sendCallback (pkt);
        }
    }

    // Call directly from a MidiInputCallback (system MIDI, not host buffer).
    // No sample-position math needed — just stamp with current session time.
    void captureDirect (const juce::MidiMessage& msg, uint64_t sessionNowNs)
    {
        MidiPacket pkt;
        pkt.sessionTimeNs = sessionNowNs;
        pkt.status  = static_cast<uint8_t> (msg.getRawData()[0] & 0xF0);
        pkt.channel = static_cast<uint8_t> (msg.getChannel() - 1);
        pkt.data1   = msg.getRawDataSize() > 1 ? msg.getRawData()[1] : 0;
        pkt.data2   = msg.getRawDataSize() > 2 ? msg.getRawData()[2] : 0;

        if (sendCallback) sendCallback (pkt);
    }

    // Convert incoming remote MidiPackets back into JUCE messages for injection.
    // sessionNowNs: current session time; returns sample offset within current block.
    static juce::MidiMessage toJuceMessage (const MidiPacket& pkt)
    {
        uint8_t statusByte = static_cast<uint8_t> (pkt.status | pkt.channel);
        return juce::MidiMessage (statusByte, pkt.data1, pkt.data2);
    }

private:
    SendCallback sendCallback;
};
