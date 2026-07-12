#!/usr/bin/env bash
set -e

# Load GitHub credentials. The cron environment does NOT export GH_TOKEN,
# so source the canonical creds file. We then embed GH_TOKEN directly in the
# remote URL for the push so git never consults the (stale) credential store
# or the gh git-credential helper.
source "${HOME}/.hermes/scripts/gh-creds.env" 2>/dev/null || true

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "=== Galley Temps Sync ==="

# GitHub Pages is served from the main branch docs/. The telemetry collector
# keeps appending to data/temps.csv, so it is perpetually dirty in the working
# tree. Preserve those uncommitted lines across the branch switch so we never
# lose live telemetry, then commit onto main.
git stash push data/temps.csv 2>/dev/null || true
git checkout main
git stash pop 2>/dev/null || true

# Copy CSV log to docs/data/ for GH Pages
mkdir -p "$SCRIPT_DIR/docs/data"
cp "$SCRIPT_DIR/data/temps.csv" "$SCRIPT_DIR/docs/data/temps.csv"

echo "Syncing to GitHub Pages..."
git add docs/data/temps.csv docs/index.html

if git diff --cached --quiet; then
  echo "No changes to sync."
  exit 0
fi

git commit -m "update: sync telemetry data $(date -u "+%Y-%m-%d %H:%M UTC")"

# Push using GH_TOKEN embedded in the URL (bypasses credential store / gh helper).
# Always restore the token-less URL afterwards, even if the push fails.
cleanup() { git remote set-url origin "https://github.com/jsteve1/galley-temps.git" 2>/dev/null || true; }
trap cleanup EXIT
git remote set-url origin "https://x-access-token:${GH_TOKEN_WRITE}@github.com/jsteve1/galley-temps.git"
git push origin main

echo "Sync complete. Dashboard: https://jsteve1.github.io/galley-temps/"
