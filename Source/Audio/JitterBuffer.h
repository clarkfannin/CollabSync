#pragma once
#include <vector>
#include <map>
#include <mutex>
#include <cstdint>
#include <algorithm>

// Packet stored in the jitter buffer
struct AudioPacket
{
    uint64_t sessionTimeNs = 0;
    uint32_t sequence      = 0;
    std::vector<uint8_t> opusData;
};

// Adaptive jitter buffer controller.
//
// Target delay starts low and adjusts automatically:
//   INCREASE — any underrun triggers +1 block, rate-limited to once per 500ms.
//   DECREASE — after 10s with no underruns, shed -1 block, rate-limited to once per 10s.
//   BASELINE — if a drop happens within 2s of a decrease, the current level becomes
//              a learned floor so we don't keep oscillating on unstable connections.
//
// Usage:
//   prepare()         — call from prepareToPlay
//   notifyDrop()      — call on every buffer underrun
//   onAudioCallback() — call once per processBlock
//   getTargetMs()     — read current target for pre-buffer sizing
//   reset()           — call on (re)connect
class JitterBuffer
{
public:
    JitterBuffer (int targetDelayMs = 20, int maxDelayMs = 80)
        : targetMs   (static_cast<float> (targetDelayMs))
        , maxMs      (static_cast<float> (maxDelayMs))
        , baselineMs (static_cast<float> (targetDelayMs))
    {}

    // Call from prepareToPlay — sets the step size used for increase/decrease.
    void prepare (double sampleRate, int blockSize)
    {
        std::lock_guard<std::mutex> lock (mtx);
        blockSizeMs = static_cast<float> (blockSize) / static_cast<float> (sampleRate) * 1000.0f;
    }

    // Call when a buffer underrun is detected.
    // nowMs: juce::Time::getMillisecondCounterHiRes()
    void notifyDrop (double nowMs)
    {
        std::lock_guard<std::mutex> lock (mtx);

        // Rate-limit: at most one increase per 500ms
        if ((nowMs - lastDropTimeMs) < minIncreaseIntervalMs)
            return;

        targetMs = std::min (targetMs + blockSizeMs, maxMs);

        // If a drop arrives within 2s of a decrease, ratchet up the baseline
        // so we don't keep bouncing back to a level the connection can't hold.
        if ((nowMs - lastDecrTimeMs) < baselineRatchetWindowMs)
            baselineMs = targetMs;

        lastDropTimeMs = nowMs;
    }

    // Call once per audio callback — drives the slow decrease path.
    void onAudioCallback (double nowMs)
    {
        std::lock_guard<std::mutex> lock (mtx);

        bool cleanLongEnough = (nowMs - lastDropTimeMs) > quietThresholdMs;
        bool decrLongEnough  = (nowMs - lastDecrTimeMs)  > minDecreaseIntervalMs;
        bool aboveBaseline   = targetMs > baselineMs + 0.01f;

        if (cleanLongEnough && decrLongEnough && aboveBaseline)
        {
            targetMs = std::max (targetMs - blockSizeMs, baselineMs);
            lastDecrTimeMs = nowMs;
        }
    }

    // Returns the current adaptive target delay in milliseconds.
    float getTargetMs() const
    {
        std::lock_guard<std::mutex> lock (mtx);
        return targetMs;
    }

    // Reset adaptive state — call on (re)connect.
    void reset()
    {
        std::lock_guard<std::mutex> lock (mtx);
        targetMs       = baselineMs;
        lastDropTimeMs = -1e9;
        lastDecrTimeMs = -1e9;
        packets.clear();
    }

    // --- Packet queue (network receive thread → audio thread) ---

    void insert (AudioPacket packet)
    {
        std::lock_guard<std::mutex> lock (mtx);
        uint32_t seq = packet.sequence;
        packets[seq] = std::move (packet);
        while (packets.size() > 200)
            packets.erase (packets.begin());
    }

    bool popIfReady (uint64_t sessionNowNs, AudioPacket& out)
    {
        std::lock_guard<std::mutex> lock (mtx);
        if (packets.empty()) return false;

        auto& earliest = packets.begin()->second;
        uint64_t targetNs = static_cast<uint64_t> (targetMs * 1'000'000.0f);
        if (earliest.sessionTimeNs + targetNs <= sessionNowNs)
        {
            out = std::move (earliest);
            packets.erase (packets.begin());
            return true;
        }
        return false;
    }

    int getNumBuffered() const
    {
        std::lock_guard<std::mutex> lock (mtx);
        return static_cast<int> (packets.size());
    }

private:
    mutable std::mutex mtx;

    float targetMs    = 20.0f;
    float maxMs       = 80.0f;
    float baselineMs  = 20.0f;  // learned minimum; ratchets up on instability
    float blockSizeMs =  5.3f;  // step size; updated by prepare()

    double lastDropTimeMs = -1e9;
    double lastDecrTimeMs = -1e9;

    static constexpr double minIncreaseIntervalMs   =   500.0; // max 1 increase per 0.5s
    static constexpr double minDecreaseIntervalMs   = 10000.0; // max 1 decrease per 10s
    static constexpr double quietThresholdMs        = 10000.0; // 10s clean before decrease
    static constexpr double baselineRatchetWindowMs =  2000.0; // ratchet if drop within 2s of decrease

    std::map<uint32_t, AudioPacket> packets;
};
