// firmware/esp32-sensor/src/anchor.cpp
// Anchor node + base receiver: AP beacon + ESP-NOW receive + Serial JSON output.

#include "anchor.h"

#ifdef ANCHOR_MODE

#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "config.h"
#include "packet_schema.h"

// ── Module state ───────────────────────────────────────────────────────────

static QueueHandle_t  s_rx_queue    = NULL;
static volatile uint32_t s_rx_count = 0;

// Sync-channel burst cycle timing
// Every SYNC_INTERVAL_MS, broadcast CMD_SYNC_CHANNEL to lock scanners
// onto ANCHOR_CHANNEL for BURST_WINDOW_MS, then release them.
static const uint32_t SYNC_INTERVAL_MS  = 3000;  // 3s between sync windows
static const uint32_t BURST_WINDOW_MS   = 800;   // 800ms dwell on anchor ch
static uint32_t       s_last_sync_ms    = 0;
static bool           s_in_burst        = false;
static uint32_t       s_burst_start_ms  = 0;

// Known scanner node MACs — registered as ESP-NOW peers.
// Filled from SCANNER_MAC_1 and SCANNER_MAC_2 in config.h.
static uint8_t s_scanner1_mac[] = SCANNER_MAC_1;
static uint8_t s_scanner2_mac[] = SCANNER_MAC_2;

// ── JSON serializer ────────────────────────────────────────────────────────
// Writes a WardrivingRecord as a compact JSON line to Serial.
// The R4 reads these lines and forwards them over WebSocket.
// All fields from packet_schema.h are included.

static void record_to_json(const WardrivingRecord* r) {
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             r->bssid[0], r->bssid[1], r->bssid[2],
             r->bssid[3], r->bssid[4], r->bssid[5]);

    // Escape SSID — replace double quotes with escaped version
    char ssid[66];
    int si = 0;
    for (int i = 0; r->ssid[i] && si < 63; i++) {
        if (r->ssid[i] == '"') { ssid[si++] = '\\'; }
        ssid[si++] = r->ssid[i];
    }
    ssid[si] = '\0';

    Serial.printf(
        "{\"node\":%d,\"schema\":%d,\"mode\":%d,\"uptime\":%lu,"
        "\"bssid\":\"%s\",\"ssid\":\"%s\","
        "\"rssi\":%d,\"ch\":%d,\"enc\":%d,"
        "\"bearing\":%.1f,\"range\":%.2f,\"in_cone\":%d,\"h_lock\":%.1f}\n",
        r->node_id,
        r->schema_version,
        r->mode,
        (unsigned long)r->uptime_ms,
        mac,
        ssid,
        r->rssi,
        r->channel,
        r->enc_type,
        r->bearing_deg,
        r->range_m,
        r->in_cone,
        r->h_lock_deg
    );
}

// ── ESP-NOW receive callback ───────────────────────────────────────────────
// Fires in WiFi driver task context — must be fast.
// Copies the record into the queue; JSON serialization happens in anchor_update().

static void espnow_recv_cb(const uint8_t* mac,
                            const uint8_t* data, int len) {
    if (len != sizeof(WardrivingRecord)) return;

    WardrivingRecord rec;
    memcpy(&rec, data, sizeof(WardrivingRecord));

    BaseType_t woken = pdFALSE;
    if (xQueueSendFromISR(s_rx_queue, &rec, &woken) == pdTRUE) {
        s_rx_count++;
    }
    portYIELD_FROM_ISR(woken);
}

// ── Sync-channel broadcast ────────────────────────────────────────────────
// Sends CMD_SYNC_CHANNEL + channel byte to both scanner nodes.
// With len=2 payload: lock to channel.
// With len=1 payload: resume autonomous hopping.

static void broadcast_sync(uint8_t channel) {
    uint8_t cmd[2] = { CMD_SYNC_CHANNEL, channel };

    esp_now_peer_info_t peer = {};
    peer.channel = 0;
    peer.encrypt = false;

    // Send to scanner 1
    memcpy(peer.peer_addr, s_scanner1_mac, 6);
    esp_now_add_peer(&peer);
    esp_now_send(s_scanner1_mac, cmd, 2);

    // Send to scanner 2
    memcpy(peer.peer_addr, s_scanner2_mac, 6);
    esp_now_add_peer(&peer);
    esp_now_send(s_scanner2_mac, cmd, 2);
}

static void broadcast_resume() {
    uint8_t cmd[1] = { CMD_SYNC_CHANNEL };  // len=1 = resume hopping

    esp_now_send(s_scanner1_mac, cmd, 1);
    esp_now_send(s_scanner2_mac, cmd, 1);
}

// ── Public API ─────────────────────────────────────────────────────────────

void anchor_init() {
    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // ── WiFi: AP+STA mode ─────────────────────────────────────────────────
    // AP mode: broadcasts WARDRIVE_ANCHOR beacon on ANCHOR_CHANNEL.
    // STA mode: required for ESP-NOW to function; no association needed.
    WiFi.mode(WIFI_AP_STA);

    // Start soft AP on the fixed anchor channel, open network, no clients
    WiFi.softAP(ANCHOR_SSID, nullptr, ANCHOR_CHANNEL, false, 0);

    // Lock the STA side to the same channel so ESP-NOW receive works
    esp_wifi_set_channel(ANCHOR_CHANNEL, WIFI_SECOND_CHAN_NONE);

    // Set TX power — low power keeps the reference signal predictable
    esp_wifi_set_max_tx_power(ANCHOR_TX_POWER);

    // ── ESP-NOW ───────────────────────────────────────────────────────────
    esp_now_init();
    esp_now_register_recv_cb(espnow_recv_cb);

    // Register scanner nodes as peers so we can send sync commands to them
    esp_now_peer_info_t peer = {};
    peer.channel = 0;
    peer.encrypt = false;

    memcpy(peer.peer_addr, s_scanner1_mac, 6);
    esp_now_add_peer(&peer);

    memcpy(peer.peer_addr, s_scanner2_mac, 6);
    esp_now_add_peer(&peer);

    // ── RX queue ──────────────────────────────────────────────────────────
    s_rx_queue = xQueueCreate(64, sizeof(WardrivingRecord));

    // ── Serial output (to R4) ─────────────────────────────────────────────
    // Serial is already initialized in main setup().
    // Baud rate must match R4's Serial1 baud rate (115200).

    // Delay to let AP fully initialize before reading MAC
    Serial.println("[ANCHOR] Initializing WiFi AP+STA mode...");
    delay(1000);
    uint8_t ap_mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, ap_mac);
    Serial.println("[ANCHOR] ──────────────────────────────");
    Serial.printf("[ANCHOR] SSID    : %s\n", ANCHOR_SSID);
    Serial.printf("[ANCHOR] BSSID   : %02X:%02X:%02X:%02X:%02X:%02X\n",
                  ap_mac[0], ap_mac[1], ap_mac[2], ap_mac[3], ap_mac[4], ap_mac[5]);
    Serial.printf ("[ANCHOR] Channel : %d\n", ANCHOR_CHANNEL);
    Serial.printf ("[ANCHOR] Mode    : AP+STA (anchor + base receiver)\n");
    Serial.println("[ANCHOR] Waiting for scanner records...");
    Serial.println("[ANCHOR] ──────────────────────────────");
}

void anchor_update() {
    uint32_t now = millis();

    // ── Drain RX queue → JSON → Serial ───────────────────────────────────
    WardrivingRecord rec;
    while (xQueueReceive(s_rx_queue, &rec, 0) == pdTRUE) {
        record_to_json(&rec);
    }

    // ── Sync-channel burst cycle ──────────────────────────────────────────
    if (!s_in_burst) {
        // Check if it's time to start a sync burst
        if ((now - s_last_sync_ms) >= SYNC_INTERVAL_MS) {
            s_last_sync_ms  = now;
            s_in_burst      = true;
            s_burst_start_ms = now;
            broadcast_sync(ANCHOR_CHANNEL);
        }
    } else {
        // Check if burst window has elapsed — release scanners
        if ((now - s_burst_start_ms) >= BURST_WINDOW_MS) {
            s_in_burst = false;
            broadcast_resume();
        }
    }
}

uint32_t anchor_records_received() {
    return s_rx_count;
}

#endif // ANCHOR_MODE