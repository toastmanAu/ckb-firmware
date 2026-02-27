/*
 * ckb_config.h — CKB Dashboard Config via USB Serial
 * ====================================================
 * Drop into any CKB dashboard project.
 *
 * On boot, listens 3s for a browser config session:
 *   Browser → "CKBCFG\n"
 *   Device  → "READY\n"
 *   Browser → JSON blob + "\nEND\n"
 *   Device  → "OK\n" then reboots
 *
 * JSON keys (all optional):
 *   wifi_ssid    string
 *   wifi_pass    string
 *   node_url     string   e.g. "http://192.168.1.5:8114"
 *   accent_r     0-255
 *   accent_g     0-255
 *   accent_b     0-255
 *   bg_r         0-255
 *   bg_g         0-255
 *   bg_b         0-255
 *
 * Reading saved config:
 *   ckb_cfg_t cfg = ckb_config_load();
 *   cfg.accent_col  — RGB565
 *   cfg.bg_col      — RGB565
 *   cfg.node_url    — char[128]
 *   cfg.wifi_ssid   — char[64]
 *   cfg.wifi_pass   — char[64]
 *   cfg.valid       — true if NVS has been written at least once
 */

#pragma once
#include <Arduino.h>
#include <Preferences.h>

/* ── Config struct ─────────────────────────────────────────────── */
struct ckb_cfg_t {
    char     wifi_ssid[64];
    char     wifi_pass[64];
    char     node_url[128];
    uint16_t accent_col;   /* RGB565 */
    uint16_t bg_col;       /* RGB565 */
    bool     valid;
};

/* ── RGB → RGB565 ──────────────────────────────────────────────── */
static inline uint16_t rgb_to_565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

/* ── Tiny JSON field extractors (no heap, no lib) ──────────────── */
static bool json_str(const String &json, const char *key, char *out, size_t outlen) {
    String search = String("\"") + key + "\":\"";
    int idx = json.indexOf(search);
    if (idx < 0) return false;
    idx += search.length();
    int end = json.indexOf('"', idx);
    if (end < 0) return false;
    String val = json.substring(idx, end);
    strncpy(out, val.c_str(), outlen - 1);
    out[outlen - 1] = '\0';
    return true;
}

static int json_int(const String &json, const char *key, int def = -1) {
    String search = String("\"") + key + "\":";
    int idx = json.indexOf(search);
    if (idx < 0) return def;
    idx += search.length();
    return json.substring(idx, idx + 6).toInt();
}

/* ── Load from NVS ─────────────────────────────────────────────── */
static ckb_cfg_t ckb_config_load() {
    ckb_cfg_t cfg = {};
    Preferences prefs;
    prefs.begin("ckbcfg", true);  /* read-only */
    cfg.valid = prefs.getBool("valid", false);
    if (cfg.valid) {
        prefs.getString("ssid", cfg.wifi_ssid, sizeof(cfg.wifi_ssid));
        prefs.getString("pass", cfg.wifi_pass, sizeof(cfg.wifi_pass));
        prefs.getString("url",  cfg.node_url,  sizeof(cfg.node_url));
        cfg.accent_col = prefs.getUShort("accent", 0xFD00);
        cfg.bg_col     = prefs.getUShort("bg",     0x0841);
    }
    prefs.end();
    return cfg;
}

/* ── Save to NVS ───────────────────────────────────────────────── */
static void ckb_config_save(const ckb_cfg_t &cfg) {
    Preferences prefs;
    prefs.begin("ckbcfg", false);  /* read-write */
    prefs.putBool("valid",  true);
    prefs.putString("ssid", cfg.wifi_ssid);
    prefs.putString("pass", cfg.wifi_pass);
    prefs.putString("url",  cfg.node_url);
    prefs.putUShort("accent", cfg.accent_col);
    prefs.putUShort("bg",     cfg.bg_col);
    prefs.end();
}

/* ── Config mode — call at start of setup() ────────────────────── */
static bool ckb_config_check(unsigned long timeout_ms = 3000) {
    /* Wait for magic header */
    unsigned long t0 = millis();
    String line = "";

    Serial.println("[cfg] waiting...");

    while (millis() - t0 < timeout_ms) {
        while (Serial.available()) {
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
                line.trim();
                if (line == "CKBCFG") goto got_magic;
                line = "";
            } else {
                line += c;
            }
        }
        delay(10);
    }
    Serial.println("[cfg] no config session");
    return false;

got_magic:
    Serial.println("READY:" CKB_BOARD_ID);
    Serial.flush();

    /* Collect JSON until "END" line */
    String json = "";
    line = "";
    unsigned long t1 = millis();
    while (millis() - t1 < 10000) {
        while (Serial.available()) {
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
                line.trim();
                if (line == "END") goto got_end;
                json += line;
                line = "";
                t1 = millis();  /* reset timeout on each line received */
            } else {
                line += c;
            }
        }
        delay(5);
    }
    Serial.println("TIMEOUT");
    return false;

got_end:
    Serial.println("[cfg] parsing...");

    ckb_cfg_t cfg = ckb_config_load();  /* load existing as base */

    /* Parse each field if present */
    char tmp[128];
    if (json_str(json, "wifi_ssid", tmp, sizeof(tmp)))
        strncpy(cfg.wifi_ssid, tmp, sizeof(cfg.wifi_ssid));
    if (json_str(json, "wifi_pass", tmp, sizeof(tmp)))
        strncpy(cfg.wifi_pass, tmp, sizeof(cfg.wifi_pass));
    if (json_str(json, "node_url", tmp, sizeof(tmp)))
        strncpy(cfg.node_url, tmp, sizeof(cfg.node_url));

    int ar = json_int(json, "accent_r", -1);
    int ag = json_int(json, "accent_g", -1);
    int ab = json_int(json, "accent_b", -1);
    if (ar >= 0 && ag >= 0 && ab >= 0)
        cfg.accent_col = rgb_to_565(ar, ag, ab);

    int br = json_int(json, "bg_r", -1);
    int bg = json_int(json, "bg_g", -1);
    int bb = json_int(json, "bg_b", -1);
    if (br >= 0 && bg >= 0 && bb >= 0)
        cfg.bg_col = rgb_to_565(br, bg, bb);

    cfg.valid = true;
    ckb_config_save(cfg);

    Serial.println("OK");
    Serial.flush();
    delay(200);
    ESP.restart();
    return true;  /* never reached */
}
