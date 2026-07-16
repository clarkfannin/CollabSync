# CollabSync

Real-time collaborative music plugin for macOS. Play and record with a friend over the internet, directly inside your DAW.

Supports VST3 and AU. Tested in FL Studio and Logic Pro.

---

## What you need

- macOS 12 or later
- A DAW that supports VST3 or AU plugins

No Tailscale, no accounts, no port forwarding — the plugin connects your two
machines directly, peer-to-peer, over the internet. Connections are made
through a free signaling server (see [NETWORKING.md](NETWORKING.md) if you want
to run your own); all audio/MIDI flows directly between you and your friend and
is end-to-end encrypted.

---

## Installation

1. Download and run `CollabSync-x.x.x.pkg`
2. Right-click the `.pkg` → **Open** (required on first install to bypass Gatekeeper)
3. Follow the installer prompts
4. Rescan plugins in your DAW

---

## Starting a session

Both people just agree on a **room code** — the plugin pairs you automatically.
There are no IP addresses to copy.

### Host (you)

1. Open your DAW and load CollabSync
2. In the plugin, click **Start Session**
3. Tell your friend to open CollabSync and click **Connect**

### Friend (joining)

1. Open their DAW and load CollabSync
2. Click **Connect**

Once both of you have connected, the status will show **● Connected**. Hit
**Record** on either machine to start a synchronized countdown and begin
recording.

> **Note:** this build pairs on a fixed room code, so run one session at a time
> per signaling server. Per-session room codes arrive with the UI update.

---

## Recording

- Click **Record** — both machines start a 4-beat countdown at your DAW's current BPM
- Both local and remote audio are recorded separately to your Desktop under **CollabSync Sessions/**
- When done, click **Stop**
- Drag the recorded files directly from the plugin into your DAW playlist

---

## Tips

- **Wired ethernet gives lower latency than WiFi.** If you can plug in, do it.
- The plugin adapts its buffer automatically — it starts low and increases if the connection is unstable.
- If you hear dropouts, give it 10–20 seconds and it will stabilize on its own.

---

## Troubleshooting

**Friend can't connect**
- Make sure both of you are using the same signaling server (build), and that
  only one pair is using it at a time (this build uses a fixed room code).
- Make sure the host clicked **Start Session** before the friend clicks **Connect**.
- A small number of networks (symmetric / carrier-grade NAT) can't be
  hole-punched directly and need a TURN relay, which this build does not ship.
  See [NETWORKING.md](NETWORKING.md).

**Dropouts / glitchy audio**
Normal on the first connection — the buffer is calibrating. Wait 10–20 seconds. If it doesn't improve, both users should switch to wired ethernet if possible.

---

## Building from source

Requires CMake 3.22+, Ninja, and Xcode command line tools.

```bash
git clone https://github.com/clarkfannin/CollabSync
cd CollabSync
./make.sh
```

The built installer will be at `dist/CollabSync-x.x.x.pkg`.

JUCE is not included in the repo. Add it as a submodule before building:

```bash
git submodule add https://github.com/juce-framework/JUCE.git JUCE
```

The networking dependencies (libjuice, libsodium, IXWebSocket, and — on
Windows/Linux — mbedTLS) are fetched automatically at configure time via CMake
`FetchContent`, so the first build needs network access. See
[NETWORKING.md](NETWORKING.md) for the architecture and how to deploy your own
free signaling server.
