/*
 * gt911.h — GT911 capacitive touch driver for Guition ESP32-S3-4848S040
 * ========================================================================
 * Confirmed pins: SDA=19, SCL=45, INT=40, RST=41, addr=0x5D
 * Uses interrupt + polling fallback for reliable detection.
 *
 * Usage:
 *   GT911 touch;
 *   touch.begin();          // call in setup()
 *   touch.update();         // call in loop()
 *   if (touch.pressed) { int x = touch.x; int y = touch.y; }
 */

#pragma once
#include <Arduino.h>
#include <Wire.h>

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

#define GT911_REG_STATUS  0x814E
#define GT911_REG_POINT1  0x814F
#define GT911_REG_PID     0x8140
#define GT911_REG_X_MAX   0x8048

class GT911 {
public:
    int     x = 0, y = 0;
    uint8_t points = 0;
    bool    pressed = false;

    bool begin(uint8_t addr = GT911_ADDR) {
        _addr = addr;

        /* Hardware reset — INT LOW → selects address 0x5D */
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

        /* Auto-detect address */
        Wire.beginTransmission(_addr);
        if (Wire.endTransmission() != 0) {
            _addr = (_addr == 0x5D) ? 0x14 : 0x5D;
            Wire.beginTransmission(_addr);
            if (Wire.endTransmission() != 0) {
                Serial.printf("[GT911] not found (SDA=%d SCL=%d)\n", GT911_SDA, GT911_SCL);
                return false;
            }
        }

        uint8_t pid[5] = {0};
        _readReg(GT911_REG_PID, pid, 4);
        Serial.printf("[GT911] addr=0x%02X PID=%s\n", _addr, pid);

        uint8_t cfg[4];
        _readReg(GT911_REG_X_MAX, cfg, 4);
        _max_x = cfg[0] | (cfg[1] << 8);
        _max_y = cfg[2] | (cfg[3] << 8);

        /* Attach interrupt */
        attachInterrupt(digitalPinToInterrupt(GT911_INT), _isr, FALLING);
        _irq = false;
        return true;
    }

    /* Call in loop() — updates x, y, points, pressed */
    bool update() {
        static uint32_t last_poll = 0;
        bool do_read = _irq;
        if (!do_read && millis() - last_poll > 16) {
            last_poll = millis();
            uint8_t s = 0;
            _readReg(GT911_REG_STATUS, &s, 1);
            if ((s & 0x80) && (s & 0x0F) > 0) do_read = true;
        }
        if (!do_read) { pressed = false; return false; }

        _irq = false;
        uint8_t status = 0;
        _readReg(GT911_REG_STATUS, &status, 1);
        uint8_t n = status & 0x0F;

        if ((status & 0x80) && n > 0) {
            uint8_t buf[8];
            _readReg(GT911_REG_POINT1, buf, 8);
            x = buf[1] | (buf[2] << 8);
            y = buf[3] | (buf[4] << 8);
            points = n;
            pressed = true;
        } else {
            pressed = false;
            points = 0;
        }

        uint8_t zero = 0;
        _writeReg(GT911_REG_STATUS, &zero, 1);
        return pressed;
    }

    /* Legacy one-shot read (no interrupt) */
    bool read() { return update(); }

    static volatile bool _irq;

private:
    uint8_t  _addr  = GT911_ADDR;
    uint16_t _max_x = 480, _max_y = 480;

    static void IRAM_ATTR _isr() { _irq = true; }

    void _writeReg(uint16_t reg, uint8_t* buf, uint8_t len) {
        Wire.beginTransmission(_addr);
        Wire.write(reg >> 8); Wire.write(reg & 0xFF);
        for (uint8_t i = 0; i < len; i++) Wire.write(buf[i]);
        Wire.endTransmission();
    }

    void _readReg(uint16_t reg, uint8_t* buf, uint8_t len) {
        Wire.beginTransmission(_addr);
        Wire.write(reg >> 8); Wire.write(reg & 0xFF);
        Wire.endTransmission(false);
        Wire.requestFrom(_addr, len);
        for (uint8_t i = 0; i < len && Wire.available(); i++) buf[i] = Wire.read();
    }
};

volatile bool GT911::_irq = false;
