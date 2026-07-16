// CollabSync signaling server — Cloudflare Worker + Durable Object.
//
// Purpose: rendezvous only. Two peers who have never talked, both behind NAT,
// need a public meeting point to swap ICE candidates before they can establish
// a direct peer-to-peer (libjuice/ICE) connection. That is ALL this server does.
// It never sees audio or MIDI — once the P2P link is up, the WebSocket closes
// and all media flows directly between the two machines.
//
// Cost: designed to run entirely on the Cloudflare Workers FREE plan.
//   - Durable Object uses the SQLite storage backend (required for the free
//     plan) but stores NOTHING — all room state is in-memory for the lifetime
//     of the pairing (seconds). Zero storage => zero storage billing.
//   - Room state is ephemeral; a room exists only while peers are connected.
//   - Free plan allowances (verified): 100k requests/day, 100k Durable Object
//     requests/day, 13k GB-s/day. A pairing is ~2 DO requests, so the free
//     tier covers tens of thousands of sessions per day.
//
// Protocol (JSON text frames over WebSocket at  wss://<worker>/rtc?room=CODE ):
//   server -> client : {"type":"role","role":"host"|"guest"}   (on connect)
//   server -> client : {"type":"waiting"}                       (first peer, alone)
//   server -> client : {"type":"peer-joined"}                   (both present)
//   server -> client : {"type":"peer-left"}                     (other side dropped)
//   server -> client : {"type":"error","reason":"room_full"}    (3rd joiner rejected)
//   client -> server : {"type":"description","sdp":"..."}       -> relayed to peer
//   client -> server : {"type":"candidate","candidate":"..."}   -> relayed to peer
// Any message the server does not itself originate is relayed verbatim to the
// other peer in the room, so the ICE signaling vocabulary can evolve without
// changing the server.

const MAX_PEERS = 2;

export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    // Lightweight health check (also handy to confirm a deploy is live).
    if (url.pathname === "/health") {
      return new Response("ok", { headers: { "content-type": "text/plain" } });
    }

    if (url.pathname !== "/rtc") {
      return new Response("Not found", { status: 404 });
    }

    const room = (url.searchParams.get("room") || "").toUpperCase().trim();
    if (!room) {
      return new Response("Missing room", { status: 400 });
    }
    // Basic sanity bound on room codes (they are user-chosen short words).
    if (room.length > 64) {
      return new Response("Room code too long", { status: 400 });
    }

    if (request.headers.get("Upgrade") !== "websocket") {
      return new Response("Expected websocket", { status: 426 });
    }

    // Route to the Durable Object that owns this room. idFromName is stable,
    // so both peers using the same room code reach the same object.
    const id = env.ROOMS.idFromName(room);
    const stub = env.ROOMS.get(id);
    return stub.fetch(request);
  },
};

// One Durable Object instance per room code. Holds up to two WebSockets and
// relays signaling messages between them. Uses the WebSocket Hibernation API
// so the object accrues no duration charges while idle between messages.
export class Room {
  constructor(state, env) {
    this.state = state;
  }

  async fetch(request) {
    const existing = this.state.getWebSockets();
    if (existing.length >= MAX_PEERS) {
      // A third joiner: reject before upgrading so the client sees a clean error.
      return new Response("room_full", { status: 403 });
    }

    const pair = new WebSocketPair();
    const client = pair[0];
    const server = pair[1];

    const role = existing.length === 0 ? "host" : "guest";

    // Hibernatable accept: the runtime, not this JS, holds the socket open.
    this.state.acceptWebSocket(server);
    server.serializeAttachment({ role });

    server.send(JSON.stringify({ type: "role", role }));

    if (existing.length === 1) {
      // Second peer just arrived: both sides are now present.
      server.send(JSON.stringify({ type: "peer-joined" }));
      try {
        existing[0].send(JSON.stringify({ type: "peer-joined" }));
      } catch (_) {
        // Stale socket; ignore — a close event will clean it up.
      }
    } else {
      server.send(JSON.stringify({ type: "waiting" }));
    }

    return new Response(null, { status: 101, webSocket: client });
  }

  // Relay every peer-originated message to the other side, untouched.
  async webSocketMessage(ws, message) {
    for (const other of this.state.getWebSockets()) {
      if (other === ws) continue;
      try {
        other.send(message);
      } catch (_) {
        // Peer went away mid-relay; the close handler will notify.
      }
    }
  }

  async webSocketClose(ws, code, reason, wasClean) {
    for (const other of this.state.getWebSockets()) {
      if (other === ws) continue;
      try {
        other.send(JSON.stringify({ type: "peer-left" }));
      } catch (_) {}
    }
    try {
      ws.close(1000, "closed");
    } catch (_) {}
  }

  async webSocketError(ws, error) {
    // Nothing to persist; hibernation will drop the socket. Peer-left is sent
    // on the subsequent close event.
  }
}
