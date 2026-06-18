// firmware/lcd-driver/src/record_processor.cpp

#include <Arduino.h>
#include <string.h>
#include "record_processor.h"
#include "config.h"

struct DedupEntry {
    uint8_t  bssid[6];
    uint32_t last_seen_ms;
};

static DedupEntry dedup_map[DEDUP_MAP_SIZE];
static uint8_t  dedup_count = 0;
static uint16_t total_unique_nets = 0;

bool dedup_check(const WardrivingRecord* r) {
    uint32_t now = millis();
    const uint32_t window_ms = (uint32_t)DEDUP_WINDOW_SEC * 1000UL;

    for (uint8_t i = 0; i < dedup_count; i++) {
        if (memcmp(dedup_map[i].bssid, r->bssid, 6) == 0) {
            bool expired = (now - dedup_map[i].last_seen_ms) >= window_ms;
            dedup_map[i].last_seen_ms = now;
            return expired;  // false = too recent, skip extended-log write
        }
    }

    // New BSSID
    if (dedup_count < DEDUP_MAP_SIZE) {
        memcpy(dedup_map[dedup_count].bssid, r->bssid, 6);
        dedup_map[dedup_count].last_seen_ms = now;
        dedup_count++;
    } else {
        // Map full — evict oldest entry (linear scan)
        uint8_t oldest_idx = 0;
        uint32_t oldest_time = dedup_map[0].last_seen_ms;
        for (uint8_t i = 1; i < DEDUP_MAP_SIZE; i++) {
            if (dedup_map[i].last_seen_ms < oldest_time) {
                oldest_time = dedup_map[i].last_seen_ms;
                oldest_idx = i;
            }
        }
        memcpy(dedup_map[oldest_idx].bssid, r->bssid, 6);
        dedup_map[oldest_idx].last_seen_ms = now;
    }

    total_unique_nets++;
    return true;
}

uint16_t dedup_unique_count() { return total_unique_nets; }
