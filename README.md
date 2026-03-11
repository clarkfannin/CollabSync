# CollabSync

Real-time collaborative music plugin for macOS. Play and record with a friend over the internet, directly inside your DAW.

Supports VST3 and AU. Tested in FL Studio and Logic Pro.

---

## What you need

- macOS 12 or later
- A DAW that supports VST3 or AU plugins
- [Tailscale](https://tailscale.com) (free) — used to connect to your friend securely without port forwarding

---

## Installation

1. Download and run `CollabSync-x.x.x.pkg`
2. Right-click the `.pkg` → **Open** (required on first install to bypass Gatekeeper)
3. Follow the installer prompts
4. Rescan plugins in your DAW

---

## Tailscale setup

Tailscale gives you and your friend private, routable IP addresses that can reach each other across the internet. **Both people need a free Tailscale account and need to be on the same tailnet.** You only need to do this once.

### Step 1 — Both people create a Tailscale account

1. Go to [tailscale.com](https://tailscale.com) and sign up (free — just a Google, Microsoft, or email login)
2. Download and install the Tailscale app for macOS
3. Log in — you'll see a Tailscale IP address (starts with `100.`) in the menu bar app

Both people need to complete this step before continuing.

### Step 2 — Host invites the guest to their tailnet

The person hosting the session needs to invite their friend so both machines can see each other.

1. Open the [Tailscale admin console](https://login.tailscale.com/admin/users)
2. Click **Invite users**
3. Enter your friend's email address (the one they used for their Tailscale account)
4. Your friend will receive an email — they click **Accept invite**

Your friend will now appear in your Tailscale network.

### Step 3 — Confirm you're connected

In the Tailscale menu bar app, both people should see each other listed. A green dot means you're ready to go.

---

## Starting a session

### Host (you)

1. Open your DAW and load CollabSync
2. In the plugin, scroll to **Host a Session** and click **Start Session**
3. Your Tailscale IP will appear — copy it and send it to your friend (text, Discord, whatever)
4. Give your friend a room code — any short word works (e.g. `STUDIO1`)
5. Enter the same room code in the **Room** field and click **Connect**

### Friend (joining)

1. Open their DAW and load CollabSync
2. Enter the room code you gave them in the **Room** field
3. Enter your Tailscale IP in the **Host** field
4. Click **Connect**

Once both of you have connected, the status will show **● Connected**. Hit **Record** on either machine to start a synchronized countdown and begin recording.

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
- The **Host** field defaults to `localhost` when you're hosting. Your friend types your Tailscale IP.

---

## Troubleshooting

**"Tailscale not detected" in the plugin**
Tailscale is not running. Open the Tailscale menu bar app and make sure you're logged in and connected.

**Friend can't connect**
- Confirm both machines are online in the Tailscale admin console
- Make sure the room codes match exactly (case-insensitive)
- Make sure you clicked **Start Session** before your friend tries to connect

**Port 8765 already in use**
Another instance of CollabSync is running, or a previous session didn't close cleanly. Restart your DAW.

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
