// firmware/esp32-sensor/src/main.cpp
// Wardriving sensor node — top level.
//
// Compile-time behavior:
//   ANCHOR_MODE defined  → broadcasts a reference beacon (node 3)
//   ANCHOR_MODE not set  → WiFi scanner + ESP-NOW TX (nodes 1 and 2)
//
// Scanner task layout (FreeRTOS, Core 1):
//   imu_task (pri 3), range_task (pri 3), espnow_tx_task (pri 6)
// Scanner Core 0: WiFi driver + wifi_sniffer_cb

#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "packet_schema.h"

#ifdef ANCHOR_MODE
// ─────────────────────────────────────────────────────────────────────────────
// ANCHOR MODE — node 3
// ─────────────────────────────────────────────────────────────────────────────
#include "anchor.h"

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.printf("\n[BOOT] Wardriving ANCHOR node, NODE_ID=0x%02X\n", NODE_ID);
    Serial.printf("[BOOT] Schema version: %d\n", SCHEMA_VERSION);
    Serial.printf("[BOOT] Built: %s %s\n", __DATE__, __TIME__);

    pinMode(PIN_STATUS_LED, OUTPUT);

    // Slow double-blink pattern = anchor mode (distinct from scanner)
    for (int i = 0; i < 2; i++) {
        digitalWrite(PIN_STATUS_LED, HIGH); delay(400);
        digitalWrite(PIN_STATUS_LED, LOW);  delay(200);
    }

    anchor_init();

    // Solid LED = anchor broadcasting
    digitalWrite(PIN_STATUS_LED, HIGH);
}

void loop() {
    anchor_update();
    delay(100);
}

#else
// ─────────────────────────────────────────────────────────────────────────────
// SCANNER MODE — nodes 1 and 2
// ─────────────────────────────────────────────────────────────────────────────
#include "espnow_tx.h"
#include "wifi_scan.h"
#include "imu.h"
#include "rangefinder.h"
#include "rssi_avg.h"

// ── LED helpers ──────────────────────────────────────────────────────────────

static void led_set(bool on) {
    digitalWrite(PIN_STATUS_LED, on ? HIGH : LOW);
}

static void led_blink(int count, int on_ms, int off_ms) {
    for (int i = 0; i < count; i++) {
        led_set(true);  vTaskDelay(pdMS_TO_TICKS(on_ms));
        led_set(false); vTaskDelay(pdMS_TO_TICKS(off_ms));
    }
}

// ── setup() ──────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.printf("\n[BOOT] Wardriving scanner node, NODE_ID=0x%02X\n", NODE_ID);
    Serial.printf("[BOOT] Schema version: %d\n", SCHEMA_VERSION);
    Serial.printf("[BOOT] Built: %s %s\n", __DATE__, __TIME__);

    pinMode(PIN_STATUS_LED, OUTPUT);
    led_set(false);

    // ── I²C ──────────────────────────────────────────────────────────────────
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.setClock(I2C_FREQ_HZ);
    Serial.printf("[BOOT] I²C: SDA=%d SCL=%d @ %dHz\n",
                  PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ);

    // ── ESP-NOW TX task + queue ───────────────────────────────────────────────
    QueueHandle_t record_queue = espnow_tx_init();
    Serial.println("[BOOT] ESP-NOW TX task started.");

    // ── WiFi + ESP-NOW ────────────────────────────────────────────────────────
    wifi_scan_init(record_queue);
    Serial.println("[BOOT] WiFi + ESP-NOW initialized.");

    // ── IMU ───────────────────────────────────────────────────────────────────
    imu_task_start();
    Serial.println("[BOOT] IMU task started (BNO085 / serial fallback).");

    // ── Rangefinder ───────────────────────────────────────────────────────────
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

    // Three quick blinks = scanner ready
    led_blink(3, 80, 80);
    led_set(true);

    wifi_scan_start();
    Serial.printf("[STATE] SCANNING — node %d online, ch hop active\n", NODE_ID);
}

// ── loop() ───────────────────────────────────────────────────────────────────

static uint32_t s_last_diag_ms = 0;
static const uint32_t DIAG_INTERVAL_MS = 5000;

void loop() {
    uint32_t now = millis();

    if ((now - s_last_diag_ms) >= DIAG_INTERVAL_MS) {
        s_last_diag_ms = now;

        Serial.printf(
            "[DIAG] node=%d uptime=%lus ch=%d | "
            "sent=%lu fail=%lu | "
            "IMU: hdg=%.1f° lock=%.1f° | "
            "range=%.2fm | "
            "tracked BSSIDs=%d\n",
            NODE_ID,
            now / 1000,
            wifi_scan_current_channel(),
            espnow_tx_sent_count(),
            espnow_tx_fail_count(),
            imu_get_heading(),
            imu_get_lock_heading(),
            rangefinder_get_distance(),
            rssi_avg_entry_count()
        );

        // Brief LED dip = heartbeat
        led_set(false);
        vTaskDelay(pdMS_TO_TICKS(50));
        led_set(true);
    }

    vTaskDelay(pdMS_TO_TICKS(50));
}

#endif // ANCHOR_MODE