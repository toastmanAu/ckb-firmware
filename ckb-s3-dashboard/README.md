# ckb-s3-dashboard

CKB node stats dashboard firmware for the **Guition ESP32-S3-4848S040** — a 480×480 IPS touchscreen HMI board. Displays live Nervos CKB blockchain data pulled directly from a full node or light client via RPC over WiFi.

![Board](https://img.shields.io/badge/board-Guition%204848S040-orange) ![Framework](https://img.shields.io/badge/framework-Arduino-blue) ![Platform](https://img.shields.io/badge/platform-ESP32--S3-green)

## What's on screen

```
┌────────────────────────────────┐
│  ● CKB NODE          SYNCED   │  Header — status indicator
├────────────────────────────────┤
│         18,732,451             │  Block height — large 7-seg font
│        3s ago                  │  Time since last block
├────────────────────────────────┤
│  Peers  21    Mempool  142     │  Network stats
├────────────────────────────────┤
│  Epoch 3142  ████████░  67%   │  Epoch progress bar
└────────────────────────────────┘
```

- **Block height** — large Digital7 7-segment style font
- **Time since last block** — updates every poll (~6s)
- **Peers** — connected peer count
- **Mempool** — pending transaction count
- **Epoch progress** — bar + percentage, clamped to 100% at boundaries
- Status indicator — green=synced, amber=slow, red=error/timeout

## Hardware

| Item | Detail |
|------|--------|
| MCU | ESP32-S3-WROOM-1, dual-core 240MHz |
| Flash | 16MB QSPI |
| PSRAM | 8MB OPI |
| Display | 4" IPS 480×480, ST7701S, 16-bit RGB parallel |
| Touch | GT911 capacitive, I2C (not used in this firmware) |
| Backlight | GPIO 38 |
| USB | CH340 (ttyUSB0) |

## RPC calls used

| Method | Purpose |
|--------|---------|
| `get_tip_header` | Block height, timestamp, epoch |
| `get_peers` | Peer count |
| `get_raw_tx_pool` | Mempool TX count |

Works with any standard CKB full node or light client that exposes the JSON-RPC API.

## Configuration

Edit `src/ckb_config.h` or set via NVS at runtime (the board serves a config page on first boot / when unconfigured):

```cpp
#define WIFI_SSID   "your-network"
#define WIFI_PASS   "your-password"
#define CKB_RPC     "http://192.168.1.x:8114"   // full node
// or
#define CKB_RPC     "http://192.168.1.x:9000"   // light client
```

Poll interval is `POLL_MS` (default 6000ms — approximately one CKB block time).

## Build & Flash

Requires [PlatformIO](https://platformio.org/).

```bash
git clone https://github.com/toastmanAu/ckb-s3-dashboard.git
cd ckb-s3-dashboard
pio run -e guition4848 -t upload
```

First build downloads the espressif32 platform + toolchains (~500MB).

> **Note:** The local `lib/Arduino_GFX` is pinned to v1.2.9. The registry version (1.3.7+) dropped the `Arduino_ST7701_RGBPanel` integrated SW-SPI constructor required by this board — do not update it.

## Fonts

- **Digital7 Mono** — 7-segment style numerics (block height, epoch number)
- **JMH Typewriter Bold** — slab serif labels and headers

## Repo structure

```
ckb-s3-dashboard/
├── src/
│   ├── main.cpp          — main firmware
│   ├── ckb_config.h      — WiFi + RPC config, NVS support
│   └── fonts/            — embedded GFX fonts
├── lib/
│   └── Arduino_GFX/      — pinned v1.2.9 (local)
├── boards/
│   └── guition4848s040.json  — PlatformIO board definition
└── platformio.ini
```

## Related projects

- [CKB-ESP32](https://github.com/toastmanAu/CKB-ESP32) — Arduino library for CKB RPC, signing, and transaction building
- [NerdMiner_CKB](https://github.com/toastmanAu/NerdMiner_CKB) — ESP32 Eaglesong solo miner
- [ckb-stratum-proxy](https://github.com/toastmanAu/ckb-stratum-proxy) — Stratum proxy for CKB mining
