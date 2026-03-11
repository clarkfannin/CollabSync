#!/bin/bash
# Start a CollabSync session. Run this before you and your friend connect.

SIGNALING_DIR="$(cd "$(dirname "$0")/signaling" && pwd)"

# Get Tailscale IP
TAILSCALE_IP=$(tailscale ip -4 2>/dev/null)
if [ -z "$TAILSCALE_IP" ]; then
    echo "⚠️  Tailscale not running. Start Tailscale first."
    echo "   Download at https://tailscale.com"
    exit 1
fi

echo ""
echo "✓ CollabSync signaling server starting..."
echo ""
echo "  Tell your friend to use this IP in the plugin: $TAILSCALE_IP"
echo "  (Both of you use the same room code)"
echo ""
echo "  Press Ctrl+C to stop the session."
echo ""

node "$SIGNALING_DIR/server.js"
