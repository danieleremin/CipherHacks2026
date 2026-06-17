// firmware/esp32-sensor/src/imu.cpp
// BNO085 rotation vector → magnetic heading, cone lock management.
// See SPEC_ESP32_SENSOR_NODE.md Section 7.

#include "imu.h"
#include "config.h"

#include <Wire.h>
#include <Adafruit_BNO08x.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <math.h>

// ── Module-private state ───────────────────────────────────────────────────

static Adafruit_BNO08x   s_bno;
static sh2_SensorValue_t s_imu_val;

static volatile float    s_heading_deg = 0.0f;   // Current heading [0, 360)
static volatile float    s_h_lock_deg  = -1.0f;  // Cone lock heading, -1 = not set

static SemaphoreHandle_t s_imu_mutex = NULL;

// BNO085 I²C address: 0x4A (Adafruit breakout default)
static const uint8_t BNO085_I2C_ADDR = 0x4A;

// Rotation vector report interval in microseconds (10000 µs = 100Hz)
static const uint32_t ROTATION_VECTOR_INTERVAL_US = 10000;

// ── Quaternion to yaw helper ───────────────────────────────────────────────
// Extracts magnetic heading (yaw about the vertical axis) from a unit quaternion.
// Returns degrees in [0, 360).

static float quaternion_to_heading(float qw, float qx, float qy, float qz) {
    // Yaw (rotation around Z axis) from quaternion:
    // yaw = atan2(2*(qw*qz + qx*qy), 1 - 2*(qy*qy + qz*qz))
    float yaw_rad = atan2f(2.0f * (qw * qz + qx * qy),
                           1.0f - 2.0f * (qy * qy + qz * qz));

    // Convert radians to degrees and shift from [-180, 180] to [0, 360)
    float heading = yaw_rad * (180.0f / (float)M_PI);
    if (heading < 0.0f) {
        heading += 360.0f;
    }
    return heading;
}

// ── IMU FreeRTOS task ──────────────────────────────────────────────────────

static void imu_task_fn(void* arg) {
    (void)arg;

    // Wire must already be initialized by the time this task runs.
    // wifi_scan_init() does not use Wire; main.cpp calls Wire.begin() before
    // starting tasks.
    if (!s_bno.begin_I2C(BNO085_I2C_ADDR, &Wire)) {
        // BNO085 not found or failed to initialize.
        // Log to Serial and park — the system will operate without heading data.
        Serial.println("[IMU] BNO085 init failed. Heading will be unavailable.");
        vTaskDelete(NULL);
        return;
    }

    // Enable rotation vector report at 100Hz (most stable heading source)
    if (!s_bno.enableReport(SH2_ROTATION_VECTOR, ROTATION_VECTOR_INTERVAL_US)) {
        Serial.println("[IMU] Failed to enable rotation vector report.");
        vTaskDelete(NULL);
        return;
    }

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

        // 10ms tick — matches the sensor report interval
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ── Public API ─────────────────────────────────────────────────────────────

void imu_task_start() {
    s_imu_mutex = xSemaphoreCreateMutex();
    configASSERT(s_imu_mutex != NULL);

    // Core 1, priority 3 (below gps_task at 4)
    xTaskCreatePinnedToCore(
        imu_task_fn,
        "imu_task",
        2048,   // Stack bytes — BNO08x library is lightweight
        NULL,
        3,
        NULL,
        1       // Core 1
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