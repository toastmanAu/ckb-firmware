/*
 * screenshot.h — HTTP screenshot endpoint for CKB S3 Node dashboard
 * ==================================================================
 * Enabled only when SCREENSHOT_SERVER is defined.
 *
 * Strategy for RGB panel (Arduino_ST7701_RGBPanel):
 *   RGB panels have no GRAM readback — you can't read pixels back over SPI.
 *   Instead, in screenshot builds, all draw calls go to an Arduino_Canvas
 *   (backed by PSRAM) which is then flushed to the panel. The canvas holds
 *   the full framebuffer, ready for JPEG encoding on demand.
 *
 * In main.cpp, when SCREENSHOT_SERVER is defined:
 *   - `gfx` is an Arduino_Canvas* (not Arduino_ST7701_RGBPanel*)
 *   - All existing gfx-> calls work unchanged (same Arduino_GFX API)
 *   - After each full redraw, call screenshot_flush() to push to panel
 *   - /screenshot endpoint on port 8080 serves a JPEG of the canvas
 *
 * Endpoints added to existing http_server (port 8080):
 *   GET /screenshot   — JPEG of current display frame
 *
 * Part of ckb-firmware dev-screenshot branch.
 * Zero impact on production builds — entirely #ifdef guarded.
 */

#pragma once

#ifdef SCREENSHOT_SERVER

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <WebServer.h>

// The panel and canvas are defined/managed here in screenshot builds
extern Arduino_ESP32RGBPanel   *ss_bus;
extern Arduino_Canvas          *ss_canvas;   // this IS 'gfx' in screenshot builds
extern Arduino_ST7701_RGBPanel *ss_panel;    // real display, canvas flushes to this

// Call once after WiFi is up, passing the existing http_server
void screenshot_init(WebServer &server);

// Call after each full screen redraw to push canvas → panel
void screenshot_flush();

// RGB565 → JPEG → WebServer response (called by HTTP handler internally)
void screenshot_serve(WebServer &server);

#endif // SCREENSHOT_SERVER
