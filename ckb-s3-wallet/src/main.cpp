/*
 * CKB S3 Wallet — Guition ESP32-S3-4848S040
 * ===========================================
 * Hardware CKB wallet with 480×480 touch display.
 * Signs and broadcasts transactions entirely on-device.
 *
 * Architecture:
 *   - Key stored in NVS (Preferences), never leaves device
 *   - Balance polled via CKB indexer RPC (getCells)
 *   - Transaction built + signed via CKB-ESP32 SIGNER profile
 *   - Broadcast via send_transaction RPC or delegated to ckb-s3-node
 *
 * Screens:
 *   BOOT      — splash + WiFi connect
 *   HOME      — address (truncated), balance, Send / Receive buttons
 *   SEND      — address input (touch keyboard), amount input, confirm
 *   CONFIRM   — review tx details, swipe/hold to sign & broadcast
 *   RECEIVE   — display own address as QR (future) + text
 *   RESULT    — tx hash or error message
 *
 * Platform: PlatformIO + espressif32@6.5.0 (IDF 4.4.6)
 * Library:  CKB-ESP32 (SIGNER profile), Arduino_GFX 1.2.9 (local)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include "ckb_config.h"
#include "gt911.h"
#include <Arduino_GFX_Library.h>

/* Fonts */
#include "fonts/Digital7Mono48.h"
#include "fonts/Digital7Mono28.h"
#include "fonts/Digital7Mono14.h"
#include "fonts/JMHTypewriterBold18.h"
#include "fonts/JMHTypewriterBold16.h"
#include "fonts/JMHTypewriterBold14.h"
#include "fonts/JMHTypewriterBold12.h"
#include "fonts/JMHTypewriter14.h"

#define FONT_HERO    (&digital_7__mono_28pt7b)
#define FONT_MED     (&digital_7__mono_14pt7b)
#define FONT_LABEL   (&JMH_Typewriter_Bold16pt7b)
#define FONT_SMALL   (&JMH_Typewriter_Bold12pt7b)
#define FONT_BODY    (&JMH_Typewriter_14pt7b)

/* ═══════════════════════════════════════════════════════════════════
 * CONFIG — override via ckb_config.h NVS or edit here
 * ═══════════════════════════════════════════════════════════════════ */
#define WIFI_SSID    "D-Link the router"
#define WIFI_PASS    ""                            /* set via ckb_config */
#define CKB_RPC      "http://192.168.68.87:8114"   /* full node */
#define CKB_INDEXER  "http://192.168.68.87:8116"   /* indexer (or same port) */

#define BL_PIN  38
#define W       480
#define H       480

/* ═══════════════════════════════════════════════════════════════════
 * COLOURS (RGB565)
 * ═══════════════════════════════════════════════════════════════════ */
#define COL_BG          0x0841   /* #101020 near-black */
#define COL_PANEL       0x10A3   /* #21264A dark card */
#define COL_ACCENT      0xFD00   /* #FF6800 CKB orange */
#define COL_OK          0x2FC6   /* green */
#define COL_WARN        0xFE60   /* amber */
#define COL_ERR         0xF800   /* red */
#define COL_TEXT        0xFFFF   /* white */
#define COL_DIM         0x8C51   /* grey */
#define COL_DIVIDER     0x2965   /* subtle line */
#define COL_BTN_SEND    0xFD00   /* orange — send */
#define COL_BTN_RECV    0x2FC6   /* green — receive */
#define COL_BTN_CANCEL  0x4228   /* dark grey */

/* ═══════════════════════════════════════════════════════════════════
 * SCREENS
 * ═══════════════════════════════════════════════════════════════════ */
enum Screen { SCREEN_BOOT, SCREEN_HOME, SCREEN_SEND,
              SCREEN_CONFIRM, SCREEN_RECEIVE, SCREEN_RESULT };

static Screen current_screen = SCREEN_BOOT;

/* ═══════════════════════════════════════════════════════════════════
 * WALLET STATE
 * ═══════════════════════════════════════════════════════════════════ */
struct WalletState {
    char     address[100];       /* bech32m mainnet address */
    char     privkey_hex[65];    /* 32 bytes hex — loaded from NVS */
    uint64_t balance_shannon;    /* live balance */
    float    balance_ckb;
    bool     key_loaded;
    bool     balance_ok;

    /* Send flow */
    char     send_to[100];
    float    send_amount_ckb;
    char     last_tx_hash[67];
    char     last_error[128];
    bool     tx_ok;
};

static WalletState wallet = {};
static ckb_cfg_t   cfg    = {};

/* ═══════════════════════════════════════════════════════════════════
 * DISPLAY
 * ═══════════════════════════════════════════════════════════════════ */
static Arduino_ESP32RGBPanel   *bus = nullptr;
static Arduino_ST7701_RGBPanel *gfx = nullptr;
static GT911 touch;

static void init_display() {
    bus = new Arduino_ESP32RGBPanel(
        39, 48, 47,
        18, 17, 16, 21,
        11,12,13,14,0,
        10,9,46,3,20,8,
        15,7,6,5,4,
        0, 0, 0, 0, 0,
        1, 10, 8, 50, 1, 10, 8, 50);

    gfx = new Arduino_ST7701_RGBPanel(bus, GFX_NOT_DEFINED, 0,
        TL021WVC02_INIT_OPERATIONS, sizeof(TL021WVC02_INIT_OPERATIONS),
        true, 480, 480, 0, 0, 0, 0);
}

static void fill_rect(int x, int y, int w, int h, uint16_t col) {
    gfx->fillRect(x, y, w, h, col);
}

/* ─── Rounded button helper ─────────────────────────────────────── */
static void draw_button(int x, int y, int w, int h,
                        uint16_t col, const char *label,
                        const GFXfont *font = nullptr) {
    gfx->fillRoundRect(x, y, w, h, 10, col);
    if (font) gfx->setFont(font);
    gfx->setTextColor(COL_TEXT);
    gfx->setTextSize(1);
    int16_t tx, ty; uint16_t tw, th;
    gfx->getTextBounds(label, 0, 0, &tx, &ty, &tw, &th);
    gfx->setCursor(x + (w - tw)/2 - tx, y + (h + th)/2 - ty/2);
    gfx->print(label);
    gfx->setFont(nullptr);
}

/* ═══════════════════════════════════════════════════════════════════
 * SCREEN: HOME
 * Layout:
 *   [0   – 52 ] Header — "CKB WALLET" + status dot
 *   [52  – 92 ] Address (truncated)
 *   [92  – 220] Balance (large 7-seg)
 *   [220 – 260] "CKB" label
 *   [260 – 360] [  SEND  ] [RECEIVE] buttons
 *   [360 – 480] Footer — node URL, last update
 * ═══════════════════════════════════════════════════════════════════ */
static void draw_home() {
    gfx->fillScreen(COL_BG);

    /* Header */
    fill_rect(0, 0, W, 52, COL_ACCENT);
    gfx->setFont(FONT_LABEL); gfx->setTextColor(COL_TEXT); gfx->setTextSize(1);
    gfx->setCursor(16, 34); gfx->print("CKB WALLET");
    /* Status dot */
    uint16_t dot = wallet.balance_ok ? COL_OK : COL_WARN;
    gfx->fillCircle(W - 24, 26, 8, dot);
    gfx->setFont(nullptr);

    /* Address (truncated: first 12 + … + last 6) */
    fill_rect(0, 52, W, 40, COL_PANEL);
    gfx->setFont(FONT_SMALL); gfx->setTextColor(COL_DIM); gfx->setTextSize(1);
    char addr_disp[32] = "no key";
    if (wallet.key_loaded && wallet.address[0]) {
        snprintf(addr_disp, sizeof(addr_disp), "%.12s...%s",
            wallet.address,
            wallet.address + strlen(wallet.address) - 6);
    }
    gfx->setCursor(16, 78); gfx->print(addr_disp);
    gfx->setFont(nullptr);

    /* Balance */
    fill_rect(0, 92, W, 128, COL_BG);
    gfx->setFont(FONT_HERO); gfx->setTextColor(COL_TEXT); gfx->setTextSize(1);
    char bal_buf[32];
    if (wallet.balance_ok)
        snprintf(bal_buf, sizeof(bal_buf), "%.2f", wallet.balance_ckb);
    else
        snprintf(bal_buf, sizeof(bal_buf), "-.--");
    int16_t bx, by; uint16_t bw, bh;
    gfx->getTextBounds(bal_buf, 0, 0, &bx, &by, &bw, &bh);
    gfx->setCursor((W - bw)/2 - bx, 92 + 100);
    gfx->print(bal_buf);
    gfx->setFont(nullptr);

    /* "CKB" sub-label */
    fill_rect(0, 220, W, 40, COL_BG);
    gfx->setFont(FONT_SMALL); gfx->setTextColor(COL_DIM); gfx->setTextSize(1);
    gfx->setCursor(W/2 - 14, 250); gfx->print("CKB");
    gfx->setFont(nullptr);

    /* Buttons */
    fill_rect(0, 260, W, 100, COL_BG);
    draw_button(20,  278, 200, 64, COL_BTN_SEND, "SEND",    FONT_LABEL);
    draw_button(260, 278, 200, 64, COL_BTN_RECV, "RECEIVE", FONT_LABEL);

    /* Footer */
    fill_rect(0, 360, W, 120, COL_PANEL);
    gfx->drawFastHLine(0, 360, W, COL_DIVIDER);
    gfx->setFont(FONT_SMALL); gfx->setTextColor(COL_DIM); gfx->setTextSize(1);
    const char *rpc = (cfg.valid && cfg.node_url[0]) ? cfg.node_url : CKB_RPC;
    gfx->setCursor(12, 388); gfx->print(rpc);
    gfx->setCursor(12, 416); gfx->print(WiFi.localIP().toString().c_str());
    gfx->setFont(nullptr);
}

/* ═══════════════════════════════════════════════════════════════════
 * SCREEN: RESULT
 * ═══════════════════════════════════════════════════════════════════ */
static void draw_result() {
    gfx->fillScreen(COL_BG);
    uint16_t hcol = wallet.tx_ok ? COL_OK : COL_ERR;
    fill_rect(0, 0, W, 52, hcol);
    gfx->setFont(FONT_LABEL); gfx->setTextColor(COL_TEXT); gfx->setTextSize(1);
    gfx->setCursor(16, 34);
    gfx->print(wallet.tx_ok ? "SENT" : "FAILED");
    gfx->setFont(nullptr);

    gfx->setFont(FONT_SMALL); gfx->setTextColor(COL_DIM); gfx->setTextSize(1);
    if (wallet.tx_ok) {
        gfx->setCursor(12, 100); gfx->print("TX Hash:");
        gfx->setTextColor(COL_TEXT);
        /* wrap hash across 2 lines */
        char line1[35], line2[35];
        strncpy(line1, wallet.last_tx_hash, 34); line1[34] = 0;
        strncpy(line2, wallet.last_tx_hash + 34, 34); line2[34] = 0;
        gfx->setCursor(12, 130); gfx->print(line1);
        gfx->setCursor(12, 158); gfx->print(line2);
    } else {
        gfx->setCursor(12, 100); gfx->print("Error:");
        gfx->setTextColor(COL_ERR);
        gfx->setCursor(12, 130); gfx->print(wallet.last_error);
    }
    gfx->setFont(nullptr);

    draw_button(20, 380, 440, 64, COL_BTN_CANCEL, "BACK TO HOME", FONT_LABEL);
}

/* ═══════════════════════════════════════════════════════════════════
 * SCREEN: RECEIVE
 * ═══════════════════════════════════════════════════════════════════ */
static void draw_receive() {
    gfx->fillScreen(COL_BG);
    fill_rect(0, 0, W, 52, COL_BTN_RECV);
    gfx->setFont(FONT_LABEL); gfx->setTextColor(COL_TEXT); gfx->setTextSize(1);
    gfx->setCursor(16, 34); gfx->print("RECEIVE CKB");
    gfx->setFont(nullptr);

    /* Address in chunks */
    gfx->setFont(FONT_SMALL); gfx->setTextColor(COL_TEXT); gfx->setTextSize(1);
    gfx->setCursor(12, 90); gfx->print("Your address:");
    gfx->setTextColor(COL_ACCENT);
    /* Print address in 3 lines of ~30 chars */
    int alen = strlen(wallet.address);
    char chunk[32];
    for (int i = 0, line = 0; i < alen && line < 4; i += 30, line++) {
        strncpy(chunk, wallet.address + i, 30); chunk[30] = 0;
        gfx->setCursor(12, 118 + line * 28); gfx->print(chunk);
    }
    gfx->setFont(nullptr);

    /* QR placeholder */
    fill_rect(140, 260, 200, 200, COL_PANEL);
    gfx->setFont(FONT_SMALL); gfx->setTextColor(COL_DIM); gfx->setTextSize(1);
    gfx->setCursor(158, 368); gfx->print("QR coming soon");
    gfx->setFont(nullptr);

    draw_button(20, 420, 440, 52, COL_BTN_CANCEL, "BACK", FONT_LABEL);
}

/* ═══════════════════════════════════════════════════════════════════
 * WIFI + BALANCE
 * ═══════════════════════════════════════════════════════════════════ */
static void connect_wifi() {
    const char *ssid = (cfg.valid && cfg.wifi_ssid[0]) ? cfg.wifi_ssid : WIFI_SSID;
    const char *pass = (cfg.valid && cfg.wifi_pass[0]) ? cfg.wifi_pass : WIFI_PASS;
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(300);
}

/* TODO: replace with CKB-ESP32 getBalance() once library linked in */
static void refresh_balance() {
    /* Placeholder — calls get_cells_capacity via indexer */
    if (WiFi.status() != WL_CONNECTED || !wallet.key_loaded) return;
    HTTPClient http;
    const char *url = (cfg.valid && cfg.node_url[0]) ? cfg.node_url : CKB_RPC;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    char body[256];
    snprintf(body, sizeof(body),
        "{\"jsonrpc\":\"2.0\",\"method\":\"get_cells_capacity\","
        "\"params\":[{\"script\":{\"code_hash\":"
        "\"0x9bd7e06f3ecf4be0f2fcd2188b23f1b9fcc88e5d4b65a8637b17723bbda3cce8\","
        "\"hash_type\":\"type\",\"args\":\"%s\"},\"script_type\":\"lock\"}],"
        "\"id\":1}", "0x4454b23e1523b8f9e88a00c4c521179f444351f4"); /* TODO: derive from key */
    int code = http.POST(body);
    if (code == 200) {
        String resp = http.getString();
        /* parse "capacity":"0x..." */
        int idx = resp.indexOf("\"capacity\":\"0x");
        if (idx >= 0) {
            uint64_t shannon = strtoull(resp.c_str() + idx + 14, nullptr, 16);
            wallet.balance_shannon = shannon;
            wallet.balance_ckb = (float)shannon / 100000000.0f;
            wallet.balance_ok = true;
        }
    }
    http.end();
}

/* ═══════════════════════════════════════════════════════════════════
 * KEY MANAGEMENT (NVS)
 * ═══════════════════════════════════════════════════════════════════ */
static void load_key() {
    Preferences prefs;
    prefs.begin("ckb-wallet", true);
    String key = prefs.getString("privkey", "");
    prefs.end();

    if (key.length() == 64) {
        strncpy(wallet.privkey_hex, key.c_str(), 64);
        wallet.privkey_hex[64] = 0;
        wallet.key_loaded = true;
        /* TODO: derive address from key via CKB-ESP32 CKBKey::fromHex() + toAddress() */
        strncpy(wallet.address, "ckb1...(derive from key)", sizeof(wallet.address));
        Serial.println("[wallet] key loaded from NVS");
    } else {
        wallet.key_loaded = false;
        Serial.println("[wallet] no key in NVS — set via serial config");
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * TOUCH (GT911 via gt911.h)
 * SDA=19, SCL=45, INT=40, RST=41 — Guition 4848S040 standard
 * ═══════════════════════════════════════════════════════════════════ */
static bool touch_get(int *tx, int *ty) {
    if (touch.read() && touch.pressed) {
        *tx = touch.x;
        *ty = touch.y;
        return true;
    }
    return false;
}

/* ═══════════════════════════════════════════════════════════════════
 * TOUCH ROUTING
 * ═══════════════════════════════════════════════════════════════════ */
static void handle_touch(int tx, int ty) {
    switch (current_screen) {
        case SCREEN_HOME:
            /* SEND button: x=20–220, y=278–342 */
            if (tx >= 20 && tx <= 220 && ty >= 278 && ty <= 342) {
                current_screen = SCREEN_SEND;
                /* TODO: draw_send() */
            }
            /* RECEIVE button: x=260–460, y=278–342 */
            if (tx >= 260 && tx <= 460 && ty >= 278 && ty <= 342) {
                current_screen = SCREEN_RECEIVE;
                draw_receive();
            }
            break;
        case SCREEN_RECEIVE:
            /* BACK button: y=420–472 */
            if (ty >= 420) { current_screen = SCREEN_HOME; draw_home(); }
            break;
        case SCREEN_RESULT:
            /* BACK button: y=380–444 */
            if (ty >= 380) { current_screen = SCREEN_HOME; draw_home(); }
            break;
        default:
            break;
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * SETUP / LOOP
 * ═══════════════════════════════════════════════════════════════════ */
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("[boot] CKB S3 Wallet");

    ckb_config_check(3000);
    cfg = ckb_config_load();

    init_display();
    pinMode(BL_PIN, OUTPUT);
    digitalWrite(BL_PIN, LOW);
    gfx->begin();
    gfx->fillScreen(0x0000);
    digitalWrite(BL_PIN, HIGH);

    /* Splash */
    gfx->setFont(FONT_LABEL); gfx->setTextColor(COL_ACCENT); gfx->setTextSize(2);
    gfx->setCursor(80, 210); gfx->print("CKB WALLET");
    gfx->setFont(FONT_SMALL); gfx->setTextColor(COL_DIM); gfx->setTextSize(1);
    gfx->setCursor(160, 260); gfx->print("starting...");
    gfx->setFont(nullptr);

    load_key();
    connect_wifi();

    /* Touch — scan I2C first to verify pins, then init GT911 */
    GT911::scanI2C();
    if (!touch.begin()) {
        Serial.println("[boot] GT911 init failed — touch disabled");
    } else {
        Serial.println("[boot] GT911 touch ready");
    }

    refresh_balance();

    current_screen = SCREEN_HOME;
    draw_home();
}

static uint32_t last_balance_ms = 0;
#define BALANCE_INTERVAL_MS  30000   /* refresh balance every 30s */

void loop() {
    /* Balance refresh */
    if (millis() - last_balance_ms > BALANCE_INTERVAL_MS) {
        refresh_balance();
        last_balance_ms = millis();
        if (current_screen == SCREEN_HOME) draw_home();
    }

    /* Touch */
    int tx, ty;
    if (touch_get(&tx, &ty)) {
        handle_touch(tx, ty);
        delay(150);  /* debounce */
    }

    delay(20);
}
