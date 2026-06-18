// firmware/esp32-sensor/src/anchor.h
// Anchor node + base receiver.
// Runs in WiFi AP+STA mode simultaneously:
//   AP  — broadcasts WARDRIVE_ANCHOR beacon on ANCHOR_CHANNEL
//   STA — not associated to any network, but ESP-NOW receive works
//          on the AP channel
//
// Receives WardrivingRecord structs from scanner nodes via ESP-NOW.
// Serializes each record as a JSON line and writes to Serial (USB)
// for the Arduino R4 to relay over WebSocket.
//
// Periodically broadcasts CMD_SYNC_CHANNEL to scanner nodes, telling
// them to dwell on ANCHOR_CHANNEL for a burst transmission window,
// then releases them back to autonomous hopping.

#pragma once
#include <stdint.h>

#ifdef ANCHOR_MODE

// Initialize WiFi AP+STA mode, ESP-NOW receive, and Serial JSON output.
// Call once from setup() instead of wifi_scan_init().
void anchor_init();

// Drive the sync-channel burst cycle and print diagnostics.
// Call from loop() every iteration.
void anchor_update();

// Returns total records received since boot.
uint32_t anchor_records_received();

#endif // ANCHOR_MODE
