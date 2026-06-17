// firmware/esp32-sensor/src/rssi_avg.h
// Rolling RSSI averager — smooths instantaneous readings across N beacon
// observations per BSSID. Reduces effective noise floor from ±5 dBm
// (single sample) to ±1 dBm (25 samples), allowing meaningful differential
// RSSI comparison between nodes spaced as close as 15–20cm apart.

#pragma once
#include <stdint.h>
#include <stdbool.h>

// Number of BSSIDs tracked simultaneously.
// Each entry uses 14 bytes of RAM. 32 entries = 448 bytes.
#define RSSI_AVG_TABLE_SIZE  32

// Number of samples in the rolling window per BSSID.
// Higher = smoother but slower to react to movement.
// 20 samples at ~100ms beacon interval = 2 second smoothing window.
#define RSSI_AVG_WINDOW  20

typedef struct {
    uint8_t  bssid[6];
    int16_t  samples[RSSI_AVG_WINDOW];  // Ring buffer of raw RSSI values
    uint8_t  head;                       // Next write position
    uint8_t  count;                      // Samples accumulated (up to WINDOW)
    bool     valid;                      // True once at least 1 sample exists
} RssiEntry;

// Initialize the table. Call once from setup().
void rssi_avg_init();

// Feed a new raw RSSI observation for a BSSID.
// Evicts the oldest entry if the table is full.
void rssi_avg_update(const uint8_t* bssid, int8_t rssi);

// Returns the smoothed RSSI for a BSSID, or raw_rssi if not enough
// samples have accumulated yet (count < RSSI_AVG_WINDOW / 2).
// smoothed_out: averaged value written here
// sample_count: how many samples contributed (for confidence weighting)
bool rssi_avg_get(const uint8_t* bssid, float* smoothed_out,
                  uint8_t* sample_count);

// Returns the number of unique BSSIDs currently tracked.
uint8_t rssi_avg_entry_count();