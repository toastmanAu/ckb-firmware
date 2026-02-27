# ckb-s3-wallet

Hardware CKB wallet running on the **Guition ESP32-S3-4848S040** — 480×480 touch display, signs and broadcasts transactions entirely on-device.

![Board](https://img.shields.io/badge/board-Guition%204848S040-orange) ![Framework](https://img.shields.io/badge/framework-Arduino-blue) ![Status](https://img.shields.io/badge/status-in%20development-yellow)

## What it is

A self-contained CKB wallet node. The private key never leaves the device — all signing happens on-chip using the [CKB-ESP32](https://github.com/toastmanAu/CKB-ESP32) SIGNER profile (secp256k1 + blake2b, proven on mainnet).

## Screens

| Screen | Description |
|--------|-------------|
| **Home** | Address (truncated), live balance, Send / Receive buttons |
| **Send** | Touch keyboard for address + amount, confirmation |
| **Confirm** | Review tx details, hold to sign & broadcast |
| **Receive** | Full address display + QR (planned) |
| **Result** | TX hash on success, error on failure |

## Build status

- [x] Display init + layout (shared from ckb-s3-node)
- [x] Home screen — balance, address, buttons
- [x] Receive screen — address display
- [x] Result screen — tx hash / error
- [x] Touch routing skeleton
- [x] Key load from NVS
- [x] Balance refresh (placeholder RPC call)
- [ ] GT911 touch driver (I2C)
- [ ] CKB-ESP32 library integration (key derivation, address, signing)
- [ ] Send screen — touch keyboard for address + amount
- [ ] `sendTransaction()` wired to confirm screen
- [ ] QR code display for receive

## Hardware

Same board as [ckb-s3-node](../ckb-s3-node):

| Item | Detail |
|------|--------|
| MCU | ESP32-S3-WROOM-1, dual-core 240MHz |
| Flash | 16MB QSPI |
| PSRAM | 8MB OPI |
| Display | 4" IPS 480×480, ST7701S, 16-bit RGB parallel |
| Touch | GT911 capacitive, I2C (SDA=19, SCL=45) |
| Backlight | GPIO 38 |

## Key security

Keys are stored in NVS (Preferences) — encrypted by ESP32-S3 flash encryption when enabled. For production use, enable Flash Encryption + Secure Boot in the ESP-IDF config.

See [CKB-ESP32 Key Security](https://github.com/toastmanAu/CKB-ESP32#key-security) for the full threat model.

## Configuration

Set via NVS config page (served on boot) or edit `src/main.cpp`:

```cpp
#define WIFI_SSID   "your-network"
#define CKB_RPC     "http://192.168.x.x:8114"
```

Private key: set via serial during provisioning — stored in NVS, never in firmware.

## Build

```bash
git clone https://github.com/toastmanAu/ckb-firmware.git
cd ckb-firmware/ckb-s3-wallet
pio run -e guition4848 -t upload
```

## Related

- [ckb-s3-node](../ckb-s3-node) — companion node monitor + broadcast relay
- [CKB-ESP32](https://github.com/toastmanAu/CKB-ESP32) — signing library
