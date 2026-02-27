/*
 * CKB WiFi Dashboard — Guition ESP32-S3-4848S040
 * ================================================
 * Full node health dashboard via direct CKB RPC over WiFi.
 * Displays: block height, time since last block, peers,
 *           mempool TX count, epoch progress, node status.
 *
 * RPCs used:
 *   get_tip_header       — height, timestamp, epoch, difficulty
 *   get_peers            — peer count
 *   get_raw_tx_pool      — mempool pending TX count
 *
 * Platform: PlatformIO + espressif32@6.5.0 (IDF 4.4.6)
 * Library:  Arduino_GFX 1.2.9 (lib/Arduino_GFX — factory version)
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "ckb_config.h"
#include <Arduino_GFX_Library.h>

/* 7-segment style fonts for block height display */
#include "fonts/Digital7Mono72.h"
#include "fonts/Digital7Mono48.h"
#include "fonts/Digital7Mono28.h"
#include "fonts/Digital7Mono14.h"

#define FONT_7SEG_HERO   (&digital_7__mono_48pt7b)
#define FONT_7SEG_MED    (&digital_7__mono_28pt7b)
#define FONT_7SEG_SMALL  (&digital_7__mono_14pt7b)

/* JMH Typewriter — slab serif for headings and labels */
#include "fonts/JMHTypewriterBold18.h"
#include "fonts/JMHTypewriterBold16.h"
#include "fonts/JMHTypewriterBold14.h"
#include "fonts/JMHTypewriterBold12.h"
#include "fonts/JMHTypewriter14.h"

/* Convenience: label font = Bold16, small label = Bold12 */
#define FONT_LABEL  (&JMH_Typewriter_Bold16pt7b)
#define FONT_SMALL  (&JMH_Typewriter_Bold12pt7b)

/* ═══════════════════════════════════════════════════════════════════
 * CONFIG
 * ═══════════════════════════════════════════════════════════════════ */
#define WIFI_SSID   "D-Link the router"
#define WIFI_PASS   "Ajeip853jw5590!"
#define CKB_RPC     "http://192.168.68.87:8114"
#define POLL_MS     6000       /* ~1 block time */

#define BL_PIN  38
#define W       480
#define H       480

/* ═══════════════════════════════════════════════════════════════════
 * COLOURS (RGB565)
 * ═══════════════════════════════════════════════════════════════════ */
#define COL_BG          0x0841  /* #101020 near-black blue */
#define COL_PANEL       0x10A3  /* #21264A dark card */
#define COL_ACCENT      0xFD00  /* #FF6800 CKB orange */
#define COL_ACCENT_DIM  0x9940  /* dimmed orange */
#define COL_OK          0x2FC6  /* #27C34C green */
#define COL_WARN        0xFE60  /* #FFCC00 amber */
#define COL_ERR         0xF800  /* #FF0000 red */
#define COL_TEXT        0xFFFF  /* white */
#define COL_DIM         0x8C51  /* #888 mid grey */
#define COL_DIVIDER     0x2965  /* subtle line */

/* ═══════════════════════════════════════════════════════════════════
 * DISPLAY
 * ═══════════════════════════════════════════════════════════════════ */
static Arduino_ESP32RGBPanel   *bus = nullptr;
static Arduino_ST7701_RGBPanel *gfx = nullptr;

static void init_display() {
    bus = new Arduino_ESP32RGBPanel(
        39, 48, 47,              /* CS, SCK, SDA */
        18, 17, 16, 21,          /* DE, VSYNC, HSYNC, PCLK */
        11,12,13,14,0,           /* R0-R4 */
        8,20,3,46,9,10,          /* G0-G5 */
        4,5,6,7,15);             /* B0-B4 */

    gfx = new Arduino_ST7701_RGBPanel(
        bus, GFX_NOT_DEFINED, 0, true, W, H,
        st7701_type1_init_operations, sizeof(st7701_type1_init_operations), true,
        10,8,50,  10,8,20);
}

/* ═══════════════════════════════════════════════════════════════════
 * STATE
 * ═══════════════════════════════════════════════════════════════════ */
struct NodeState {
    uint64_t height        = 0;
    uint64_t block_ts_ms   = 0;
    uint32_t peers         = 0;
    uint32_t mempool_tx    = 0;
    uint64_t epoch_num     = 0;
    uint32_t epoch_idx     = 0;
    uint32_t epoch_len     = 1800;
    bool     ok            = false;
    uint32_t last_ok_ms    = 0;
    uint32_t query_count   = 0;
    char     node_id[20]   = "";  /* last 16 chars of node id, e.g. "...a1b2c3d4" */
};
static NodeState state;
static ckb_cfg_t  cfg;     /* loaded from NVS at boot */

/* ═══════════════════════════════════════════════════════════════════
 * RPC HELPERS
 * ═══════════════════════════════════════════════════════════════════ */
static String rpc_call(const char *body) {
    if (WiFi.status() != WL_CONNECTED) return "";
    HTTPClient http;
    const char *url = (cfg.valid && cfg.node_url[0]) ? cfg.node_url : CKB_RPC;
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000);
    int code = http.POST(body);
    String resp = (code == 200) ? http.getString() : "";
    http.end();
    return resp;
}

static uint64_t parse_hex_field(const String &json, const char *key) {
    String search = String("\"") + key + "\":\"0x";
    int idx = json.indexOf(search);
    if (idx < 0) return 0;
    idx += search.length();
    int end = json.indexOf('"', idx);
    if (end < 0) return 0;
    String hex = "0x" + json.substring(idx, end);
    return (uint64_t)strtoull(hex.c_str(), nullptr, 16);
}

static uint32_t parse_array_length(const String &json, const char *key) {
    /* Counts items in a JSON array field — used for peers/mempool */
    String search = String("\"") + key + "\":[";
    int idx = json.indexOf(search);
    if (idx < 0) return 0;
    idx += search.length();
    if (json[idx] == ']') return 0;
    uint32_t count = 1;
    int depth = 1;
    while (idx < (int)json.length() && depth > 0) {
        char c = json[idx++];
        if (c == '[' || c == '{') depth++;
        else if (c == ']' || c == '}') { depth--; if (depth == 0) break; }
        else if (c == ',' && depth == 1) count++;
    }
    return count;
}

/* Parse epoch from compact string "0xNNNNNNNN" where:
 *   bits [0:15]  = block index within epoch
 *   bits [16:31] = epoch length
 *   bits [32:47] = epoch number               */
static void parse_epoch(const String &json, uint64_t &num, uint32_t &idx, uint32_t &len) {
    uint64_t v = parse_hex_field(json, "epoch");
    num = (v >> 32) & 0xFFFFFF;
    len = (v >> 16) & 0xFFFF;
    idx = v & 0xFFFF;
    if (len == 0) len = 1800;
}

static bool fetch_tip_header() {
    String resp = rpc_call(
        "{\"jsonrpc\":\"2.0\",\"method\":\"get_tip_header\",\"params\":[],\"id\":1}");
    if (resp.isEmpty()) return false;
    state.height     = parse_hex_field(resp, "number");
    state.block_ts_ms = parse_hex_field(resp, "timestamp");
    parse_epoch(resp, state.epoch_num, state.epoch_idx, state.epoch_len);
    return state.height > 0;
}

static void fetch_peers() {
    String resp = rpc_call(
        "{\"jsonrpc\":\"2.0\",\"method\":\"get_peers\",\"params\":[],\"id\":2}");
    if (!resp.isEmpty())
        state.peers = parse_array_length(resp, "result");
}

static void fetch_mempool() {
    String resp = rpc_call(
        "{\"jsonrpc\":\"2.0\",\"method\":\"get_raw_tx_pool\",\"params\":[false],\"id\":3}");
    if (!resp.isEmpty())
        state.mempool_tx = parse_array_length(resp, "pending");
}

static void fetch_node_id() {
    /* Fetch once — extract node_id, store last 16 hex chars prefixed with "..." */
    String resp = rpc_call(
        "{\"jsonrpc\":\"2.0\",\"method\":\"local_node_info\",\"params\":[],\"id\":4}");
    if (resp.isEmpty()) return;
    String search = "\"node_id\":\"";
    int idx = resp.indexOf(search);
    if (idx < 0) return;
    idx += search.length();
    int end = resp.indexOf('"', idx);
    if (end < 0) return;
    String full = resp.substring(idx, end);
    /* Keep last 16 chars */
    String short_id = "..." + full.substring(full.length() > 16 ? full.length() - 16 : 0);
    short_id.toCharArray(state.node_id, sizeof(state.node_id));
}

/* ═══════════════════════════════════════════════════════════════════
 * LAYOUT CONSTANTS
 * ═══════════════════════════════════════════════════════════════════ */
/*
 * 480×480 layout (portrait, USB at bottom):
 *
 *  ┌──────────────────────────────┐  y=0
 *  │  CKB NODE         ●          │  h=52  header
 *  ├──────────────────────────────┤  y=52
 *  │  block height (label)        │  h=24  label above number
 *  │  18,709,215  (48pt 7-seg)    │  h=80  hero number
 *  ├──────────────────────────────┤  y=156
 *  │  Last block: 4s ago          │  h=44  since bar
 *  ├──────────────────────────────┤  y=200
 *  │  Peers: 21   |  Mempool: 14  │  h=72  stats
 *  ├──────────────────────────────┤  y=272
 *  │  Epoch 3142  ████░░  67%     │  h=88  epoch bar
 *  ├──────────────────────────────┤  y=360
 *  │  node IP · polls · IP        │  h=30  footer
 *  └──────────────────────────────┘  y=390..480 (pad)
 */

#define HEADER_Y    0
#define HEADER_H    52
#define LABEL_Y     52
#define LABEL_H     24
#define HEIGHT_Y    76
#define HEIGHT_H    80
#define SINCE_Y     156
#define SINCE_H     44
#define STATS_Y     200
#define STATS_H     72
#define EPOCH_Y     272
#define EPOCH_H     79
#define FOOTER_Y    367
#define FOOTER_H    113

/* ═══════════════════════════════════════════════════════════════════
 * DRAW HELPERS
 * ═══════════════════════════════════════════════════════════════════ */
static void fill_section(int y, int h, uint16_t col) {
    gfx->fillRect(0, y, W, h, col);
}

static void draw_header(bool ok) {
    uint16_t accent = (cfg.valid) ? cfg.accent_col : COL_ACCENT;
    fill_section(HEADER_Y, HEADER_H, ok ? accent : COL_ERR);
    gfx->setFont(FONT_LABEL);
    gfx->setTextColor(0x0000);
    gfx->setTextSize(1);
    gfx->setCursor(14, HEADER_H - 23);
    gfx->print("CKB NODE");
    gfx->setFont(nullptr);
    /* Status dot */
    gfx->fillCircle(W-28, HEADER_H/2, 11, 0x0000);
    gfx->fillCircle(W-28, HEADER_H/2, 8, ok ? COL_OK : COL_BG);
}

static void draw_block_height(uint64_t h) {
    /* Label row */
    fill_section(LABEL_Y, LABEL_H, COL_BG);
    gfx->setFont(FONT_SMALL);
    gfx->setTextColor(COL_DIM);
    gfx->setTextSize(1);
    const char *lbl = "block height";
    int16_t lx1, ly1; uint16_t ltw, lth;
    gfx->getTextBounds(lbl, 0, LABEL_Y+LABEL_H, &lx1, &ly1, &ltw, &lth);
    gfx->setCursor((W - ltw)/2 - lx1, LABEL_Y + LABEL_H - 2);
    gfx->print(lbl);
    gfx->setFont(nullptr);

    /* Hero number — 48pt 7-seg fits 9 digits in 480px wide */
    fill_section(HEIGHT_Y, HEIGHT_H, COL_BG);
    char buf[24];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)h);

    gfx->setFont(&digital_7__mono_48pt7b);
    gfx->setTextColor((cfg.valid) ? cfg.accent_col : COL_ACCENT);
    gfx->setTextSize(1);

    int16_t x1, y1; uint16_t tw, th;
    gfx->getTextBounds(buf, 0, HEIGHT_Y + HEIGHT_H, &x1, &y1, &tw, &th);
    /* baseline: raise 2 more pixels (was -7, now -9) */
    gfx->setCursor((W - tw) / 2 - x1, HEIGHT_Y + HEIGHT_H - 9);
    gfx->print(buf);

    gfx->setFont(nullptr);
}

static void draw_since(uint64_t /*block_ts_ms*/) {
    fill_section(SINCE_Y, SINCE_H, COL_PANEL);
    gfx->drawFastHLine(0, SINCE_Y,            W, COL_DIVIDER);
    gfx->drawFastHLine(0, SINCE_Y+SINCE_H-1,  W, COL_DIVIDER);

    uint32_t age_s = state.last_ok_ms > 0 ? (millis() - state.last_ok_ms) / 1000 : 0;
    char label[40];
    if (state.last_ok_ms == 0)
        snprintf(label, sizeof(label), "Last block: --");
    else if (age_s < 60)
        snprintf(label, sizeof(label), "Last block:  %lus ago", (unsigned long)age_s);
    else if (age_s < 3600)
        snprintf(label, sizeof(label), "Last block:  %lum ago", (unsigned long)(age_s/60));
    else
        snprintf(label, sizeof(label), "Last block:  >1h ago!");

    uint16_t col = (age_s < 20) ? COL_OK : (age_s < 60) ? COL_WARN : COL_ERR;
    gfx->setFont(FONT_SMALL);
    gfx->setTextColor(col);
    gfx->setTextSize(1);
    int16_t sx, sy; uint16_t stw, sth;
    gfx->getTextBounds(label, 0, 0, &sx, &sy, &stw, &sth);
    /* Hardcoded to visual centre of SINCE band */
    gfx->setCursor((W - stw)/2 - sx, SINCE_Y + 28);
    gfx->print(label);
    gfx->setFont(nullptr);
}

static void draw_stats(uint32_t peers, uint32_t mempool) {
    fill_section(STATS_Y, STATS_H, COL_BG);
    gfx->drawFastVLine(W/2, STATS_Y+8, STATS_H-16, COL_DIVIDER);

    /* Left: Peers */
    gfx->setFont(FONT_SMALL);
    gfx->setTextSize(1);
    gfx->setTextColor(COL_DIM);
    gfx->setCursor(20, STATS_Y + 18);
    gfx->print("Peers");

    /* Peers value in 7-seg — vertically centred in zone */
    char pbuf[8];
    snprintf(pbuf, sizeof(pbuf), "%lu", (unsigned long)peers);
    uint16_t pcol = (peers >= 5) ? COL_OK : (peers > 0) ? COL_WARN : COL_ERR;
    gfx->setFont(&digital_7__mono_28pt7b);
    gfx->setTextColor(pcol);
    gfx->setTextSize(1);
    /* 28pt font is ~32px tall; zone is STATS_H=72px; label=16px; remaining=56px; centre of remaining ≈ label+28+14=label+42 */
    gfx->setCursor(20, STATS_Y + STATS_H - 10);
    gfx->print(pbuf);

    /* Right: Mempool */
    gfx->setFont(FONT_SMALL);
    gfx->setTextSize(1);
    gfx->setTextColor(COL_DIM);
    gfx->setCursor(W/2 + 20, STATS_Y + 18);
    gfx->print("Mempool");

    char mbuf[12];
    snprintf(mbuf, sizeof(mbuf), "%lu TX", (unsigned long)mempool);
    gfx->setFont(&digital_7__mono_28pt7b);
    gfx->setTextColor(COL_TEXT);
    gfx->setTextSize(1);
    gfx->setCursor(W/2 + 20, STATS_Y + STATS_H - 10);
    gfx->print(mbuf);

    gfx->setFont(nullptr);
}

static void draw_epoch(uint64_t num, uint32_t idx, uint32_t len) {
    fill_section(EPOCH_Y, EPOCH_H, COL_PANEL);
    gfx->drawFastHLine(0, EPOCH_Y, W, COL_DIVIDER);

    gfx->setFont(FONT_SMALL);
    gfx->setTextColor(COL_DIM);
    gfx->setTextSize(1);
    char ebuf[32];
    /* "Epoch" label in slab, epoch number in 7-seg */
    gfx->setCursor(20, EPOCH_Y + 22);
    gfx->print("Epoch");
    gfx->setFont(FONT_7SEG_SMALL);
    gfx->setTextColor(COL_TEXT);
    snprintf(ebuf, sizeof(ebuf), " %llu", (unsigned long long)num);
    gfx->print(ebuf);

    /* Progress bar — double height */
    int bar_x = 20, bar_y = EPOCH_Y+40, bar_w = W-40, bar_h = 36;
    gfx->fillRoundRect(bar_x, bar_y, bar_w, bar_h, 6, COL_DIVIDER);
    if (len > 0 && idx > 0) {
        /* Clamp filled to bar_w-1 so it never overdraws the right edge */
        int32_t filled = (int32_t)((uint64_t)(bar_w - 1) * idx / len);
        if (filled > bar_w - 1) filled = bar_w - 1;
        if (filled > 0)
            gfx->fillRect(bar_x, bar_y, filled, bar_h, COL_ACCENT);
        /* Redraw left rounded cap over the fill */
        gfx->fillCircle(bar_x + 6, bar_y + bar_h/2, 6, (filled > 0) ? COL_ACCENT : COL_DIVIDER);
    }

    uint32_t pct = (len > 0) ? (uint32_t)(100ULL * (idx < len ? idx : len) / len) : 0;
    snprintf(ebuf, sizeof(ebuf), "%lu%%", (unsigned long)pct);
    gfx->setFont(FONT_7SEG_SMALL);
    gfx->setTextColor(COL_TEXT);
    gfx->setTextSize(1);
    int16_t ex, ey; uint16_t etw, eth;
    gfx->getTextBounds(ebuf, 0, 0, &ex, &ey, &etw, &eth);
    gfx->setCursor(W - 20 - etw - ex, EPOCH_Y + 22);
    gfx->print(ebuf);
    gfx->setFont(nullptr);
}

static void draw_footer() {
    fill_section(FOOTER_Y, FOOTER_H, COL_BG);
    gfx->drawFastHLine(0, FOOTER_Y, W, COL_DIVIDER);
    char buf[48];
    int line_h = 24;
    int y1 = FOOTER_Y + 22;
    int y2 = y1 + line_h;
    int y3 = y2 + line_h + 1;
    int y4 = y3 + line_h + 1;

    /* Line 1: "node:" label + node IP in 7-seg */
    gfx->setFont(FONT_SMALL); gfx->setTextColor(COL_DIM); gfx->setTextSize(1);
    gfx->setCursor(8, y1); gfx->print("node:");
    gfx->setFont(FONT_7SEG_SMALL); gfx->setTextColor(COL_TEXT);
    gfx->print(" 192.168.68.87");

    /* Line 2: "polls:" label + count in 7-seg */
    gfx->setFont(FONT_SMALL); gfx->setTextColor(COL_DIM);
    gfx->setCursor(8, y2); gfx->print("polls:");
    gfx->setFont(FONT_7SEG_SMALL); gfx->setTextColor(COL_TEXT);
    snprintf(buf, sizeof(buf), " %lu", (unsigned long)state.query_count);
    gfx->print(buf);

    /* Line 3: "ip:" label + device IP in 7-seg */
    gfx->setFont(FONT_SMALL); gfx->setTextColor(COL_DIM);
    gfx->setCursor(8, y3); gfx->print("ip:");
    gfx->setFont(FONT_7SEG_SMALL); gfx->setTextColor(COL_TEXT);
    snprintf(buf, sizeof(buf), " %s", WiFi.localIP().toString().c_str());
    gfx->print(buf);

    /* Line 4: "id:" label + truncated node_id in 7-seg */
    if (state.node_id[0] != '\0') {
        gfx->setFont(FONT_SMALL); gfx->setTextColor(COL_DIM);
        gfx->setCursor(8, y4); gfx->print("id:");
        gfx->setFont(FONT_7SEG_SMALL); gfx->setTextColor(COL_DIM);
        snprintf(buf, sizeof(buf), " %s", state.node_id);
        gfx->print(buf);
    }

    gfx->setFont(nullptr);
}

static void draw_chrome() {
    /* Redraw all section backgrounds to eliminate any remnants */
    gfx->fillScreen(COL_BG);
    fill_section(HEADER_Y, HEADER_H, COL_ACCENT);
    fill_section(LABEL_Y,  LABEL_H,  COL_BG);
    fill_section(HEIGHT_Y, HEIGHT_H, COL_BG);
    fill_section(SINCE_Y,  SINCE_H,  COL_PANEL);
    fill_section(STATS_Y,  STATS_H,  COL_BG);
    fill_section(EPOCH_Y,  EPOCH_H,  COL_PANEL);
    fill_section(FOOTER_Y, FOOTER_H, COL_BG);
}

static void draw_splash() {
    gfx->fillScreen(COL_BG);
    /* "CKB NODE" in JMH Typewriter Bold, centred */
    gfx->setFont(FONT_LABEL);
    gfx->setTextColor(COL_ACCENT);
    gfx->setTextSize(2);
    int16_t x1, y1; uint16_t tw, th;
    gfx->getTextBounds("CKB NODE", 0, 0, &x1, &y1, &tw, &th);
    gfx->setCursor((W - tw) / 2 - x1, 210);
    gfx->print("CKB NODE");
    /* "connecting..." centred below */
    gfx->setFont(FONT_SMALL);
    gfx->setTextColor(COL_DIM);
    gfx->setTextSize(1);
    gfx->getTextBounds("connecting...", 0, 0, &x1, &y1, &tw, &th);
    gfx->setCursor((W - tw) / 2 - x1, 250);
    gfx->print("connecting...");
    gfx->setFont(nullptr);
}

/* ═══════════════════════════════════════════════════════════════════
 * WIFI
 * ═══════════════════════════════════════════════════════════════════ */
static void connect_wifi() {
    const char *ssid = (cfg.valid && cfg.wifi_ssid[0]) ? cfg.wifi_ssid : WIFI_SSID;
    const char *pass = (cfg.valid && cfg.wifi_pass[0]) ? cfg.wifi_pass : WIFI_PASS;
    Serial.printf("[WiFi] connecting to %s\n", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 30000) delay(300);
    if (WiFi.status() == WL_CONNECTED)
        Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
    else
        Serial.println("[WiFi] FAILED");
}

/* ═══════════════════════════════════════════════════════════════════
 * MAIN QUERY + RENDER
 * ═══════════════════════════════════════════════════════════════════ */
static void update() {
    state.query_count++;
    bool ok = fetch_tip_header();
    if (ok) {
        fetch_peers();
        fetch_mempool();
        if (state.query_count == 1) fetch_node_id(); /* once on first successful poll */
        state.ok = true;
        state.last_ok_ms = millis();
        Serial.printf("[OK] height=%llu peers=%lu pool=%lu epoch=%llu %lu/%lu\n",
            (unsigned long long)state.height,
            (unsigned long)state.peers,
            (unsigned long)state.mempool_tx,
            (unsigned long long)state.epoch_num,
            (unsigned long)state.epoch_idx,
            (unsigned long)state.epoch_len);
    } else {
        Serial.println("[ERR] RPC failed");
    }

    draw_header(state.ok);
    if (state.query_count == 1) draw_chrome(); /* full repaint on first update to clear any remnants */
    draw_block_height(state.height);
    draw_since(state.block_ts_ms);
    draw_stats(state.peers, state.mempool_tx);
    draw_epoch(state.epoch_num, state.epoch_idx, state.epoch_len);
    draw_footer();
}

/* ═══════════════════════════════════════════════════════════════════
 * SETUP / LOOP
 * ═══════════════════════════════════════════════════════════════════ */
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("[boot] CKB dashboard");
    ckb_config_check(3000);  /* 3s window for browser config session */
    cfg = ckb_config_load();  /* load saved config (colours, wifi, url) */

    init_display();
    pinMode(BL_PIN, OUTPUT);
    digitalWrite(BL_PIN, LOW);
    gfx->begin();
    gfx->fillScreen(0x0000);
    digitalWrite(BL_PIN, HIGH);
    delay(100);

    draw_splash();
    connect_wifi();
    delay(200);
}

void loop() {
    update();
    delay(POLL_MS);
}
