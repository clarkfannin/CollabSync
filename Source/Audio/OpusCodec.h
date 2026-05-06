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

        opus_encoder_ctl (enc, OPUS_SET_BITRATE (192000));
        opus_encoder_ctl (enc, OPUS_SET_SIGNAL (OPUS_SIGNAL_MUSIC));
        opus_encoder_ctl (enc, OPUS_SET_COMPLEXITY (5));
        // Inband FEC: bundle low-bitrate redundancy of the previous frame into
        // the current packet. Lets the decoder recover a single-packet drop on
        // unreliable links (WiFi). Costs ~10% bitrate for the loss-percentage hint.
        opus_encoder_ctl (enc, OPUS_SET_INBAND_FEC (1));
        opus_encoder_ctl (enc, OPUS_SET_PACKET_LOSS_PERC (10));
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

    void resetState() { if (enc) opus_encoder_ctl (enc, OPUS_RESET_STATE); }

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

    // Decode to interleaved float PCM.
    //   plc=true  → synthesize a missing frame (pass any maxFrames; data ignored)
    //   fec=true  → recover the previous lost frame from this packet's FEC payload
    //   both false → normal decode of this packet
    int decode (const uint8_t* data, int dataLen, float* pcmOut, int maxFrames,
                bool plc = false, bool fec = false)
    {
        if (plc)
            return opus_decode_float (dec, nullptr, 0, pcmOut, maxFrames, 0);
        return opus_decode_float (dec, data, dataLen, pcmOut, maxFrames, fec ? 1 : 0);
    }

    void resetState() { if (dec) opus_decoder_ctl (dec, OPUS_RESET_STATE); }

private:
    ::OpusDecoder* dec = nullptr;
    int sampleRate, channels;
};
