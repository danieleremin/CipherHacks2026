// firmware/esp32-sensor/src/rangefinder.cpp
// Rangefinder task: VL53L1X (I²C) or TF-Luna (UART), compile-time selected.
// See SPEC_ESP32_SENSOR_NODE.md Section 2.7.

#include "rangefinder.h"
#include "config.h"

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Shared distance value. Written by range task, read by wifi_scan.cpp.
// On Xtensa LX7, aligned 32-bit reads/writes are atomic, so no mutex needed
// for a single float. Declared volatile to prevent compiler caching.
static volatile float s_distance_m = -1.0f;

// ── VL53L1X implementation ─────────────────────────────────────────────────
#if defined(RANGEFINDER_VL53L1X)

#include <Wire.h>
#include <Adafruit_VL53L1X.h>

static Adafruit_VL53L1X s_vl53;

static void range_task_fn(void* arg) {
    (void)arg;

    // Wire must be initialized before this task starts (done in main setup())
    if (!s_vl53.begin(0x29, &Wire)) {
        Serial.println("[RANGE] VL53L1X init failed.");
        s_distance_m = -1.0f;
        vTaskDelete(NULL);
        return;
    }

    // Long range mode, 50ms timing budget
    // Adafruit VL53L1X 3.x API: startRanging() accepts timing budget in ms
    // and optionally inter-measurement period in ms
    s_vl53.startRanging();
    s_vl53.setTimingBudget(50);

    Serial.println("[RANGE] VL53L1X ready (long range, 50ms budget).");

    while (true) {
        if (s_vl53.dataReady()) {
            int16_t dist_mm = s_vl53.distance();
            s_vl53.clearInterrupt();

            if (dist_mm > 0 && dist_mm < 4500) {
                // Convert mm to metres; clamp to sensor max range
                s_distance_m = dist_mm / 1000.0f;
            } else {
                // Out of range or error
                s_distance_m = -1.0f;
            }
        }
        // Poll at ~100ms to match spec requirement
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ── TF-Luna implementation ─────────────────────────────────────────────────
#elif defined(RANGEFINDER_TF_LUNA)

// TF-Luna I²C mode: address 0x10, 9-byte output packet
// Packet format (I²C read, 9 bytes):
//   [0]  0x59 (header)
//   [1]  0x59 (header)
//   [2]  dist low byte
//   [3]  dist high byte  → distance in cm
//   [4]  strength low
//   [5]  strength high   → signal strength (>100 = valid)
//   [6]  temp low
//   [7]  temp high
//   [8]  checksum

#include <Wire.h>

static const uint8_t TF_LUNA_ADDR = 0x10;

static bool tf_luna_read(uint16_t* dist_cm, uint16_t* strength) {
    Wire.requestFrom(TF_LUNA_ADDR, (uint8_t)9);
    if (Wire.available() < 9) return false;

    uint8_t buf[9];
    for (int i = 0; i < 9; i++) buf[i] = Wire.read();

    if (buf[0] != 0x59 || buf[1] != 0x59) return false;

    // Verify checksum (sum of bytes 0–7, low 8 bits)
    uint8_t csum = 0;
    for (int i = 0; i < 8; i++) csum += buf[i];
    if (csum != buf[8]) return false;

    *dist_cm  = (uint16_t)(buf[3] << 8) | buf[2];
    *strength = (uint16_t)(buf[5] << 8) | buf[4];
    return true;
}

static void range_task_fn(void* arg) {
    (void)arg;

    // Enable continuous ranging on TF-Luna (already default after power-on)
    // Set frame rate to 10Hz via I²C config register if needed
    // For simplicity, poll at 100ms — TF-Luna default is 100Hz, we read the latest
    Serial.println("[RANGE] TF-Luna ready (I2C mode, addr 0x10).");

    while (true) {
        uint16_t dist_cm = 0, strength = 0;
        if (tf_luna_read(&dist_cm, &strength)) {
            // Strength > 100 indicates a reliable return signal
            // TF-Luna max range 8m = 800cm
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
    // No rangefinder configured — range always reports -1.0
    // Task still created so the rest of the system doesn't need to check
    s_distance_m = -1.0f;
    vTaskDelete(NULL);
}

#endif  // rangefinder selection

// ── Public API (same for all implementations) ──────────────────────────────

void rangefinder_task_start() {
    xTaskCreatePinnedToCore(
        range_task_fn,
        "range_task",
        2048,
        NULL,
        3,      // Priority 3, same as imu_task
        NULL,
        1       // Core 1
    );
}

float rangefinder_get_distance() {
    return s_distance_m;   // Atomic 32-bit read on Xtensa
}