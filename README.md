# Galley Temps

System telemetry monitor — Crow C++ server collecting CPU/GPU temps, usage, memory, and disk, with a live GitHub Pages dashboard.

## Hardware

- **CPU**: AMD Ryzen 7 3700X (8C/16T)
- **GPU**: 2× NVIDIA GeForce RTX 3090
- **RAM**: 128GB

## Architecture

```
every 30s: collect metrics → append to data/temps.csv
                    │
                    ├── /api/current  (live JSON via tunnel)
                    └── docs/data/temps.csv → GitHub Pages dashboard
```

### Components

- **C++ Server** (`src/main.cpp`): Crow HTTP server on port 18900
  - Background thread collects metrics every 30s
  - `/api/current` — live JSON snapshot
  - `/api/metrics` — raw CSV log
- **GitHub Pages** (`docs/index.html`): Chart.js dashboard, dark theme
- **Cron sync** (`cron_sync.sh`): Copies CSV to docs/ and pushes to GitHub every 2 min
- **Tunnel** (`serve.sh`): Starts server + Cloudflare tunnel for remote access

## Quick Start

```bash
git clone https://github.com/jsteve1/galley-temps.git
make all
./build/galley-temps
```

Server starts on port 18900. Dashboard at https://jsteve1.github.io/galley-temps/

### With tunnel

```bash
./serve.sh
```

Requires `cloudflared` installed and configured.

## CSV Format

Fixed columns (header + data rows):

```
timestamp,cpu_usage,cpu_temp,mem_pct,disk_pct,uptime_h,gpu0_util,gpu0_temp,gpu1_util,gpu1_temp
2026-06-01T05:57:54,8.7,80.6,13.7,44.0,1.2,31.0,67.0,49.0,86.0
```

## Setup Cron (optional)

```bash
crontab -e
*/2 * * * * /home/gasparilla/galley-temps/cron_sync.sh >> /home/gasparilla/galley-temps/data/cron.log 2>&1
```

This pushes telemetry data to GitHub Pages every 2 minutes.
