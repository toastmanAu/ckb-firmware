# ckb-firmware

Nervos CKB firmware projects for ESP32 family boards — dashboards, miners, proxies, POS terminals, and HMI builds.

## Projects

| Project | Board | Description |
|---------|-------|-------------|
| `ckb-s3-dashboard` | Guition 4848S040 (ESP32-S3) | 480×480 CKB node stats dashboard |
| *(more coming)* | | |

## Libraries

Standalone repos, usable independently:

- [CKB-ESP32](https://github.com/toastmanAu/CKB-ESP32) — CKB RPC, signing, transaction building
- [TelegramSerial](https://github.com/toastmanAu/TelegramSerial) — Arduino Serial→Telegram bridge

## Structure

Each project is self-contained with its own `platformio.ini`, board defs, and local libs.
Shared assets (fonts, `ckb_config.h`) are copied into each project — no symlinks.

## Related

- [ckb-node-dashboard](https://github.com/toastmanAu/ckb-node-dashboard) — Node.js web dashboard
- [ckb-stratum-proxy](https://github.com/toastmanAu/ckb-stratum-proxy) — Stratum mining proxy
- [ckb-whale-bot](https://github.com/toastmanAu/ckb-whale-bot) — Telegram whale alert bot
