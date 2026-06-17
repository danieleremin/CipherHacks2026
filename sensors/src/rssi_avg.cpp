// firmware/esp32-sensor/src/rssi_avg.cpp
// Rolling RSSI averager implementation.

#include "rssi_avg.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static RssiEntry      s_table[RSSI_AVG_TABLE_SIZE];
static SemaphoreHandle_t s_mutex = NULL;

void rssi_avg_init() {
    memset(s_table, 0, sizeof(s_table));
    s_mutex = xSemaphoreCreateMutex();
}

// Find existing entry for this BSSID, or return -1
static int find_entry(const uint8_t* bssid) {
    for (int i = 0; i < RSSI_AVG_TABLE_SIZE; i++) {
        if (s_table[i].valid &&
            memcmp(s_table[i].bssid, bssid, 6) == 0) {
            return i;
        }
    }
    return -1;
}

// Find a free slot, or evict the entry with the fewest samples
static int alloc_entry() {
    // First look for an unused slot
    for (int i = 0; i < RSSI_AVG_TABLE_SIZE; i++) {
        if (!s_table[i].valid) return i;
    }
    // Table full — evict entry with fewest samples (least confident)
    int min_idx = 0;
    uint8_t min_count = s_table[0].count;
    for (int i = 1; i < RSSI_AVG_TABLE_SIZE; i++) {
        if (s_table[i].count < min_count) {
            min_count = s_table[i].count;
            min_idx = i;
        }
    }
    return min_idx;
}

void rssi_avg_update(const uint8_t* bssid, int8_t rssi) {
    if (s_mutex == NULL) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    int idx = find_entry(bssid);
    if (idx < 0) {
        idx = alloc_entry();
        memset(&s_table[idx], 0, sizeof(RssiEntry));
        memcpy(s_table[idx].bssid, bssid, 6);
        s_table[idx].valid = true;
    }

    RssiEntry* e = &s_table[idx];
    e->samples[e->head] = (int16_t)rssi;
    e->head = (e->head + 1) % RSSI_AVG_WINDOW;
    if (e->count < RSSI_AVG_WINDOW) e->count++;

    xSemaphoreGive(s_mutex);
}

bool rssi_avg_get(const uint8_t* bssid, float* smoothed_out,
                  uint8_t* sample_count) {
    if (s_mutex == NULL) return false;
    xSemaphoreTake(s_mutex, portMAX_DELAY);

    int idx = find_entry(bssid);
    if (idx < 0) {
        xSemaphoreGive(s_mutex);
        return false;
    }

    RssiEntry* e = &s_table[idx];
    *sample_count = e->count;

    // Need at least half the window before reporting smoothed value
    if (e->count < RSSI_AVG_WINDOW / 2) {
        // Return raw last sample until we have enough data
        *smoothed_out = (float)e->samples[(e->head + RSSI_AVG_WINDOW - 1)
                                           % RSSI_AVG_WINDOW];
        xSemaphoreGive(s_mutex);
        return true;
    }

    // Simple mean over all accumulated samples
    int32_t sum = 0;
    for (uint8_t i = 0; i < e->count; i++) {
        sum += e->samples[i];
    }
    *smoothed_out = (float)sum / (float)e->count;

    xSemaphoreGive(s_mutex);
    return true;
}

uint8_t rssi_avg_entry_count() {
    uint8_t count = 0;
    for (int i = 0; i < RSSI_AVG_TABLE_SIZE; i++) {
        if (s_table[i].valid) count++;
    }
    return count;
}