#!/usr/bin/env bash
set -e

# Load GitHub credentials. The cron environment does NOT export GH_TOKEN,
# so source the canonical creds file. We then embed the token directly in the
# remote URL for the push so git never consults the (stale) credential store
# or the gh git-credential helper.
source "/home/gasparilla/.hermes/scripts/gh-creds.env" 2>/dev/null || source "${HOME}/.hermes/scripts/gh-creds.env" 2>/dev/null || true
export GH_TOKEN GH_TOKEN_READ GH_TOKEN_WRITE

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

# Push with the embedded token (bypasses credential store / gh helper).
# Always restore the token-less URL afterwards, even on failure, so no token
# is ever left in git config.
cleanup() {
  git remote set-url origin "https://github.com/jsteve1/galley-temps.git" 2>/dev/null || true
  git remote set-url --push origin "https://github.com/jsteve1/galley-temps.git" 2>/dev/null || true
}
trap cleanup EXIT
push_with_token() {
  local tok="$1"
  git remote set-url origin "https://x-access-token:${tok}@github.com/jsteve1/galley-temps.git"
  git remote set-url --push origin "https://x-access-token:${tok}@github.com/jsteve1/galley-temps.git"
  git -c credential.helper= \
      -c credential.https://github.com.helper= \
      -c credential.https://gist.github.com.helper= push origin main
}

PUSHED=0
for CAND in GH_TOKEN GH_TOKEN_READ GH_TOKEN_WRITE; do
  tok="${!CAND}"
  [ -n "$tok" ] || continue
  echo "Attempting push with $CAND ..."
  if push_with_token "$tok"; then PUSHED=1; break; fi
  echo "$CAND denied (HTTP 403). Trying next credential." >&2
done

if [ "$PUSHED" -ne 1 ]; then
  echo "ERROR: git push to origin/main failed (HTTP 403 Permission denied) for every available credential." >&2
  echo "" >&2
  echo "Root cause: no available GitHub token can WRITE to jsteve1/galley-temps." >&2
  echo "  - GH_TOKEN / GH_TOKEN_READ (fine-grained, jsteve1): API reports push:true but the" >&2
  echo "    token has no write scope (no x-oauth-scopes); GitHub denies all writes." >&2
  echo "  - GH_TOKEN_WRITE (classic, gaspar-bot): has repo scope but gaspar-bot is not a" >&2
  echo "    collaborator on jsteve1/galley-temps, so it has no push access." >&2
  echo "" >&2
  echo "REMEDIATION (requires jsteve1):" >&2
  echo "  Option A: Create a classic PAT for jsteve1 with the repo scope (or a fine-grained" >&2
  echo "           PAT with Contents: Read and write for this repo) and put it in" >&2
  echo "           /home/gasparilla/.hermes/scripts/gh-creds.env as GH_TOKEN." >&2
  echo "  Option B: Add gaspar-bot as a collaborator with Write (or Admin) access to" >&2
  echo "           jsteve1/galley-temps; then GH_TOKEN_WRITE (already has repo scope) works." >&2
  exit 128
fi

echo "Sync complete. Dashboard: https://jsteve1.github.io/galley-temps/"
