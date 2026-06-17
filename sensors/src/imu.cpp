// firmware/esp32-sensor/src/imu.cpp
// BNO085 rotation vector → magnetic heading, cone lock management.
//
// When BNO085 is not connected, falls back to SERIAL_HEADING mode:
// type a heading in degrees into the serial monitor and press Enter
// to inject it. This lets you test cone mode logic without hardware.
// Commands:
//   123.4    → set heading to 123.4°
//   lock     → lock current heading as cone center
//   unlock   → clear cone lock

#include "imu.h"
#include "config.h"

#include <Wire.h>
#include <Adafruit_BNO08x.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

// ── Module-private state ───────────────────────────────────────────────────

static Adafruit_BNO08x   s_bno;
static sh2_SensorValue_t s_imu_val;

static volatile float    s_heading_deg = 0.0f;
static volatile float    s_h_lock_deg  = -1.0f;
static volatile bool     s_hw_present  = false;

static SemaphoreHandle_t s_imu_mutex = NULL;

static const uint8_t  BNO085_I2C_ADDR           = 0x4A;
static const uint32_t ROTATION_VECTOR_INTERVAL_US = 10000;

// ── Quaternion to heading ──────────────────────────────────────────────────

static float quaternion_to_heading(float qw, float qx, float qy, float qz) {
    float yaw_rad = atan2f(2.0f * (qw * qz + qx * qy),
                           1.0f - 2.0f * (qy * qy + qz * qz));
    float heading = yaw_rad * (180.0f / (float)M_PI);
    if (heading < 0.0f) heading += 360.0f;
    return heading;
}

// ── Serial heading injection (fallback when no IMU hardware) ───────────────
// Reads lines from Serial. A bare number sets the heading.
// "lock" locks the current heading. "unlock" clears it.

static void serial_heading_task_fn(void* arg) {
    (void)arg;

    char buf[32];
    uint8_t idx = 0;

    Serial.println("[IMU] No hardware — serial heading mode active.");
    Serial.println("[IMU] Type a heading (0-360) and press Enter to set.");
    Serial.println("[IMU] Type 'lock' to lock cone, 'unlock' to clear.");

    while (true) {
        while (Serial.available()) {
            char c = (char)Serial.read();

            if (c == '\n' || c == '\r') {
                if (idx == 0) continue;
                buf[idx] = '\0';
                idx = 0;

                // Strip trailing whitespace
                while (strlen(buf) > 0 && buf[strlen(buf)-1] == ' ')
                    buf[strlen(buf)-1] = '\0';

                if (strcmp(buf, "lock") == 0) {
                    imu_lock_heading();
                } else if (strcmp(buf, "unlock") == 0) {
                    imu_clear_lock();
                    Serial.println("[IMU] Cone lock cleared.");
                } else {
                    // Try parsing as a float heading
                    char* end;
                    float val = strtof(buf, &end);
                    if (end != buf && val >= 0.0f && val < 360.0f) {
                        xSemaphoreTake(s_imu_mutex, portMAX_DELAY);
                        s_heading_deg = val;
                        xSemaphoreGive(s_imu_mutex);
                        Serial.printf("[IMU] Heading set to %.1f°\n", val);
                    } else {
                        Serial.printf("[IMU] Unknown command: '%s'\n", buf);
                    }
                }
            } else if (idx < sizeof(buf) - 1) {
                buf[idx++] = c;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ── BNO085 hardware task ───────────────────────────────────────────────────

static void imu_hw_task_fn(void* arg) {
    (void)arg;

    if (!s_bno.begin_I2C(BNO085_I2C_ADDR, &Wire)) {
        Serial.println("[IMU] BNO085 not found — falling back to serial heading mode.");
        // Start serial injection task instead
        xTaskCreatePinnedToCore(
            serial_heading_task_fn,
            "imu_serial",
            2048,
            NULL,
            2,
            NULL,
            1
        );
        vTaskDelete(NULL);
        return;
    }

    if (!s_bno.enableReport(SH2_ROTATION_VECTOR, ROTATION_VECTOR_INTERVAL_US)) {
        Serial.println("[IMU] Failed to enable rotation vector.");
        vTaskDelete(NULL);
        return;
    }

    s_hw_present = true;
    Serial.println("[IMU] BNO085 ready. Wave in figure-8 to calibrate.");

    while (true) {
        if (s_bno.getSensorEvent(&s_imu_val)) {
            if (s_imu_val.sensorId == SH2_ROTATION_VECTOR) {
                float qw = s_imu_val.un.rotationVector.real;
                float qx = s_imu_val.un.rotationVector.i;
                float qy = s_imu_val.un.rotationVector.j;
                float qz = s_imu_val.un.rotationVector.k;

                float new_heading = quaternion_to_heading(qw, qx, qy, qz);

                xSemaphoreTake(s_imu_mutex, portMAX_DELAY);
                s_heading_deg = new_heading;
                xSemaphoreGive(s_imu_mutex);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ── Public API ─────────────────────────────────────────────────────────────

void imu_task_start() {
    s_imu_mutex = xSemaphoreCreateMutex();
    configASSERT(s_imu_mutex != NULL);

    xTaskCreatePinnedToCore(
        imu_hw_task_fn,
        "imu_task",
        3072,
        NULL,
        3,
        NULL,
        1
    );
}

float imu_get_heading() {
    float h = 0.0f;
    xSemaphoreTake(s_imu_mutex, portMAX_DELAY);
    h = s_heading_deg;
    xSemaphoreGive(s_imu_mutex);
    return h;
}

float imu_get_lock_heading() {
    float h = -1.0f;
    xSemaphoreTake(s_imu_mutex, portMAX_DELAY);
    h = s_h_lock_deg;
    xSemaphoreGive(s_imu_mutex);
    return h;
}

void imu_lock_heading() {
    xSemaphoreTake(s_imu_mutex, portMAX_DELAY);
    s_h_lock_deg = s_heading_deg;
    xSemaphoreGive(s_imu_mutex);
    Serial.printf("[IMU] Cone locked to %.1f°\n", s_h_lock_deg);
}

void imu_clear_lock() {
    xSemaphoreTake(s_imu_mutex, portMAX_DELAY);
    s_h_lock_deg = -1.0f;
    xSemaphoreGive(s_imu_mutex);
}