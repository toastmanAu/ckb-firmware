# ckb-s3-node

An embedded **Nervos CKB light client node** running on the **Guition ESP32-S3-4848S040** HMI board. Connects to a CKB full node or light client via WiFi, tracks live chain state, and renders it on the built-in 480×480 display.

![Board](https://img.shields.io/badge/board-Guition%204848S040-orange) ![Framework](https://img.shields.io/badge/framework-Arduino-blue) ![Platform](https://img.shields.io/badge/platform-ESP32--S3-green)

## What it is

This is a **chain-aware embedded node**, not just a dashboard. It polls the CKB RPC continuously, maintains a local view of chain state (block height, epoch, peers, mempool), and displays that state on the HMI panel. The display is the output — the node behaviour is the core.

**Current capabilities:**
- Tracks tip block height and timestamp
- Monitors peer count and mempool depth
- Tracks epoch progress
- Detects sync status and node health
- Configurable RPC endpoint (full node or light client)

**Roadmap:**
- Address watching (balance tracking)
- Transaction signing via CKB-ESP32 SIGNER profile
- Send CKB from the device (touch UI)

## Display

```
┌────────────────────────────────┐
│  ● CKB NODE          SYNCED   │  Status indicator
├────────────────────────────────┤
│         18,732,451             │  Block height — 7-seg font
│           3s ago               │  Time since last block
├────────────────────────────────┤
│  Peers  21       Mempool  142  │  Network stats
├────────────────────────────────┤
│  Epoch 3142  ████████░  67%   │  Epoch progress bar
└────────────────────────────────┘
```

## Hardware

| Item | Detail |
|------|--------|
| MCU | ESP32-S3-WROOM-1, dual-core 240MHz |
| Flash | 16MB QSPI |
| PSRAM | 8MB OPI |
| Display | 4" IPS 480×480, ST7701S, 16-bit RGB parallel |
| Touch | GT911 capacitive, I2C |
| Backlight | GPIO 38 |
| USB | CH340 (ttyUSB0) |

## RPC methods used

| Method | Purpose |
|--------|---------|
| `get_tip_header` | Block height, timestamp, epoch |
| `get_peers` | Peer count |
| `get_raw_tx_pool` | Mempool TX count |

Compatible with any standard CKB full node (`port 8114`) or light client (`port 9000`).

## Configuration

Edit `src/ckb_config.h` — or configure via NVS at runtime (served on first boot):

```cpp
#define WIFI_SSID   "your-network"
#define WIFI_PASS   "your-password"
#define CKB_RPC     "http://192.168.1.x:8114"   // full node
// or
#define CKB_RPC     "http://192.168.1.x:9000"   // light client
```

Poll interval: `POLL_MS` (default 6000ms ≈ one CKB block time).

## Build & Flash

Requires [PlatformIO](https://platformio.org/).

```bash
git clone https://github.com/toastmanAu/ckb-s3-node.git
cd ckb-s3-node
pio run -e guition4848 -t upload
```

First build downloads the espressif32 platform + toolchains (~500MB).

> **Note:** `lib/Arduino_GFX` is pinned to v1.2.9. The registry version (1.3.7+) removed the `Arduino_ST7701_RGBPanel` integrated SW-SPI constructor required by this board — do not update it.

## Part of ckb-firmware

This project lives in the [toastmanAu/ckb-firmware](https://github.com/toastmanAu/ckb-firmware) monorepo alongside other CKB ESP32 firmware projects.

## Related

- [CKB-ESP32](https://github.com/toastmanAu/CKB-ESP32) — Arduino library: RPC, signing, transaction building
- [NerdMiner_CKB](https://github.com/toastmanAu/NerdMiner_CKB) — ESP32 Eaglesong solo miner
- [ckb-stratum-proxy](https://github.com/toastmanAu/ckb-stratum-proxy) — Stratum mining proxy
