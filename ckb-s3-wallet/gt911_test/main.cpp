/*
 * GT911 Touch Test — Guition ESP32-S3-4848S040
 * Interrupt-driven version
 */

#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>

/* ── Touch pins ─────────────────────────────────────────────────── */
#define GT911_SDA   19
#define GT911_SCL   45
#define GT911_INT   40
#define GT911_RST   41
#define REG_STATUS  0x814E
#define REG_POINT1  0x814F
#define REG_PID     0x8140

/* ── Display ────────────────────────────────────────────────────── */
#define GFX_BL 38
#define W 480
#define H 480

static Arduino_ESP32RGBPanel   *bus = nullptr;
static Arduino_ST7701_RGBPanel *gfx = nullptr;

static void init_display() {
    bus = new Arduino_ESP32RGBPanel(
        39, 48, 47,
        18, 17, 16, 21,
        11,12,13,14,0,
        8,20,3,46,9,10,
        4,5,6,7,15);
    gfx = new Arduino_ST7701_RGBPanel(
        bus, GFX_NOT_DEFINED, 0, true, W, H,
        st7701_type1_init_operations, sizeof(st7701_type1_init_operations), true,
        10,8,50, 10,8,20);
}

/* ── GT911 ──────────────────────────────────────────────────────── */
static uint8_t gt_addr = 0x5D;
static volatile bool touch_irq = false;

static void IRAM_ATTR gt_isr() { touch_irq = true; }

static void gt_write(uint16_t reg, uint8_t val) {
    Wire.beginTransmission(gt_addr);
    Wire.write(reg >> 8); Wire.write(reg & 0xFF); Wire.write(val);
    Wire.endTransmission();
}

static bool gt_read(uint16_t reg, uint8_t* buf, uint8_t len) {
    Wire.beginTransmission(gt_addr);
    Wire.write(reg >> 8); Wire.write(reg & 0xFF);
    if (Wire.endTransmission(false) != 0) return false;
    Wire.requestFrom(gt_addr, len);
    for (uint8_t i = 0; i < len && Wire.available(); i++) buf[i] = Wire.read();
    return true;
}

void setup() {
    Serial.begin(115200);
    delay(500);

    init_display();
    gfx->begin();
    pinMode(GFX_BL, OUTPUT);
    digitalWrite(GFX_BL, HIGH);
    gfx->fillScreen(BLACK);
    gfx->setTextColor(WHITE);
    gfx->setTextSize(3);
    gfx->setCursor(60, 200);
    gfx->print("Init touch...");

    /* GT911 reset — INT LOW → addr 0x5D */
    pinMode(GT911_RST, OUTPUT);
    pinMode(GT911_INT, OUTPUT);
    digitalWrite(GT911_INT, LOW);
    digitalWrite(GT911_RST, LOW);
    delay(10);
    digitalWrite(GT911_RST, HIGH);
    delay(10);
    pinMode(GT911_INT, INPUT);
    delay(50);

    Wire.begin(GT911_SDA, GT911_SCL);
    Wire.setClock(400000);

    /* Scan */
    for (uint8_t a = 1; a < 127; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            Serial.printf("I2C: 0x%02X\n", a);
            if (a == 0x5D || a == 0x14) gt_addr = a;
        }
    }

    uint8_t pid[5] = {0};
    gt_read(REG_PID, pid, 4);
    Serial.printf("GT911 addr=0x%02X PID=%s\n", gt_addr, pid);

    /* Attach interrupt on INT pin — FALLING edge = touch event */
    attachInterrupt(digitalPinToInterrupt(GT911_INT), gt_isr, FALLING);

    gfx->fillScreen(BLACK);
    gfx->setTextColor(0x07E0);
    gfx->setTextSize(3);
    gfx->setCursor(60, 210);
    gfx->printf("GT911 0x%02X  PID=%s", gt_addr, pid);
    gfx->setCursor(130, 260);
    gfx->setTextColor(WHITE);
    gfx->print("Tap anywhere!");

    Serial.println("Ready — tap the screen");
}

static int last_x = -1, last_y = -1;
static uint32_t tap_count = 0;

void loop() {
    /* Also poll status register every 16ms as fallback */
    static uint32_t last_poll = 0;
    bool do_read = touch_irq;
    if (!do_read && millis() - last_poll > 16) {
        last_poll = millis();
        uint8_t s = 0;
        gt_read(REG_STATUS, &s, 1);
        if ((s & 0x80) && (s & 0x0F) > 0) do_read = true;
    }

    if (do_read) {
        touch_irq = false;
        uint8_t status = 0;
        gt_read(REG_STATUS, &status, 1);
        uint8_t n = status & 0x0F;

        if ((status & 0x80) && n > 0) {
            uint8_t buf[8];
            gt_read(REG_POINT1, buf, 8);
            int x = buf[1] | (buf[2] << 8);
            int y = buf[3] | (buf[4] << 8);
            tap_count++;

            Serial.printf("touch #%u x=%d y=%d pts=%d\n", tap_count, x, y, n);

            if (last_x >= 0) gfx->fillCircle(last_x, last_y, 22, BLACK);
            gfx->fillCircle(x, y, 20, 0xF800);
            gfx->fillCircle(x, y,  6, WHITE);

            gfx->fillRect(0, 415, 480, 65, BLACK);
            gfx->setTextColor(0xFFE0);
            gfx->setTextSize(3);
            gfx->setCursor(30, 430);
            gfx->printf("#%u  x=%3d  y=%3d  pts=%d", tap_count, x, y, n);

            last_x = x; last_y = y;
        }
        gt_write(REG_STATUS, 0);  /* always clear */
    }

    delay(5);
}
