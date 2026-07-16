# CollabSync Signaling Server (Cloudflare Worker)

This is the **rendezvous** server for CollabSync. It does one job: let two peers
who have never talked — both behind home NATs — exchange ICE candidates so they
can open a **direct peer-to-peer** connection. It never sees your audio or MIDI.
Once the P2P link is established, the WebSocket closes and all media flows
directly between the two machines.

It replaces the old Tailscale + local Node signaling setup. No accounts, no
tailnet, no port forwarding — the plugin just connects here with a room code.

## Why this is free

Cloudflare Workers **free plan** (verified against Cloudflare's pricing docs):

| Allowance (free plan)      | Limit          | Our usage per session |
| -------------------------- | -------------- | --------------------- |
| Requests / day             | 100,000        | ~1 (WS upgrade)       |
| Durable Object requests/day| 100,000        | ~2 (20:1 WS ratio)    |
| Compute duration / day     | 13,000 GB-s    | negligible (hibernates)|
| Durable Object storage     | 5 GB free      | **0 bytes** (in-memory)|

A pairing costs ~2 Durable Object requests, so the free tier covers **tens of
thousands of sessions per day**. The Durable Object uses the SQLite backend
(required for the free plan) but **never writes storage**, so the SQLite storage
billing that began Jan 2026 does not apply. See `wrangler.toml`.

The only thing that could ever cost money is a TURN relay for the ~15% of peers
behind symmetric NAT — and that is **not part of this server**. It is optional
and deferred (see the main networking README).

## Deploy

You need a free Cloudflare account. Then, from this directory:

```bash
npm install
npx wrangler login      # opens a browser once to authorize your account
npx wrangler deploy
```

Wrangler prints your Worker URL, e.g.:

```
https://collabsync-signaling.<your-subdomain>.workers.dev
```

Put the `wss://` form of that host into the plugin's signaling URL setting:

```
wss://collabsync-signaling.<your-subdomain>.workers.dev/rtc?room=CODE
```

(The plugin appends `?room=CODE` itself; you configure just the base
`wss://.../rtc` URL.)

## Local testing

```bash
npx wrangler dev
# Worker at http://localhost:8787
# Health check:
curl http://localhost:8787/health          # -> ok
# WebSocket endpoint: ws://localhost:8787/rtc?room=TEST
```

You can drive two `wscat` clients against the same room to watch the relay:

```bash
npx wscat -c "ws://localhost:8787/rtc?room=TEST"   # terminal 1 -> role:host, waiting
npx wscat -c "ws://localhost:8787/rtc?room=TEST"   # terminal 2 -> role:guest, peer-joined
# type a JSON line in one; it appears in the other.
```

## Protocol

Text JSON frames over `wss://<worker>/rtc?room=CODE`:

Server → client:
- `{"type":"role","role":"host"|"guest"}` — sent on connect. The `host` is the
  ICE controlling agent.
- `{"type":"waiting"}` — you are first in the room, waiting for a peer.
- `{"type":"peer-joined"}` — both peers are present; begin ICE.
- `{"type":"peer-left"}` — the other peer disconnected.
- `{"type":"error","reason":"room_full"}` — a third peer tried to join.

Client → server (relayed verbatim to the other peer):
- `{"type":"description","sdp":"..."}` — libjuice local description.
- `{"type":"candidate","candidate":"..."}` — a trickled ICE candidate.

The server relays any message it does not itself originate, so the ICE
vocabulary can change without redeploying.

## Monitoring cost

`npx wrangler tail` streams live logs. The Cloudflare dashboard (Workers &
Pages → your worker → Metrics) shows requests/day against the 100k free limit.
