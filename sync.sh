#!/usr/bin/env bash
# Sync data.csv and update GH Pages dashboard to github.com
# Usage: ./sync.sh
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_URL="$(git -C "$SCRIPT_DIR" remote get-url origin 2>/dev/null || echo "https://github.com/jsteve1/galley-temps.git")"

echo "=== Galley Temps Sync ==="

# Copy CSV log to docs/data/ for GH Pages
mkdir -p "$SCRIPT_DIR/docs/data"
cp "$SCRIPT_DIR/data/temps.csv" "$SCRIPT_DIR/docs/data/temps.csv"

# Configure GitHub Pages to serve from docs/
echo "Syncing to GitHub Pages..."
cd "$SCRIPT_DIR"
git add docs/data/temps.csv docs/index.html
git commit -m "update: sync telemetry data $(date -u '+%Y-%m-%d %H:%M UTC')"
git push origin main

echo "Sync complete. Dashboard: https://jsteve1.github.io/galley-temps/"
