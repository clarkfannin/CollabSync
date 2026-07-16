#include "PeerCrypto.h"
#include <sodium.h>
#include <cstring>

bool PeerCrypto::init()
{
    // sodium_init() returns 0 on success, 1 if already initialised, -1 on
    // failure. It is safe to call more than once.
    static const int result = sodium_init();
    return result >= 0;
}

PeerCrypto::PeerCrypto()
{
    init();
    localPk.resize (crypto_kx_PUBLICKEYBYTES);
    localSk.resize (crypto_kx_SECRETKEYBYTES);
    crypto_kx_keypair (localPk.data(), localSk.data());
}

bool PeerCrypto::deriveSharedKeys (const std::vector<uint8_t>& peerPublicKey,
                                   bool isServer,
                                   const std::string& roomCode)
{
    if (peerPublicKey.size() != crypto_kx_PUBLICKEYBYTES)
        return false;

    std::array<uint8_t, crypto_kx_SESSIONKEYBYTES> rx{}, tx{};

    // crypto_kx derives a matching pair of keys: the server's rx equals the
    // client's tx and vice versa, so exactly one side must act as "server".
    const int rc = isServer
        ? crypto_kx_server_session_keys (rx.data(), tx.data(),
                                         localPk.data(), localSk.data(),
                                         peerPublicKey.data())
        : crypto_kx_client_session_keys (rx.data(), tx.data(),
                                         localPk.data(), localSk.data(),
                                         peerPublicKey.data());
    if (rc != 0)
        return false;

    // Mix the room code into both keys so that a party who never learned the
    // room code cannot derive matching keys. Applying the same deterministic,
    // room-keyed hash to both rx and tx preserves the server-rx == client-tx
    // pairing that crypto_kx guarantees.
    std::array<uint8_t, crypto_generichash_KEYBYTES> roomKey{};
    crypto_generichash (roomKey.data(), roomKey.size(),
                        reinterpret_cast<const unsigned char*> (roomCode.data()),
                        roomCode.size(),
                        nullptr, 0);

    auto mix = [&roomKey] (const std::array<uint8_t, crypto_kx_SESSIONKEYBYTES>& in,
                           std::array<uint8_t, 32>& out)
    {
        static_assert (crypto_kx_SESSIONKEYBYTES >= 32, "session key too small");
        crypto_generichash (out.data(), out.size(),
                            in.data(), in.size(),
                            roomKey.data(), roomKey.size());
    };
    mix (rx, rxKey);
    mix (tx, txKey);

    // Wipe intermediate key material.
    sodium_memzero (rx.data(), rx.size());
    sodium_memzero (tx.data(), tx.size());
    sodium_memzero (roomKey.data(), roomKey.size());

    ready = true;
    return true;
}

std::vector<uint8_t> PeerCrypto::encrypt (const uint8_t* data, size_t len) const
{
    if (! ready)
        return {};

    constexpr size_t NPUB = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES; // 24
    constexpr size_t ABYT = crypto_aead_xchacha20poly1305_ietf_ABYTES;    // 16

    std::vector<uint8_t> out (NPUB + len + ABYT);
    randombytes_buf (out.data(), NPUB); // fresh random nonce, prepended

    unsigned long long clen = 0;
    const int rc = crypto_aead_xchacha20poly1305_ietf_encrypt (
        out.data() + NPUB, &clen,
        data, len,
        nullptr, 0,          // no additional data
        nullptr,             // nsec (unused)
        out.data(),          // nonce
        txKey.data());
    if (rc != 0)
        return {};

    out.resize (NPUB + (size_t) clen);
    return out;
}

bool PeerCrypto::decrypt (const uint8_t* data, size_t len, std::vector<uint8_t>& out) const
{
    if (! ready)
        return false;

    constexpr size_t NPUB = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES; // 24
    constexpr size_t ABYT = crypto_aead_xchacha20poly1305_ietf_ABYTES;    // 16

    if (len < NPUB + ABYT)
        return false;

    const size_t cipherLen = len - NPUB;
    out.resize (cipherLen - ABYT);

    unsigned long long mlen = 0;
    const int rc = crypto_aead_xchacha20poly1305_ietf_decrypt (
        out.data(), &mlen,
        nullptr,             // nsec (unused)
        data + NPUB, cipherLen,
        nullptr, 0,          // no additional data
        data,                // nonce
        rxKey.data());
    if (rc != 0)
    {
        out.clear();
        return false; // authentication failed — drop the packet
    }

    out.resize ((size_t) mlen);
    return true;
}
