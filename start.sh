#!/usr/bin/env bash
# Start galley-temps + tunnel
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Kill existing
pkill -f "galley-temps" 2>/dev/null || true
pkill -f "cloudflared.*18900" 2>/dev/null || true
sleep 1

# Build if needed
if [ ! -f "$SCRIPT_DIR/build/galley-temps" ]; then
    make -C "$SCRIPT_DIR" all
fi

# Start server
nohup "$SCRIPT_DIR/build/galley-temps" > "$SCRIPT_DIR/data/server.log" 2>&1 &
SERVER_PID=$!
echo "Server PID: $SERVER_PID"

# Wait for server
sleep 3
if ! curl -s http://localhost:18900/health > /dev/null 2>&1; then
    echo "Server failed to start. Log:"
    cat "$SCRIPT_DIR/data/server.log"
    exit 1
fi

# Start tunnel
nohup cloudflared tunnel --url http://localhost:18900 \
    > "$SCRIPT_DIR/data/cloudflared.log" 2>&1 &
TUNNEL_PID=$!
echo "Tunnel PID: $TUNNEL_PID"

# Wait for tunnel URL
sleep 3
TUNNEL_URL=$(grep -oP 'https://[a-z0-9\-]+\.trycloudflare\.com' "$SCRIPT_DIR/data/cloudflared.log" 2>/dev/null | head -1)
echo "Dashboard: ${TUNNEL_URL:-http://localhost:18900}"
echo "Server running on :18900"
