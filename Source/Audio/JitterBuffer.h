#pragma once
#include <vector>
#include <map>
#include <mutex>
#include <cstdint>

// Packet stored in the jitter buffer
struct AudioPacket
{
    uint64_t sessionTimeNs = 0;
    uint32_t sequence      = 0;
    std::vector<uint8_t> opusData;
};

// Adaptive jitter buffer.
// Network receive thread inserts packets; audio/decode thread pulls them.
class JitterBuffer
{
public:
    JitterBuffer (int targetDelayMs = 20, int maxDelayMs = 80)
        : targetDelayNs (static_cast<uint64_t> (targetDelayMs) * 1'000'000ULL)
        , maxDelayNs    (static_cast<uint64_t> (maxDelayMs)    * 1'000'000ULL)
    {}

    void insert (AudioPacket packet)
    {
        std::lock_guard<std::mutex> lock (mtx);
        uint32_t seq = packet.sequence;
        packets[seq] = std::move (packet);

        // Don't let the buffer grow unbounded
        while (packets.size() > 200)
            packets.erase (packets.begin());
    }

    // Returns true and fills packet if a packet is ready for playback now.
    // sessionNow = current session time in nanoseconds.
    bool popIfReady (uint64_t sessionNowNs, AudioPacket& out)
    {
        std::lock_guard<std::mutex> lock (mtx);
        if (packets.empty()) return false;

        auto& earliest = packets.begin()->second;

        // Packet is ready when its session time + target delay <= now
        if (earliest.sessionTimeNs + targetDelayNs <= sessionNowNs)
        {
            out = std::move (earliest);
            packets.erase (packets.begin());
            return true;
        }
        return false;
    }

    void setTargetDelayMs (int ms)
    {
        targetDelayNs = static_cast<uint64_t> (ms) * 1'000'000ULL;
    }

    int getNumBuffered() const
    {
        std::lock_guard<std::mutex> lock (mtx);
        return static_cast<int> (packets.size());
    }

private:
    mutable std::mutex mtx;
    std::map<uint32_t, AudioPacket> packets; // ordered by sequence number
    uint64_t targetDelayNs;
    uint64_t maxDelayNs;
};
