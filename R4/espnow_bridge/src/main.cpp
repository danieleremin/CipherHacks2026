// firmware/espnow-bridge/src/main.cpp
// Routes between two legs:
//   ESP-NOW radio  <-> scanning nodes
//   Serial2 (R4 leg)  <- CMD bytes from the R4's USB-serial console
//                     -> RECORD frames forwarded from ESP-NOW
//
// See ../../shared/uart_bridge_protocol.h for the full topology rationale.

#include <Arduino.h>
#include <string.h>

#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "config.h"
#include "../../shared/packet_schema.h"
#include "../../shared/uart_bridge_protocol.h"

static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

#define R4_LEG  Serial2

static QueueHandle_t s_record_queue = NULL;  // ESP-NOW recv -> R4 leg tx
static volatile uint32_t s_rx_from_air   = 0;
static volatile uint32_t s_rx_dropped    = 0;
static volatile uint32_t s_tx_to_air     = 0;

static void led_pulse() {
    digitalWrite(PIN_STATUS_LED, HIGH);
    delay(2);
    digitalWrite(PIN_STATUS_LED, LOW);
}

// ── ESP-NOW callbacks (run in WiFi driver task — keep minimal) ─────────────

static void espnow_recv_cb(const uint8_t* mac, const uint8_t* data, int len) {
    (void)mac;
    if (len != sizeof(WardrivingRecord)) return;

    WardrivingRecord rec;
    memcpy(&rec, data, sizeof(rec));

    BaseType_t woken = pdFALSE;
    if (xQueueSendFromISR(s_record_queue, &rec, &woken) != pdTRUE) {
        s_rx_dropped++;
    } else {
        s_rx_from_air++;
    }
    portYIELD_FROM_ISR(woken);
}

static void espnow_send_cb(const uint8_t* mac, esp_now_send_status_t status) {
    (void)mac;
    (void)status;
}

// ── Setup helpers ───────────────────────────────────────────────────────────

static void espnow_setup() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

    esp_now_init();
    esp_now_register_send_cb(espnow_send_cb);
    esp_now_register_recv_cb(espnow_recv_cb);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, BROADCAST_MAC, 6);
    peer.channel = ESPNOW_CHANNEL;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
}

// ── UART framing: write ─────────────────────────────────────────────────────

static void uart_write_frame(uint8_t type, const uint8_t* payload, uint8_t len) {
    uint8_t checksum = bridge_checksum(type, len, payload);
    R4_LEG.write(BRIDGE_SYNC_0);
    R4_LEG.write(BRIDGE_SYNC_1);
    R4_LEG.write(type);
    R4_LEG.write(len);
    if (len > 0) R4_LEG.write(payload, len);
    R4_LEG.write(checksum);
}

// ── UART framing: read (CMD bytes arriving from the R4) ────────────────────

enum UartRxState { WAIT_SYNC0, WAIT_SYNC1, WAIT_TYPE, WAIT_LEN, WAIT_PAYLOAD, WAIT_CHECKSUM };

static UartRxState s_rx_state = WAIT_SYNC0;
static uint8_t s_rx_type = 0;
static uint8_t s_rx_len = 0;
static uint8_t s_rx_payload[sizeof(WardrivingRecord)];
static uint8_t s_rx_idx = 0;

static void handle_complete_frame_from_r4(uint8_t type, const uint8_t* payload, uint8_t len) {
    if (type != BRIDGE_PKT_CMD || len != 1) return;

    esp_now_send(BROADCAST_MAC, payload, 1);
    s_tx_to_air++;
    led_pulse();
}

static void uart_poll_r4_rx() {
    while (R4_LEG.available()) {
        uint8_t b = (uint8_t)R4_LEG.read();

        switch (s_rx_state) {
            case WAIT_SYNC0:
                if (b == BRIDGE_SYNC_0) s_rx_state = WAIT_SYNC1;
                break;
            case WAIT_SYNC1:
                s_rx_state = (b == BRIDGE_SYNC_1) ? WAIT_TYPE : WAIT_SYNC0;
                break;
            case WAIT_TYPE:
                s_rx_type = b;
                s_rx_state = WAIT_LEN;
                break;
            case WAIT_LEN:
                s_rx_len = b;
                s_rx_idx = 0;
                if (s_rx_len == 0) {
                    s_rx_state = WAIT_CHECKSUM;
                } else if (s_rx_len > sizeof(s_rx_payload)) {
                    s_rx_state = WAIT_SYNC0;  // bogus length — resync
                } else {
                    s_rx_state = WAIT_PAYLOAD;
                }
                break;
            case WAIT_PAYLOAD:
                s_rx_payload[s_rx_idx++] = b;
                if (s_rx_idx >= s_rx_len) s_rx_state = WAIT_CHECKSUM;
                break;
            case WAIT_CHECKSUM: {
                uint8_t expected = bridge_checksum(s_rx_type, s_rx_len, s_rx_payload);
                if (b == expected) {
                    handle_complete_frame_from_r4(s_rx_type, s_rx_payload, s_rx_len);
                }
                s_rx_state = WAIT_SYNC0;
                break;
            }
        }
    }
}

// ── Arduino entry points ────────────────────────────────────────────────────

void setup() {
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, LOW);

    Serial.begin(115200);
    delay(200);
    Serial.println("[BOOT] ESP-NOW bridge starting (R4 leg only)");

    R4_LEG.begin(BRIDGE_UART_BAUD, SERIAL_8N1, PIN_R4_UART_RX, PIN_R4_UART_TX);

    s_record_queue = xQueueCreate(RECORD_QUEUE_DEPTH, sizeof(WardrivingRecord));

    espnow_setup();
    Serial.printf("[BOOT] ESP-NOW ready on channel %d\n", ESPNOW_CHANNEL);
}

void loop() {
    WardrivingRecord rec;
    while (xQueueReceive(s_record_queue, &rec, 0) == pdTRUE) {
        uart_write_frame(BRIDGE_PKT_RECORD, (const uint8_t*)&rec, sizeof(rec));
        led_pulse();
    }

    uart_poll_r4_rx();

    static uint32_t last_diag = 0;
    if (millis() - last_diag > 5000) {
        last_diag = millis();
        Serial.printf("[DIAG] air_rx=%lu dropped=%lu air_tx=%lu\n",
                      s_rx_from_air, s_rx_dropped, s_tx_to_air);
    }
}
