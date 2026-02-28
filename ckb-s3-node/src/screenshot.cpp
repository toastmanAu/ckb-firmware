/*
 * screenshot.cpp — HTTP screenshot for CKB S3 Node (dev-screenshot branch)
 */

#ifdef SCREENSHOT_SERVER

#include "screenshot.h"
#include <WiFi.h>
#include "esp_camera.h"   // for fmt2jpg — part of arduino-esp32 SDK

Arduino_ESP32RGBPanel   *ss_bus    = nullptr;
Arduino_Canvas          *ss_canvas = nullptr;
Arduino_ST7701_RGBPanel *ss_panel  = nullptr;

// ── RGB565 → RGB888 ─────────────────────────────────────────────
static uint8_t* rgb565_to_rgb888(const uint16_t* src, int w, int h) {
    uint32_t n = (uint32_t)w * h;
    uint8_t* dst = (uint8_t*)ps_malloc(n * 3);
    if (!dst) dst = (uint8_t*)malloc(n * 3);
    if (!dst) return nullptr;
    for (uint32_t i = 0; i < n; i++) {
        uint16_t px = src[i];
        uint8_t r5 = (px >> 11) & 0x1F;
        uint8_t g6 = (px >> 5)  & 0x3F;
        uint8_t b5 =  px        & 0x1F;
        dst[i*3+0] = (r5 << 3) | (r5 >> 2);
        dst[i*3+1] = (g6 << 2) | (g6 >> 4);
        dst[i*3+2] = (b5 << 3) | (b5 >> 2);
    }
    return dst;
}

// ── HTTP handler ─────────────────────────────────────────────────
static WebServer *_server = nullptr;

static void handle_screenshot() {
    if (!ss_canvas) {
        _server->send(503, "text/plain", "Canvas not ready");
        return;
    }
    int w = ss_canvas->width();
    int h = ss_canvas->height();
    uint16_t* fb = (uint16_t*)ss_canvas->getFramebuffer();
    if (!fb) {
        _server->send(503, "text/plain", "Framebuffer not available");
        return;
    }

    uint8_t* rgb888 = rgb565_to_rgb888(fb, w, h);
    if (!rgb888) {
        _server->send(503, "text/plain", "OOM — not enough PSRAM");
        return;
    }

    uint8_t* jpegBuf = nullptr;
    size_t   jpegLen = 0;
    bool ok = fmt2jpg(rgb888, (size_t)w * h * 3, w, h,
                      PIXFORMAT_RGB888, 85, &jpegBuf, &jpegLen);
    free(rgb888);

    if (!ok || !jpegBuf) {
        _server->send(503, "text/plain", "JPEG encode failed");
        return;
    }

    _server->sendHeader("Content-Disposition", "inline; filename=\"ckb-node-monitor.jpg\"");
    _server->sendHeader("Cache-Control", "no-cache, no-store");
    _server->sendHeader("Access-Control-Allow-Origin", "*");
    _server->send_P(200, "image/jpeg", (const char*)jpegBuf, jpegLen);
    free(jpegBuf);

    Serial.printf("[Screenshot] Served %dx%d JPEG (%d bytes)\n", w, h, (int)jpegLen);
}

// ── Public API ───────────────────────────────────────────────────
void screenshot_init(WebServer &server) {
    _server = &server;
    server.on("/screenshot", HTTP_GET, handle_screenshot);
    Serial.printf("[Screenshot] endpoint: http://%s:8080/screenshot\n",
                  WiFi.localIP().toString().c_str());
}

void screenshot_flush() {
    if (!ss_canvas || !ss_panel) return;
    // Push canvas framebuffer to real RGB panel
    // Arduino_Canvas::flush() does this automatically if linked to parent
    ss_canvas->flush();
}

#endif // SCREENSHOT_SERVER
