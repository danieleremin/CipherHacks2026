// firmware/esp32-sensor/src/anchor.h
// Anchor node interface — reference transmitter for multi-node RSSI positioning.
// Only compiled when ANCHOR_MODE is defined in build_flags.
//
// The anchor node:
//   - Broadcasts a WiFi soft AP beacon on a fixed channel (ANCHOR_CHANNEL)
//   - SSID: WARDRIVE_ANCHOR (configurable via ANCHOR_SSID)
//   - Accepts no client connections (max_connection = 0)
//   - Reports its own status periodically over Serial
//   - Listens for CMD_SYNC_CHANNEL from the R4 to switch channels if needed
//
// Scanner nodes detect WARDRIVE_ANCHOR in their normal beacon capture.
// Because the anchor's BSSID (its MAC) and TX power are known constants,
// the R4 can use differential RSSI between scanner nodes for that BSSID
// to estimate bearing without relying on unknown third-party APs.

#pragma once

#ifdef ANCHOR_MODE

// Initializes WiFi in AP mode and starts broadcasting.
// Call once from setup() instead of wifi_scan_init().
void anchor_init();

// Prints status (uptime, channel, TX power) to Serial.
// Call from loop() on a timer.
void anchor_update();

#endif // ANCHOR_MODE