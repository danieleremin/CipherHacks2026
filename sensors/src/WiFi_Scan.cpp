// firmware/esp32-sensor/src/wifi_scan.cpp
// WiFi promiscuous capture, 802.11 IE parsing, channel hopping, ESP-NOW setup.

#include "WiFi_Scan.h"
#include "config.h"
#include "imu.h"
#include "rangefinder.h"
#include "../../shared/packet_schema.h"

#include <string.h>
#include <math.h>
#include <Arduino.h>

#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "freertos/timers.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// ── Module-private state ───────────────────────────────────────────────────

static QueueHandle_t s_record_queue  = NULL;
static volatile uint8_t s_current_mode = MODE_RADIUS;
static volatile uint8_t s_ch_idx      = 0;
static TimerHandle_t    s_hop_timer   = NULL;

static const uint8_t CHANNELS[]   = {1,2,3,4,5,6,7,8,9,10,11,12,13};
static const uint8_t NUM_CHANNELS = sizeof(CHANNELS);

static volatile uint32_t s_dropped   = 0;
static volatile uint32_t s_send_fail = 0;

// ── Forward declarations ───────────────────────────────────────────────────

static void channel_hop_timer_cb(TimerHandle_t xTimer);
static void wifi_sniffer_cb(void* buf, wifi_promiscuous_pkt_type_t type);
static void parse_beacon_ies(const uint8_t* ies, int len,
                              WardrivingRecord* rec, bool cap_privacy);
static void populate_sensor_fields(WardrivingRecord* rec);
static void espnow_send_cb(const uint8_t* mac, esp_now_send_status_t status);
static void espnow_recv_cb(const uint8_t* mac,
                           const uint8_t* data, int len);

// ── Public API ─────────────────────────────────────────────────────────────

void wifi_scan_init(QueueHandle_t queue) {
    s_record_queue = queue;

    // 1. NVS — required by WiFi stack
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // 2. Network interface + event loop
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    // 3. WiFi — STA mode required for ESP-NOW
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    esp_wifi_set_channel(CHANNELS[0], WIFI_SECOND_CHAN_NONE);

    // 4. ESP-NOW — must be after esp_wifi_start()
    esp_now_init();
    esp_now_register_send_cb(espnow_send_cb);
    esp_now_register_recv_cb(espnow_recv_cb);

    // 5. Register base node as ESP-NOW peer
    esp_now_peer_info_t peer = {};
    const uint8_t base_mac[] = BASE_NODE_MAC;
    memcpy(peer.peer_addr, base_mac, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);

    // 6. Channel-hop timer (starts when wifi_scan_start() is called)
    s_hop_timer = xTimerCreate(
        "ch_hop",
        pdMS_TO_TICKS(CHANNEL_DWELL_MS),
        pdTRUE,
        NULL,
        channel_hop_timer_cb
    );
}

void wifi_scan_start() {
    // Management frames only — keeps queue free of data frame noise
    wifi_promiscuous_filter_t filt = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT
    };
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous_rx_cb(wifi_sniffer_cb);
    esp_wifi_set_promiscuous(true);

    if (s_hop_timer != NULL) {
        xTimerStart(s_hop_timer, 0);
    }
}

void wifi_scan_stop() {
    if (s_hop_timer != NULL) xTimerStop(s_hop_timer, 0);
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
}

uint8_t wifi_scan_current_channel() {
    return CHANNELS[s_ch_idx];
}

// ── Channel hop timer ──────────────────────────────────────────────────────

static void channel_hop_timer_cb(TimerHandle_t xTimer) {
    (void)xTimer;
    s_ch_idx = (s_ch_idx + 1) % NUM_CHANNELS;
    esp_wifi_set_channel(CHANNELS[s_ch_idx], WIFI_SECOND_CHAN_NONE);
}

// ── WiFi sniffer callback ──────────────────────────────────────────────────
// Runs in the WiFi driver task on Core 0. Must return quickly — no blocking.

static void wifi_sniffer_cb(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;

    const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
    const uint8_t* frame = pkt->payload;
    const int frame_len  = pkt->rx_ctrl.sig_len;

    // Subtype from frame control byte 0 (bits [7:4])
    uint8_t subtype = (frame[0] >> 4) & 0x0F;

    // Beacon = 0x08, Probe Response = 0x05
    if (subtype != 0x08 && subtype != 0x05) return;

    // 24B MAC header + 8B timestamp + 2B interval + 2B capability + 2B min IE = 38B
    if (frame_len < 38) return;

    if (pkt->rx_ctrl.rssi < RSSI_FLOOR_DBM) return;

    WardrivingRecord rec = {};
    rec.node_id        = (uint8_t)NODE_ID;
    rec.schema_version = SCHEMA_VERSION;
    rec.rssi           = (int8_t)pkt->rx_ctrl.rssi;
    rec.channel        = pkt->rx_ctrl.channel;
    rec.mode           = s_current_mode;
    rec.uptime_ms      = (uint32_t)esp_timer_get_time() / 1000ULL;

    // BSSID: bytes 16–21 of the 802.11 MAC header (Address 3 in beacons)
    memcpy(rec.bssid, frame + 16, 6);

    // Capability Information: body bytes 10–11 → frame offset 34–35
    uint16_t cap = (uint16_t)(frame[35] << 8) | frame[34];
    bool cap_privacy = (cap >> 4) & 0x01;

    // Tagged parameters begin at frame offset 36 (body offset 12)
    const uint8_t* ies     = frame + 36;
    const int      ies_len = frame_len - 36;
    if (ies_len > 0) {
        parse_beacon_ies(ies, ies_len, &rec, cap_privacy);
    }

    populate_sensor_fields(&rec);

    BaseType_t woken = pdFALSE;
    if (xQueueSendFromISR(s_record_queue, &rec, &woken) != pdTRUE) {
        s_dropped++;
    }
    portYIELD_FROM_ISR(woken);
}

// ── 802.11 Information Element parser ─────────────────────────────────────

static void parse_beacon_ies(const uint8_t* ies, int len,
                              WardrivingRecord* rec, bool cap_privacy) {
    bool has_rsn = false;
    bool has_wpa = false;
    int  i = 0;

    while (i < len - 1) {
        uint8_t tag_id  = ies[i];
        uint8_t tag_len = ies[i + 1];
        if (i + 2 + tag_len > len) break;

        const uint8_t* tag_data = ies + i + 2;

        switch (tag_id) {
            case 0x00: {  // SSID
                if (tag_len > 0 && tag_len <= 32) {
                    memcpy(rec->ssid, tag_data, tag_len);
                    rec->ssid[tag_len] = '\0';
                }
                // tag_len == 0 → hidden SSID, ssid stays as empty string
                break;
            }
            case 0x03: {  // DS Parameter Set — authoritative channel
                if (tag_len >= 1) rec->channel = tag_data[0];
                break;
            }
            case 0x30: {  // RSN IE — WPA2 or WPA3
                has_rsn = true;
                rec->enc_type = ENC_WPA2;

                // Walk AKM suites to detect WPA3 (SAE, AKM type 8: OUI 00-0F-AC)
                if (tag_len >= 8) {
                    uint16_t pw_count  = (uint16_t)(tag_data[7] << 8) | tag_data[6];
                    int akm_off = 8 + (pw_count * 4);
                    if (akm_off + 2 <= tag_len) {
                        uint16_t akm_count = (uint16_t)(tag_data[akm_off + 1] << 8)
                                           |  tag_data[akm_off];
                        akm_off += 2;
                        for (uint16_t k = 0; k < akm_count; k++) {
                            if (akm_off + 4 > tag_len) break;
                            if (tag_data[akm_off]     == 0x00 &&
                                tag_data[akm_off + 1] == 0x0F &&
                                tag_data[akm_off + 2] == 0xAC &&
                                tag_data[akm_off + 3] == 0x08) {
                                rec->enc_type = ENC_WPA3;
                            }
                            akm_off += 4;
                        }
                    }
                }
                break;
            }
            case 0xDD: {  // Vendor Specific — WPA OUI 00:50:F2:01
                if (tag_len >= 4 &&
                    tag_data[0] == 0x00 && tag_data[1] == 0x50 &&
                    tag_data[2] == 0xF2 && tag_data[3] == 0x01) {
                    has_wpa = true;
                }
                break;
            }
            default: break;
        }

        i += 2 + tag_len;
    }

    // Resolve encryption type
    if (has_rsn) {
        if (has_wpa) rec->enc_type = ENC_WPA2;  // Mixed/transition — conservative
        // else enc_type already set to WPA2 or WPA3 above
    } else if (has_wpa) {
        rec->enc_type = ENC_WPA;
    } else if (cap_privacy) {
        rec->enc_type = ENC_WEP;
    } else {
        rec->enc_type = ENC_OPEN;
    }
}

// ── Sensor field population ────────────────────────────────────────────────
// Reads from IMU and rangefinder cross-task globals.

static void populate_sensor_fields(WardrivingRecord* rec) {
    float heading = imu_get_heading();
    float h_lock  = imu_get_lock_heading();

    if (rec->mode == MODE_CONE) {
        rec->bearing_deg = heading;
        rec->h_lock_deg  = h_lock;
        rec->range_m     = rangefinder_get_distance();

        if (h_lock >= 0.0f) {
            float delta = fmodf(fabsf(heading - h_lock), 360.0f);
            if (delta > 180.0f) delta = 360.0f - delta;
            rec->in_cone = (delta <= CONE_HALF_ANGLE_DEG) ? 1 : 0;
        } else {
            rec->in_cone = 0;  // Not yet locked
        }
    } else {
        // Radius mode — cone fields unused
        rec->bearing_deg = -1.0f;
        rec->range_m     = -1.0f;
        rec->h_lock_deg  = -1.0f;
        rec->in_cone     = 1;
    }
}

// ── ESP-NOW callbacks ──────────────────────────────────────────────────────

static void espnow_send_cb(const uint8_t* mac, esp_now_send_status_t status) {
    (void)mac;
    if (status != ESP_NOW_SEND_SUCCESS) s_send_fail++;
}

static void espnow_recv_cb(const uint8_t* mac,
                            const uint8_t* data, int len) {
    (void)mac;
    if (len < 1) return;

    switch (data[0]) {
        case CMD_SET_RADIUS:
            s_current_mode = MODE_RADIUS;
            imu_clear_lock();
            break;
        case CMD_SET_CONE:
            s_current_mode = MODE_CONE;
            break;
        case CMD_CONE_LOCK:
            imu_lock_heading();
            break;
        default: break;
    }
}