// firmware/lcd-driver/src/main.cpp
// LCD/SD driver — top level. Receives records and mirrored mode commands
// from the ESP-NOW bridge, then enriches (OUI), dedups, logs (SD), and
// displays (TFT) each one. Owns no command authority of its own — mode
// state here always mirrors whatever the R4's USB console last sent.

#include <Arduino.h>
#include <string.h>
#include "config.h"
#include "../../shared/packet_schema.h"
#include "espnow_rx.h"
#include "sd_logger.h"
#include "oui_lookup.h"
#include "record_processor.h"
#include "lcd_display.h"

static WardrivingRecord last_record;
static bool has_last_record = false;
static char last_mfr[24] = "---";

// Tiny "which sensor nodes are alive" tracker, sized for a handful of nodes.
#define ACTIVE_NODE_SLOTS 8
#define ACTIVE_NODE_TIMEOUT_MS 60000UL

static uint8_t  active_node_ids[ACTIVE_NODE_SLOTS];
static uint32_t active_node_seen[ACTIVE_NODE_SLOTS];
static uint8_t  active_node_count = 0;

static void note_node_seen(uint8_t node_id) {
    uint32_t now = millis();
    for (uint8_t i = 0; i < active_node_count; i++) {
        if (active_node_ids[i] == node_id) {
            active_node_seen[i] = now;
            return;
        }
    }
    if (active_node_count < ACTIVE_NODE_SLOTS) {
        active_node_ids[active_node_count] = node_id;
        active_node_seen[active_node_count] = now;
        active_node_count++;
    }
}

static uint8_t count_active_nodes() {
    uint32_t now = millis();
    uint8_t count = 0;
    for (uint8_t i = 0; i < active_node_count; i++) {
        if (now - active_node_seen[i] < ACTIVE_NODE_TIMEOUT_MS) count++;
    }
    return count;
}

void setup() {
    Serial.begin(115200);

    if (!sd_logger_init()) {
        Serial.println("SD init failed");
        while (true) { delay(200); }
    }

    if (!oui_lookup_init()) {
        Serial.println("OUI table not found on SD - manufacturer lookup disabled");
    }

    lcd_init();
    espnow_rx_init();

    Serial.println("LCD/SD driver ready.");
}

void loop() {
    espnow_rx_poll();

    WardrivingRecord rec;
    while (espnow_rx_dequeue(&rec)) {
        char mfr[24];
        oui_lookup(rec.bssid, mfr);

        sd_logger_write_wigle(&rec);
        if (dedup_check(&rec)) {
            sd_logger_write_extended(&rec, mfr);
        }

        note_node_seen(rec.node_id);

        memcpy(&last_record, &rec, sizeof(rec));
        strncpy(last_mfr, mfr, 23);
        last_mfr[23] = '\0';
        has_last_record = true;
    }

    static uint32_t last_lcd = 0;
    if (millis() - last_lcd > 250) {
        last_lcd = millis();
        lcd_update(
            espnow_rx_mode(),
            espnow_rx_cone_locked(),
            dedup_unique_count(),
            count_active_nodes(),
            has_last_record ? &last_record : nullptr,
            last_mfr
        );
    }
}
