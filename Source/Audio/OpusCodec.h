#pragma once
#include <vector>
#include <cstdint>
#include <stdexcept>

// Thin RAII wrappers around libopus encode/decode.
// We use system libopus installed via brew.
#include <opus.h>

class OpusEncoder
{
public:
    // frameSizeMs: 2.5, 5, 10, 20, 40, 60
    OpusEncoder (int sampleRate = 48000, int channels = 2, float frameSizeMs = 10.0f)
        : sampleRate (sampleRate), channels (channels)
        , frameSizeSamples (static_cast<int> (sampleRate * frameSizeMs / 1000.0f))
    {
        int err = 0;
        enc = opus_encoder_create (sampleRate, channels, OPUS_APPLICATION_RESTRICTED_LOWDELAY, &err);
        if (err != OPUS_OK) throw std::runtime_error ("opus_encoder_create failed");

        opus_encoder_ctl (enc, OPUS_SET_BITRATE (128000));
        opus_encoder_ctl (enc, OPUS_SET_SIGNAL (OPUS_SIGNAL_MUSIC));
        opus_encoder_ctl (enc, OPUS_SET_COMPLEXITY (5));
    }

    ~OpusEncoder() { if (enc) opus_encoder_destroy (enc); }

    // Encode interleaved float PCM. Returns encoded bytes.
    std::vector<uint8_t> encode (const float* pcm, int numFrames)
    {
        std::vector<uint8_t> out (4000);
        int bytes = opus_encode_float (enc, pcm, numFrames, out.data(), static_cast<opus_int32>(out.size()));
        if (bytes < 0) return {};
        out.resize (static_cast<size_t>(bytes));
        return out;
    }

    int getFrameSizeSamples() const { return frameSizeSamples; }
    int getChannels()         const { return channels; }

private:
    ::OpusEncoder* enc = nullptr;
    int sampleRate, channels, frameSizeSamples;
};

class OpusDecoder
{
public:
    OpusDecoder (int sampleRate = 48000, int channels = 2)
        : sampleRate (sampleRate), channels (channels)
    {
        int err = 0;
        dec = opus_decoder_create (sampleRate, channels, &err);
        if (err != OPUS_OK) throw std::runtime_error ("opus_decoder_create failed");
    }

    ~OpusDecoder() { if (dec) opus_decoder_destroy (dec); }

    // Decode to interleaved float PCM. Pass nullptr data for PLC (packet loss concealment).
    int decode (const uint8_t* data, int dataLen, float* pcmOut, int maxFrames, bool plc = false)
    {
        return opus_decode_float (dec,
            plc ? nullptr : data,
            plc ? 0       : dataLen,
            pcmOut, maxFrames, plc ? 1 : 0);
    }

private:
    ::OpusDecoder* dec = nullptr;
    int sampleRate, channels;
};
