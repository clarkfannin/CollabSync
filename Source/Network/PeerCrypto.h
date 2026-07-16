#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Authenticated encryption for the peer-to-peer media channel, using libsodium.
//
// Key agreement: X25519 via crypto_kx. Each peer generates an ephemeral
// keypair, exchanges public keys through the signaling channel, and derives a
// pair of session keys. The shared room code is mixed into the derived keys so
// that both sides must know it — a party that never learned the room code
// cannot derive matching keys.
//
// Packet encryption: XChaCha20-Poly1305 (crypto_aead_xchacha20poly1305_ietf)
// with a fresh random 24-byte nonce per packet, prepended to the ciphertext.
// A 192-bit random nonce makes reuse negligible, so packets may be lost or
// reordered freely (required for real-time UDP) without a stateful cipher.
//
// SECURITY NOTE: this protects against passive eavesdroppers and against third
// parties who do not know the room code. It is NOT authenticated key exchange:
// a determined active man-in-the-middle who both controls the signaling path
// and knows the room code could substitute public keys. A future hardening
// step is a short-authentication-string (SAS) confirmation exchanged over the
// established P2P channel. See NETWORKING.md.
class PeerCrypto
{
public:
    // Initialise libsodium. Safe to call multiple times / from multiple
    // threads. Returns false if the library failed to initialise.
    static bool init();

    PeerCrypto();

    // Our ephemeral public key, to be sent to the peer (raw bytes).
    const std::vector<uint8_t>& localPublicKey() const { return localPk; }

    // Derive session keys once the peer's public key arrives. `isServer` must
    // be true on exactly one side of the pair (host = server, guest = client);
    // crypto_kx assigns rx/tx roles from it. `roomCode` is mixed into the
    // derived keys as a shared secret. Returns false on bad input.
    bool deriveSharedKeys (const std::vector<uint8_t>& peerPublicKey,
                           bool isServer,
                           const std::string& roomCode);

    bool isReady() const { return ready; }

    // Encrypt: returns nonce (24 bytes) || ciphertext (len + 16-byte tag).
    // Returns an empty vector if keys are not ready or encryption fails.
    std::vector<uint8_t> encrypt (const uint8_t* data, size_t len) const;

    // Decrypt a nonce||ciphertext buffer produced by the peer. Returns false
    // on authentication failure or if keys are not ready.
    bool decrypt (const uint8_t* data, size_t len, std::vector<uint8_t>& out) const;

private:
    std::vector<uint8_t>    localPk; // crypto_kx public key
    std::vector<uint8_t>    localSk; // crypto_kx secret key
    std::array<uint8_t, 32> rxKey{}; // decrypts peer -> us
    std::array<uint8_t, 32> txKey{}; // encrypts us -> peer
    bool ready = false;
};
