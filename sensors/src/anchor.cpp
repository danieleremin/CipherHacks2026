// firmware/esp32-sensor/src/anchor.cpp
// Anchor node: WiFi soft AP reference transmitter.
// Only compiled when ANCHOR_MODE is defined.

#include "anchor.h"

#ifdef ANCHOR_MODE

#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "packet_schema.h"

// ── ESP-NOW receive callback ───────────────────────────────────────────────
// Anchor listens for CMD_SYNC_CHANNEL from the R4 so the R4 can move
// the anchor to a different channel during correlation sweeps.

static void anchor_espnow_recv_cb(const uint8_t* mac,
                                   const uint8_t* data, int len) {
    (void)mac;
    if (len < 1) return;

    if (data[0] == CMD_SYNC_CHANNEL && len >= 2) {
        uint8_t ch = data[1];
        if (ch >= 1 && ch <= 13) {
            // Move the AP to the requested channel
            // Note: changing AP channel requires brief downtime (~50ms)
            WiFi.softAPdisconnect(false);
            WiFi.softAP(ANCHOR_SSID, nullptr, ch, false, 0);
            Serial.printf("[ANCHOR] Moved to ch%d on R4 request\n", ch);
        }
    }
}

// ── Public API ─────────────────────────────────────────────────────────────

void anchor_init() {
    // NVS init — required by WiFi stack
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Start WiFi in AP mode
    WiFi.mode(WIFI_AP);

    // Spoof MAC address for AP interface to match frontend compile-time constant
    uint8_t mac[] = {0x30, 0x76, 0xF5, 0x06, 0x28, 0xC5};
    esp_wifi_set_mac(WIFI_IF_AP, mac);

    // softAP(ssid, password, channel, hidden, max_connection)
    // password = nullptr → open network (no WPA overhead, just a beacon)
    // max_connection = 0 → accept no clients, just broadcast the beacon
    bool ok = WiFi.softAP(ANCHOR_SSID, nullptr, ANCHOR_CHANNEL, false, 0);
    if (!ok) {
        Serial.println("[ANCHOR] softAP failed — check config.");
        return;
    }

    // Set TX power (ESP-IDF unit: dBm × 4)
    // ANCHOR_TX_POWER=20 → 5 dBm, a low power that keeps range predictable
    esp_wifi_set_max_tx_power(ANCHOR_TX_POWER);

    // Initialize ESP-NOW so anchor can receive channel sync commands from R4
    esp_now_init();
    esp_now_register_recv_cb(anchor_espnow_recv_cb);

    // Register R4 as ESP-NOW peer
    esp_now_peer_info_t peer = {};
    const uint8_t base_mac[] = BASE_NODE_MAC;
    memcpy(peer.peer_addr, base_mac, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);

    Serial.println("[ANCHOR] ──────────────────────────────────");
    Serial.printf ("[ANCHOR] NODE_ID    : 0x%02X\n", NODE_ID);
    Serial.printf ("[ANCHOR] SSID       : %s\n", ANCHOR_SSID);
    Serial.printf ("[ANCHOR] BSSID      : %s\n",
                   WiFi.softAPmacAddress().c_str());
    Serial.printf ("[ANCHOR] Channel    : %d\n", ANCHOR_CHANNEL);
    Serial.printf ("[ANCHOR] TX power   : %d (units: dBm×4)\n", ANCHOR_TX_POWER);
    Serial.println("[ANCHOR] Broadcasting. Scanner nodes should detect this AP.");
    Serial.println("[ANCHOR] ──────────────────────────────────");
    Serial.println("[ANCHOR] Copy the BSSID above into your R4 firmware");
    Serial.println("[ANCHOR] as the known anchor MAC for RSSI correlation.");
}

void anchor_update() {
    static uint32_t last_print = 0;
    if (millis() - last_print < 10000) return;
    last_print = millis();

    Serial.printf("[ANCHOR] uptime=%lus clients=%d ch=%d\n",
                  millis() / 1000,
                  WiFi.softAPgetStationNum(),
                  ANCHOR_CHANNEL);
}

#endif // ANCHOR_MODE