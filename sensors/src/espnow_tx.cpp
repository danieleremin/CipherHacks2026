// firmware/esp32-sensor/src/espnow_tx.cpp
// Drains the WardrivingRecord queue and transmits via ESP-NOW.
// When DEBUG_SERIAL_ONLY is defined in config.h, prints to Serial instead.
// See SPEC_ESP32_SENSOR_NODE.md Sections 9 and 10.

#include "espnow_tx.h"
#include "config.h"
#include "../../shared/packet_schema.h"

#include <Arduino.h>
#include "esp_now.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// ── Module-private state ───────────────────────────────────────────────────

static QueueHandle_t s_record_queue = NULL;

static volatile uint32_t s_sent_count    = 0;
static volatile uint32_t s_dropped_count = 0;
static volatile uint32_t s_fail_count    = 0;

// Base node MAC (peer already registered in wifi_scan_init)
static const uint8_t BASE_MAC[] = BASE_NODE_MAC;

// ── Debug serial print ─────────────────────────────────────────────────────

#ifdef DEBUG_SERIAL_ONLY
static void debug_print_record(const WardrivingRecord* r) {
    Serial.printf(
        "[NODE %02X] "
        "BSSID=%02X:%02X:%02X:%02X:%02X:%02X "
        "SSID='%s' "
        "RSSI=%d ch=%d enc=%d "
        "lat=%.6f lon=%.6f alt=%.1f "
        "hdop=%.1f sats=%d "
        "bearing=%.1f range=%.2f in_cone=%d "
        "t=%lu.%03u\n",
        r->node_id,
        r->bssid[0], r->bssid[1], r->bssid[2],
        r->bssid[3], r->bssid[4], r->bssid[5],
        r->ssid,
        r->rssi, r->channel, r->enc_type,
        r->latitude, r->longitude, r->altitude_m,
        (float)r->hdop_x10 / 10.0f, r->satellites,
        r->bearing_deg, r->range_m, r->in_cone,
        r->timestamp_unix, r->timestamp_ms
    );
}
#endif

// ── TX FreeRTOS task ───────────────────────────────────────────────────────

static void espnow_tx_task_fn(void* arg) {
    (void)arg;
    WardrivingRecord rec;

    while (true) {
        // Block until a record arrives (or 100ms timeout for watchdog-style check)
        if (xQueueReceive(s_record_queue, &rec, pdMS_TO_TICKS(100)) == pdTRUE) {

#ifdef DEBUG_SERIAL_ONLY
            debug_print_record(&rec);
            s_sent_count++;
#else
            esp_err_t err = esp_now_send(
                BASE_MAC,
                (const uint8_t*)&rec,
                sizeof(WardrivingRecord)
            );

            if (err == ESP_OK) {
                s_sent_count++;
            } else {
                s_fail_count++;
                // esp_now_send errors:
                //   ESP_ERR_ESPNOW_NOT_INIT   — wifi_scan_init() not called yet
                //   ESP_ERR_ESPNOW_ARG        — invalid args
                //   ESP_ERR_ESPNOW_INTERNAL   — internal error
                //   ESP_ERR_ESPNOW_NO_MEM     — out of memory
                //   ESP_ERR_ESPNOW_NOT_FOUND  — peer not registered
                //   ESP_ERR_ESPNOW_IF         — WiFi interface error
                Serial.printf("[TX] esp_now_send error: 0x%x\n", err);
            }
#endif
        }
        // No yield needed — xQueueReceive already yields when blocking
    }
}

// ── Public API ─────────────────────────────────────────────────────────────

QueueHandle_t espnow_tx_init() {
    // Create queue of WardrivingRecord structs
    // Each item: sizeof(WardrivingRecord) ≈ 87 bytes
    // 32 items × 87 bytes ≈ 2.8KB of queue RAM
    s_record_queue = xQueueCreate(RECORD_QUEUE_DEPTH, sizeof(WardrivingRecord));
    configASSERT(s_record_queue != NULL);

    // Priority 6 — highest among sensor tasks so records drain promptly
    // Core 1 — keeps WiFi driver (Core 0) free
    xTaskCreatePinnedToCore(
        espnow_tx_task_fn,
        "espnow_tx",
        4096,
        NULL,
        6,
        NULL,
        1
    );

    return s_record_queue;
}

uint32_t espnow_tx_sent_count()    { return s_sent_count; }
uint32_t espnow_tx_dropped_count() { return s_dropped_count; }
uint32_t espnow_tx_fail_count()    { return s_fail_count; }