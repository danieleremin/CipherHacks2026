// firmware/esp32-sensor/src/main.cpp
// Wardriving sensor node — top level.
// No GPS. Scanning begins immediately on boot after peripheral init.
//
// State machine:
//   INIT      → peripheral init
//   SCANNING  → sub-states MODE_RADIUS / MODE_CONE (managed by wifi_scan)
//
// Task layout (FreeRTOS):
//   Core 0: WiFi driver (internal) + wifi_sniffer_cb
//   Core 1: imu_task (pri 3), range_task (pri 3),
//            espnow_tx_task (pri 6), loop / state machine (pri 1)

#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "espnow_tx.h"
#include "WiFi_Scan.h"
#include "imu.h"
#include "rangefinder.h"
#include "../../shared/packet_schema.h"

// ── LED helpers ────────────────────────────────────────────────────────────

static void led_set(bool on) {
    digitalWrite(PIN_STATUS_LED, on ? HIGH : LOW);
}

static void led_blink(int count, int on_ms, int off_ms) {
    for (int i = 0; i < count; i++) {
        led_set(true);  vTaskDelay(pdMS_TO_TICKS(on_ms));
        led_set(false); vTaskDelay(pdMS_TO_TICKS(off_ms));
    }
}

// ── setup() ───────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.printf("\n[BOOT] Wardriving sensor node, NODE_ID=0x%02X\n", NODE_ID);
    Serial.printf("[BOOT] Schema version: %d\n", SCHEMA_VERSION);
    Serial.printf("[BOOT] Built: %s %s\n", __DATE__, __TIME__);

    pinMode(PIN_STATUS_LED, OUTPUT);
    led_set(false);

    // ── I²C bus ────────────────────────────────────────────────────────────
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(I2C_FREQ_HZ);
    Serial.printf("[BOOT] I²C: SDA=%d SCL=%d @ %dHz\n",
                  PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ);

    // ── ESP-NOW TX task + queue ────────────────────────────────────────────
    QueueHandle_t record_queue = espnow_tx_init();
    Serial.println("[BOOT] ESP-NOW TX task started.");

    // ── WiFi + ESP-NOW init ────────────────────────────────────────────────
    wifi_scan_init(record_queue);
    Serial.println("[BOOT] WiFi + ESP-NOW initialized.");

    // ── IMU task ───────────────────────────────────────────────────────────
    imu_task_start();
    Serial.println("[BOOT] IMU task started (BNO085).");

    // ── Rangefinder task ───────────────────────────────────────────────────
    rangefinder_task_start();
#if defined(RANGEFINDER_VL53L1X)
    Serial.println("[BOOT] Rangefinder: VL53L1X.");
#elif defined(RANGEFINDER_TF_LUNA)
    Serial.println("[BOOT] Rangefinder: TF-Luna.");
#else
    Serial.println("[BOOT] No rangefinder configured.");
#endif

#ifdef DEBUG_SERIAL_ONLY
    Serial.println("[BOOT] *** DEBUG_SERIAL_ONLY — ESP-NOW TX disabled ***");
#endif

    // ── Start scanning immediately ─────────────────────────────────────────
    // Three quick blinks = ready
    led_blink(3, 80, 80);
    led_set(true);

    wifi_scan_start();
    Serial.println("[STATE] SCANNING — ch hop active, mode=RADIUS");
}

// ── loop() ────────────────────────────────────────────────────────────────

static uint32_t s_last_diag_ms = 0;
static const uint32_t DIAG_INTERVAL_MS = 5000;

void loop() {
    uint32_t now = millis();

    if ((now - s_last_diag_ms) >= DIAG_INTERVAL_MS) {
        s_last_diag_ms = now;

        float hdg = imu_get_heading();
        float rng = rangefinder_get_distance();

        Serial.printf(
            "[DIAG] uptime=%lus ch=%d | "
            "sent=%lu fail=%lu | "
            "IMU: hdg=%.1f° lock=%.1f° | "
            "range=%.2fm\n",
            now / 1000,
            wifi_scan_current_channel(),
            espnow_tx_sent_count(),
            espnow_tx_fail_count(),
            hdg,
            imu_get_lock_heading(),
            rng
        );

        // Brief LED dip = heartbeat
        led_set(false);
        vTaskDelay(pdMS_TO_TICKS(50));
        led_set(true);
    }

    vTaskDelay(pdMS_TO_TICKS(50));
}