/*
 * GT911 Touch Test — Guition ESP32-S3-4848S040
 * ==============================================
 * Scans I2C, initialises GT911, prints touch coordinates to serial.
 * Flash this first to verify SDA/SCL/INT/RST pins before integrating.
 *
 * Expected output:
 *   [GT911] scanning I2C (SDA=19 SCL=45)...
 *   [GT911] found device at 0x5D
 *   [GT911] product ID: 911
 *   [GT911] resolution: 480 × 480
 *   [GT911] touch ready — tap the screen
 *   touch x=240 y=120 points=1
 */

#include <Arduino.h>
#include <Wire.h>

/* ── Pin config — Guition 4848S040 ─────────────────────────────── */
#define GT911_SDA   19
#define GT911_SCL   45
#define GT911_INT   40
#define GT911_RST   41
#define GT911_ADDR  0x5D

/* ── GT911 registers ────────────────────────────────────────────── */
#define REG_STATUS  0x814E
#define REG_POINT1  0x814F
#define REG_X_MAX   0x8048
#define REG_PID     0x8140

static uint8_t gt_addr = GT911_ADDR;

static void gt_write(uint16_t reg, uint8_t val) {
    Wire.beginTransmission(gt_addr);
    Wire.write(reg >> 8); Wire.write(reg & 0xFF);
    Wire.write(val);
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
    delay(1000);
    Serial.println("\n[GT911 Touch Test]");

    /* Reset sequence */
    pinMode(GT911_RST, OUTPUT); pinMode(GT911_INT, OUTPUT);
    digitalWrite(GT911_INT, LOW); digitalWrite(GT911_RST, LOW);
    delay(10);
    digitalWrite(GT911_RST, HIGH);
    delay(10);
    pinMode(GT911_INT, INPUT);
    delay(50);

    Wire.begin(GT911_SDA, GT911_SCL);
    Wire.setClock(400000);

    /* I2C scan */
    Serial.printf("[GT911] scanning (SDA=%d SCL=%d)...\n", GT911_SDA, GT911_SCL);
    int found = 0;
    for (uint8_t a = 1; a < 127; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  found: 0x%02X\n", a);
            if (a == 0x5D || a == 0x14) gt_addr = a;
            found++;
        }
    }
    if (!found) { Serial.println("  none — check SDA/SCL"); return; }

    /* Product ID */
    uint8_t pid[5] = {0};
    gt_read(REG_PID, pid, 4);
    Serial.printf("[GT911] product ID: %s\n", pid);

    /* Resolution */
    uint8_t cfg[4];
    gt_read(REG_X_MAX, cfg, 4);
    uint16_t mx = cfg[0] | (cfg[1] << 8);
    uint16_t my = cfg[2] | (cfg[3] << 8);
    Serial.printf("[GT911] resolution: %d × %d\n", mx, my);
    Serial.println("[GT911] touch ready — tap the screen");
}

void loop() {
    uint8_t status = 0;
    gt_read(REG_STATUS, &status, 1);
    if ((status & 0x80) && (status & 0x0F) > 0) {
        uint8_t buf[8];
        gt_read(REG_POINT1, buf, 8);
        uint16_t x = buf[1] | (buf[2] << 8);
        uint16_t y = buf[3] | (buf[4] << 8);
        uint8_t n = status & 0x0F;
        Serial.printf("touch x=%d y=%d points=%d\n", x, y, n);
        uint8_t zero = 0;
        gt_write(REG_STATUS, 0);
        delay(50);
    }
    delay(10);
}
