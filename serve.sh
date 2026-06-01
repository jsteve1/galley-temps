#!/usr/bin/env bash
# Start the monitoring server + cloudflared tunnel
# Usage: ./serve.sh
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TUNNEL_TOKEN_FILE="$SCRIPT_DIR/tunnel.token"
TUNNEL_URL_FILE="$SCRIPT_DIR/data/tunnel_url.txt"

# Build if needed
make -C "$SCRIPT_DIR" all

# Start server in background
echo "Starting server on port 18900..."
gnome-terminal -- bash -c "cd $SCRIPT_DIR && ./build/galley-temps; exec bash" 2>/dev/null || \
x-terminal-emulator -e "bash -c 'cd $SCRIPT_DIR && ./build/galley-temps; exec bash'" 2>/dev/null || {
    nohup "$SCRIPT_DIR/build/galley-temps" > "$SCRIPT_DIR/data/server.log" 2>&1 &
    echo "Server PID: $!"
}

sleep 2

# Start cloudflared tunnel
if command -v cloudflared &>/dev/null && [ -f "$TUNNEL_TOKEN_FILE" ]; then
    echo "Starting Cloudflare tunnel..."
    nohup cloudflared tunnel --url http://localhost:18900 \
        > "$SCRIPT_DIR/data/cloudflared.log" 2>&1 &
    echo "Cloudflared PID: $!"

    # Wait for tunnel URL
    sleep 3
    if [ -f "$SCRIPT_DIR/data/cloudflared.log" ]; then
        grep -oP 'https://[a-z]+\.trycloudflare\.com' "$SCRIPT_DIR/data/cloudflared.log" | head -1 | \
            tee "$TUNNEL_URL_FILE" | xargs echo "Tunnel URL:"
    fi
else
    echo "No cloudflared or tunnel token found."
    echo "To enable: cloudflared tunnel --url http://localhost:18900"
fi

echo "Server running at http://localhost:18900"
echo "Dashboard: https://jsteve1.github.io/galley-temps/"
