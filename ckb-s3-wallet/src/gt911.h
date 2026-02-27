/*
 * gt911.h — Minimal GT911 capacitive touch driver for Guition ESP32-S3-4848S040
 * ==============================================================================
 * Pins (Guition 4848S040):
 *   SDA  = 19
 *   SCL  = 45
 *   INT  = 40
 *   RST  = 41
 *
 * I2C address: 0x5D (INT pulled LOW on reset) or 0x14 (INT pulled HIGH)
 * Default: 0x5D
 *
 * Usage:
 *   GT911 touch;
 *   touch.begin();
 *   if (touch.read()) { int x = touch.x; int y = touch.y; }
 */

#pragma once
#include <Arduino.h>
#include <Wire.h>

/* ── Pin config ─────────────────────────────────────────────────── */
#ifndef GT911_SDA
#define GT911_SDA   19
#endif
#ifndef GT911_SCL
#define GT911_SCL   45
#endif
#ifndef GT911_INT
#define GT911_INT   40
#endif
#ifndef GT911_RST
#define GT911_RST   41
#endif
#ifndef GT911_ADDR
#define GT911_ADDR  0x5D
#endif

/* ── GT911 registers ────────────────────────────────────────────── */
#define GT911_REG_CMD        0x8040
#define GT911_REG_CFG_VER    0x8047
#define GT911_REG_X_MAX_LO   0x8048
#define GT911_REG_X_MAX_HI   0x8049
#define GT911_REG_Y_MAX_LO   0x804A
#define GT911_REG_Y_MAX_HI   0x804B
#define GT911_REG_TOUCH_NUM  0x804C   /* max touch points */
#define GT911_REG_STATUS     0x814E
#define GT911_REG_POINT1     0x814F

class GT911 {
public:
    int     x = 0, y = 0;
    uint8_t points = 0;
    bool    pressed = false;

    bool begin(uint8_t addr = GT911_ADDR) {
        _addr = addr;

        /* Hardware reset — pull RST + INT low, release RST */
        pinMode(GT911_RST, OUTPUT);
        pinMode(GT911_INT, OUTPUT);
        digitalWrite(GT911_INT, LOW);
        digitalWrite(GT911_RST, LOW);
        delay(10);
        digitalWrite(GT911_RST, HIGH);
        delay(10);
        /* INT high → address 0x28/0x29 (0x14); INT low → 0xBA/0xBB (0x5D) */
        /* We want 0x5D so keep INT low */
        pinMode(GT911_INT, INPUT);
        delay(50);

        Wire.begin(GT911_SDA, GT911_SCL);
        Wire.setClock(400000);

        /* Verify chip responds */
        Wire.beginTransmission(_addr);
        if (Wire.endTransmission() != 0) {
            /* Try alternate address */
            _addr = (_addr == 0x5D) ? 0x14 : 0x5D;
            Wire.beginTransmission(_addr);
            if (Wire.endTransmission() != 0) {
                Serial.printf("[GT911] not found at 0x5D or 0x14 (SDA=%d SCL=%d)\n",
                    GT911_SDA, GT911_SCL);
                return false;
            }
        }

        Serial.printf("[GT911] found at 0x%02X\n", _addr);

        /* Read product ID */
        uint8_t pid[5] = {0};
        _readReg(0x8140, pid, 4);
        Serial.printf("[GT911] product ID: %s\n", pid);

        /* Read config resolution */
        uint8_t cfg[4];
        _readReg(GT911_REG_X_MAX_LO, cfg, 4);
        _max_x = cfg[0] | (cfg[1] << 8);
        _max_y = cfg[2] | (cfg[3] << 8);
        Serial.printf("[GT911] resolution: %d × %d\n", _max_x, _max_y);

        return true;
    }

    /* Returns true if a touch is active. Updates x, y, points, pressed. */
    bool read() {
        uint8_t status = 0;
        _readReg(GT911_REG_STATUS, &status, 1);

        uint8_t n = status & 0x0F;
        bool buf_ready = (status & 0x80) != 0;

        if (!buf_ready) {
            pressed = false;
            points = 0;
            return false;
        }

        /* Clear status flag */
        uint8_t zero = 0;
        _writeReg(GT911_REG_STATUS, &zero, 1);

        if (n == 0) {
            pressed = false;
            points = 0;
            return false;
        }

        /* Read first touch point (8 bytes: id, x_lo, x_hi, y_lo, y_hi, size_lo, size_hi, reserved) */
        uint8_t buf[8];
        _readReg(GT911_REG_POINT1, buf, 8);

        x = buf[1] | (buf[2] << 8);
        y = buf[3] | (buf[4] << 8);
        points = n;
        pressed = true;
        return true;
    }

    /* I2C bus scan helper — call on boot to verify pins are correct */
    static void scanI2C() {
        Serial.printf("[GT911] scanning I2C (SDA=%d SCL=%d)...\n", GT911_SDA, GT911_SCL);
        Wire.begin(GT911_SDA, GT911_SCL);
        int found = 0;
        for (uint8_t a = 1; a < 127; a++) {
            Wire.beginTransmission(a);
            if (Wire.endTransmission() == 0) {
                Serial.printf("[GT911] found device at 0x%02X\n", a);
                found++;
            }
        }
        if (!found) Serial.println("[GT911] no I2C devices found — check SDA/SCL pins");
    }

private:
    uint8_t _addr = GT911_ADDR;
    uint16_t _max_x = 480, _max_y = 480;

    void _writeReg(uint16_t reg, uint8_t* buf, uint8_t len) {
        Wire.beginTransmission(_addr);
        Wire.write(reg >> 8);
        Wire.write(reg & 0xFF);
        for (uint8_t i = 0; i < len; i++) Wire.write(buf[i]);
        Wire.endTransmission();
    }

    void _readReg(uint16_t reg, uint8_t* buf, uint8_t len) {
        Wire.beginTransmission(_addr);
        Wire.write(reg >> 8);
        Wire.write(reg & 0xFF);
        Wire.endTransmission(false);
        Wire.requestFrom(_addr, len);
        for (uint8_t i = 0; i < len && Wire.available(); i++) buf[i] = Wire.read();
    }
};
