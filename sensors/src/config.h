// firmware/esp32-sensor/src/config.h
// Compile-time configuration — ESP32 DEVKIT V1
//
// NODE_ID, ANCHOR_MODE, ANCHOR_CHANNEL, ANCHOR_SSID, ANCHOR_TX_POWER
// are all injected via platformio.ini build_flags. Do not hardcode them here.

#pragma once

// ── Node identity ──────────────────────────────────────────────────────────
#ifndef NODE_ID
  #define NODE_ID  0x01
#endif

// ── ESP-NOW peers ─────────────────────────────────────────────────────────
// BASE_NODE_MAC: MAC of the anchor/base node (node 3, the USB-serial bridge).
// Read from node 3 serial output after flashing — printed as [ANCHOR] BSSID.
#define BASE_NODE_MAC  { 0x30, 0x76, 0xF5, 0x06, 0x28, 0xC5 }
// Scanner MACs: used by node 3 to send CMD_SYNC_CHANNEL to scanners.
// Read from node 1 and node 2 serial output (printed at boot as MAC: XX:XX:XX...)
// or from the esptool output during flashing (MAC: line).
#define SCANNER_MAC_1  { 0xE0, 0x8C, 0xFE, 0xE6, 0x49, 0xD0 }
#define SCANNER_MAC_2  { 0x8C, 0x94, 0xDF, 0x93, 0xC7, 0xA0 }

// ── Cone mode ──────────────────────────────────────────────────────────────
#define CONE_HALF_ANGLE_DEG  30.0f

// ── WiFi scanning (scanner nodes only) ────────────────────────────────────
#define CHANNEL_DWELL_MS  200   // ms per channel, 200 × 13 = 2.6s full sweep
#define RSSI_FLOOR_DBM   -90   // Discard detections weaker than this

// ── Queue ──────────────────────────────────────────────────────────────────
#define RECORD_QUEUE_DEPTH  32

// ── Debug ──────────────────────────────────────────────────────────────────
// Define via build_flags: -DDEBUG_SERIAL_ONLY
// #define DEBUG_SERIAL_ONLY

// ── Anchor mode defaults (overridden by build_flags) ──────────────────────
// These are only used when ANCHOR_MODE is defined.
#ifndef ANCHOR_CHANNEL
  #define ANCHOR_CHANNEL   6
#endif
#ifndef ANCHOR_SSID
  #define ANCHOR_SSID      "WARDRIVE_ANCHOR"
#endif
#ifndef ANCHOR_TX_POWER
  #define ANCHOR_TX_POWER  20   // dBm × 4 in ESP-IDF (20 = 5 dBm)
#endif

// ── GPIO pin assignments (ESP32 DEVKIT V1) ────────────────────────────────
#define PIN_I2C_SDA     21      // Standard Arduino I²C SDA
#define PIN_I2C_SCL     22      // Standard Arduino I²C SCL
#define PIN_LUNA_RX     16      // UART2 RX ← TF-Luna TX (if used)
#define PIN_LUNA_TX     17      // UART2 TX → TF-Luna RX (if used)
#define PIN_STATUS_LED   2      // Onboard LED

// ── Rangefinder selection ──────────────────────────────────────────────────
#define RANGEFINDER_VL53L1X     // Default: short-range I²C (up to 4m)
// #define RANGEFINDER_TF_LUNA  // Alternative: long-range UART (up to 8m)

// ── I²C speed ─────────────────────────────────────────────────────────────
#define I2C_FREQ_HZ  400000
