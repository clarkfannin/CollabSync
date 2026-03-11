#pragma once
#include <cstdint>
#include <chrono>
#include <atomic>

// Maintains a shared session clock offset so both peers use the same timeline.
// Master peer: offset = 0, slave peer: offset = measured difference.
class SessionClock
{
public:
    // Returns local monotonic time in nanoseconds
    static uint64_t localNow()
    {
        using namespace std::chrono;
        return static_cast<uint64_t> (
            duration_cast<nanoseconds> (steady_clock::now().time_since_epoch()).count());
    }

    void setSessionStart (uint64_t localStartNs)
    {
        sessionStartNs.store (localStartNs, std::memory_order_release);
    }

    // Returns nanoseconds elapsed since session start, corrected by peer offset
    uint64_t sessionNow() const
    {
        uint64_t start = sessionStartNs.load (std::memory_order_acquire);
        int64_t  off   = offsetNs.load        (std::memory_order_acquire);
        return static_cast<uint64_t> (static_cast<int64_t>(localNow() - start) + off);
    }

    // Called after running Cristian's sync: measured offset of our clock vs peer's
    void applyOffset (int64_t measuredOffsetNs)
    {
        offsetNs.store (measuredOffsetNs, std::memory_order_release);
    }

    // Cristian's algorithm: call this on the result of a ping round-trip.
    // t1 = local time we sent the ping
    // t2 = peer's time when they received it   (echoed back)
    // t3 = peer's time when they sent the reply (echoed back)
    // t4 = local time we received the reply
    static int64_t computeOffset (uint64_t t1, uint64_t t2, uint64_t t3, uint64_t t4)
    {
        // offset = ((t2 - t1) + (t3 - t4)) / 2
        int64_t a = static_cast<int64_t>(t2) - static_cast<int64_t>(t1);
        int64_t b = static_cast<int64_t>(t3) - static_cast<int64_t>(t4);
        return (a + b) / 2;
    }

    static uint64_t computeRttNs (uint64_t t1, uint64_t t2, uint64_t t3, uint64_t t4)
    {
        return (t4 - t1) - (t3 - t2);
    }

private:
    std::atomic<uint64_t> sessionStartNs { 0 };
    std::atomic<int64_t>  offsetNs       { 0 };
};
