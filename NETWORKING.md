# CollabSync Networking

This document describes how two CollabSync instances find each other and
exchange audio/MIDI, and how to stand up the (free) infrastructure it needs.

It replaces the previous **Tailscale** model. There are no accounts to create,
no tailnet to join, and no IP addresses to copy — two people enter the same
**room code** and connect.

## Architecture

```
   Peer A (plugin)                 Peer B (plugin)
        │                                │
        │  1. wss:// (room code)         │  1. wss:// (room code)
        └──────────┐          ┌──────────┘
                   ▼          ▼
            Cloudflare Worker (signaling / rendezvous)
              - pairs the two peers by room code
              - relays ICE candidates + public keys
              - never sees audio/MIDI
                   │          │
        ┌──────────┘          └──────────┐
        │  2. ICE candidates + pubkeys    │
        ▼                                 ▼
   Peer A  ◀───── 3. direct P2P UDP ─────▶  Peer B
           (libjuice hole-punch, libsodium-encrypted media)
```

1. **Rendezvous.** Both peers open a WebSocket to the Cloudflare Worker with the
   same room code. The Worker pairs them and assigns roles (first in = host =
   ICE controlling side).
2. **ICE negotiation.** Each peer gathers ICE candidates via a public STUN
   server (discovering its own public IP:port), and the candidates + encryption
   public keys are relayed through the Worker.
3. **Direct P2P.** libjuice hole-punches a direct UDP path between the two NATs.
   All media flows directly peer-to-peer, encrypted with libsodium. Once the
   path is established the signaling WebSocket closes.

### Components

| Piece | What | Where |
| ----- | ---- | ----- |
| Signaling | Cloudflare Worker + Durable Object | `signaling/worker/` |
| STUN | Public server (`stun.l.google.com:19302`) | external, free |
| ICE / hole punching | libjuice (MPL-2.0) | fetched at build time |
| Encryption | libsodium (ISC) | fetched at build time |
| WSS client | IXWebSocket (BSD) + TLS backend | fetched at build time |

TLS backend: **Apple SecureTransport** on macOS (native, no extra dependency);
**mbedTLS** (Apache-2.0) on Windows/Linux.

## Cost: free at hobby/indie scale

- **STUN** — public servers, unmetered. $0.
- **Signaling** — Cloudflare Workers free plan. A pairing costs ~2 Durable
  Object requests against a 100,000/day allowance, and the Durable Object
  stores nothing (in-memory room state), so no storage billing. See
  `signaling/worker/README.md` for the verified numbers.
- **TURN relay** — *not deployed.* Deferred (see below).

## Deploying the signaling server

See `signaling/worker/README.md`. In short:

```bash
cd signaling/worker
npm install
npx wrangler login
npx wrangler deploy
```

Then point the plugin at the printed URL — either at build time:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCOLLABSYNC_SIGNALING_URL='wss://collabsync-signaling.you.workers.dev/rtc'
```

or at runtime by typing the `wss://…/rtc` URL into the plugin's host field
(a `ws://`/`wss://` value there overrides the compiled-in default).

## Security posture

Media is encrypted with **XChaCha20-Poly1305** (authenticated encryption).
Session keys come from an **X25519** key agreement (libsodium `crypto_kx`), with
the **room code mixed into the derived keys** so a party who never learned the
room code cannot derive matching keys.

**What this protects against:** passive eavesdroppers on the network path, and
third parties who do not know the room code.

**What it does not (yet) protect against:** an active man-in-the-middle who both
controls the signaling path and knows the room code could substitute public
keys during the exchange. This is weaker than Tailscale's WireGuard identity
model. A planned hardening step is a **short-authentication-string (SAS)**
confirmation exchanged over the established P2P channel (the two peers compare a
few digits derived from the agreed key), which closes the MITM gap without any
central authority. Tracked as future work; see `PeerCrypto.h`.

## Known limitations / roadmap

- **Room code is currently fixed to `SYNC`** in the plugin UI, so only one pair
  can use a given Worker at a time. Per-pair room-code entry lands with the UI
  redesign (a separate branch). The transport already carries the room code
  end-to-end; only the UI field is missing.
- **No TURN fallback.** ICE hole punching succeeds for the large majority of
  home NATs, but pairs where at least one side is behind **symmetric / carrier-
  grade NAT** will fail to connect directly. Tailscale hid this with its DERP
  relays. To cover that tail, deploy a TURN server (e.g. `coturn`, or a free
  tier such as Metered Open Relay) and add it to the libjuice config
  (`turn_servers` in `PeerConnection::startIce`). Deferred by choice — ship
  direct-only, add relay if users hit it.
- **SAS/authenticated key exchange** — see Security posture above.
