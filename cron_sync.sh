#!/usr/bin/env bash
# Cron script: push CSV data to docs/ for GitHub Pages (runs every 2 minutes)
# Add to crontab: */2 * * * * /home/gasparilla/galley-temps/cron_sync.sh >> /home/gasparilla/galley-temps/data/cron.log 2>&1

SCRIPT_DIR="/home/gasparilla/galley-temps"
DATA="$SCRIPT_DIR/data/temps.csv"

# Only sync if log exists and has recent data
if [ -f "$DATA" ]; then
    cp "$DATA" "$SCRIPT_DIR/docs/data/temps.csv"
    cd "$SCRIPT_DIR"
    git add docs/data/temps.csv
    git commit -m "data: $(date -u '+%Y-%m-%dT%H:%M') UTC" > /dev/null 2>&1
fi
