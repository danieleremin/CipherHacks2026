// firmware/esp32-sensor/src/rangefinder.cpp
// Rangefinder task: VL53L1X (I²C) or TF-Luna (UART), compile-time selected.

#include "rangefinder.h"
#include "config.h"

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static volatile float s_distance_m = -1.0f;

// ── VL53L1X implementation ─────────────────────────────────────────────────
#if defined(RANGEFINDER_VL53L1X)

#include <Wire.h>
#include <Adafruit_VL53L1X.h>

// Pass -1 for XSHUT pin — we are not using the hardware shutdown line.
// Without this the library defaults to an invalid pin number and crashes
// with "gpio_num error" on boot.
static Adafruit_VL53L1X s_vl53(-1);

static void range_task_fn(void* arg) {
    (void)arg;

    if (!s_vl53.begin(0x29, &Wire)) {
        Serial.println("[RANGE] VL53L1X not found — check wiring.");
        s_distance_m = -1.0f;
        vTaskDelete(NULL);
        return;
    }

    s_vl53.startRanging();
    s_vl53.setTimingBudget(50);

    Serial.println("[RANGE] VL53L1X ready.");

    while (true) {
        if (s_vl53.dataReady()) {
            int16_t dist_mm = s_vl53.distance();
            s_vl53.clearInterrupt();

            if (dist_mm > 0 && dist_mm < 4500) {
                s_distance_m = dist_mm / 1000.0f;
            } else {
                s_distance_m = -1.0f;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ── TF-Luna implementation ─────────────────────────────────────────────────
#elif defined(RANGEFINDER_TF_LUNA)

#include <Wire.h>

static const uint8_t TF_LUNA_ADDR = 0x10;

static bool tf_luna_read(uint16_t* dist_cm, uint16_t* strength) {
    Wire.requestFrom(TF_LUNA_ADDR, (uint8_t)9);
    if (Wire.available() < 9) return false;

    uint8_t buf[9];
    for (int i = 0; i < 9; i++) buf[i] = Wire.read();

    if (buf[0] != 0x59 || buf[1] != 0x59) return false;

    uint8_t csum = 0;
    for (int i = 0; i < 8; i++) csum += buf[i];
    if (csum != buf[8]) return false;

    *dist_cm  = (uint16_t)(buf[3] << 8) | buf[2];
    *strength = (uint16_t)(buf[5] << 8) | buf[4];
    return true;
}

static void range_task_fn(void* arg) {
    (void)arg;
    Serial.println("[RANGE] TF-Luna ready (I2C mode, addr 0x10).");

    while (true) {
        uint16_t dist_cm = 0, strength = 0;
        if (tf_luna_read(&dist_cm, &strength)) {
            if (strength > 100 && dist_cm > 0 && dist_cm <= 800) {
                s_distance_m = dist_cm / 100.0f;
            } else {
                s_distance_m = -1.0f;
            }
        } else {
            s_distance_m = -1.0f;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ── No rangefinder ─────────────────────────────────────────────────────────
#else

static void range_task_fn(void* arg) {
    (void)arg;
    s_distance_m = -1.0f;
    vTaskDelete(NULL);
}

#endif

// ── Public API ─────────────────────────────────────────────────────────────

void rangefinder_task_start() {
    xTaskCreatePinnedToCore(
        range_task_fn,
        "range_task",
        4096,   // Increased from 2048 — VL53L1X library needs more stack
        NULL,
        3,
        NULL,
        1
    );
}

float rangefinder_get_distance() {
    return s_distance_m;
}