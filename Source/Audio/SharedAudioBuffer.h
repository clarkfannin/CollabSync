#pragma once
#include <vector>
#include <atomic>
#include <cstring>

// Lock-free single-producer single-consumer ring buffer for audio samples.
// Audio thread writes (or reads); network thread reads (or writes).
class SharedAudioBuffer
{
public:
    SharedAudioBuffer() : capacity (48000) { buffer.resize (48000, 0.0f); }

    SharedAudioBuffer (SharedAudioBuffer&& other) noexcept
        : buffer   (std::move (other.buffer))
        , capacity (other.capacity)
    {
        readHead.store  (other.readHead.load());
        writeHead.store (other.writeHead.load());
    }

    SharedAudioBuffer& operator= (SharedAudioBuffer&& other) noexcept
    {
        buffer   = std::move (other.buffer);
        capacity = other.capacity;
        readHead.store  (other.readHead.load());
        writeHead.store (other.writeHead.load());
        return *this;
    }

    SharedAudioBuffer (const SharedAudioBuffer&) = delete;
    SharedAudioBuffer& operator= (const SharedAudioBuffer&) = delete;

    explicit SharedAudioBuffer (int capacitySamples)
        : capacity (capacitySamples)
    {
        buffer.resize (static_cast<size_t> (capacity), 0.0f);
    }

    // Called from producer thread. Returns number of samples actually written.
    int write (const float* data, int numSamples)
    {
        int head = writeHead.load (std::memory_order_relaxed);
        int tail = readHead.load  (std::memory_order_acquire);

        int available = capacity - ((head - tail + capacity) % capacity) - 1;
        int toWrite   = std::min (numSamples, available);

        for (int i = 0; i < toWrite; ++i)
            buffer[static_cast<size_t>((head + i) % capacity)] = data[i];

        writeHead.store ((head + toWrite) % capacity, std::memory_order_release);
        return toWrite;
    }

    // Called from consumer thread. Returns number of samples actually read.
    int read (float* data, int numSamples)
    {
        int tail = readHead.load  (std::memory_order_relaxed);
        int head = writeHead.load (std::memory_order_acquire);

        int available = (head - tail + capacity) % capacity;
        int toRead    = std::min (numSamples, available);

        for (int i = 0; i < toRead; ++i)
            data[i] = buffer[static_cast<size_t>((tail + i) % capacity)];

        // Zero-fill if underrun
        for (int i = toRead; i < numSamples; ++i)
            data[i] = 0.0f;

        readHead.store ((tail + toRead) % capacity, std::memory_order_release);
        return toRead;
    }

    int getNumAvailableToRead() const
    {
        int head = writeHead.load (std::memory_order_acquire);
        int tail = readHead.load  (std::memory_order_acquire);
        return (head - tail + capacity) % capacity;
    }

    void reset()
    {
        readHead.store  (0, std::memory_order_release);
        writeHead.store (0, std::memory_order_release);
    }

private:
    std::vector<float> buffer;
    int capacity;
    std::atomic<int> readHead  { 0 };
    std::atomic<int> writeHead { 0 };
};
