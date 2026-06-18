// firmware/arduino-r4-base/src/led_matrix.cpp

#include <Arduino.h>
#include <string.h>
#include <Arduino_LED_Matrix.h>
#include "led_matrix.h"

static ArduinoLEDMatrix matrix;
static uint32_t last_step = 0;
static uint32_t flash_started = 0;
static uint8_t scan_col = 0;

void led_matrix_init() {
    matrix.begin();
}

void led_matrix_update(bool record_just_forwarded) {
    uint32_t now = millis();
    uint8_t frame[8][12] = {};  // 8 rows, 12 cols, all off

    if (record_just_forwarded) flash_started = now;

    if (now - flash_started < 80) {
        memset(frame, 1, sizeof(frame));  // Flash all LEDs briefly on forward
    } else {
        if (now - last_step > 60) {
            scan_col = (scan_col + 1) % 12;
            last_step = now;
        }
        frame[3][scan_col] = 1;  // Heartbeat sweep
        frame[4][scan_col] = 1;
    }

    matrix.renderBitmap(frame, 8, 12);
}
