// firmware/esp32-sensor/src/config.h
// Compile-time configuration for the ESP32 wardriving sensor node.
// NODE_ID is injected via platformio.ini build_flags: -DNODE_ID=1
// Edit BASE_NODE_MAC after flashing the Arduino R4 and reading its MAC.

#pragma once

// ── Node identity ──────────────────────────────────────────────────────────
// NODE_ID is defined via build_flags in platformio.ini (-DNODE_ID=1).
// Fallback to 0x01 if not set (should not happen in normal builds).
#ifndef NODE_ID
  #define NODE_ID  0x01
#endif

// ── ESP-NOW peer ───────────────────────────────────────────────────────────
// MAC address of the Arduino R4 WiFi base node.
// Read the R4's MAC from its setup() Serial output, then fill in here.
// Format: six comma-separated 0x-prefixed hex bytes.
#define BASE_NODE_MAC  { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF }

// ── Cone mode ──────────────────────────────────────────────────────────────
// Half-angle of the detection cone in degrees. Full cone = 2 × this value.
// Spec calls for 60° total → 30° half-angle.
#define CONE_HALF_ANGLE_DEG  30.0f

// ── GPS quality thresholds ─────────────────────────────────────────────────
// Scanning does not begin until both conditions are met.
#define MIN_SATELLITES  4       // Minimum satellites for a valid fix
#define MAX_HDOP        3.0f    // Maximum HDOP (lower = more accurate)

// ── WiFi scanning ──────────────────────────────────────────────────────────
// Time spent on each channel before hopping to the next (milliseconds).
// 200ms × 13 channels = 2.6s per full sweep.
#define CHANNEL_DWELL_MS  200

// RSSI floor: detections weaker than this dBm value are discarded as noise.
#define RSSI_FLOOR_DBM  -90

// ── Queue ──────────────────────────────────────────────────────────────────
// Depth of the inter-task detection queue (WardrivingRecord structs).
// Each record is ~87 bytes. 32 entries = ~2.8KB of queue RAM.
#define RECORD_QUEUE_DEPTH  32

// ── Debug ──────────────────────────────────────────────────────────────────
// When defined, skip ESP-NOW transmission and dump records to Serial instead.
// Useful for development without the Arduino R4 base node present.
// Uncomment to enable, or define via build_flags: -DDEBUG_SERIAL_ONLY
#define DEBUG_SERIAL_ONLY

// ── GPIO pin assignments (ESP32-S3-DevKitC-1) ─────────────────────────────
#define PIN_GPS_RX      16      // UART1 RX ← GPS TX
#define PIN_GPS_TX      17      // UART1 TX → GPS RX
#define PIN_I2C_SDA      8      // I²C SDA (BNO085 + VL53L1X)
#define PIN_I2C_SCL      9      // I²C SCL
#define PIN_LUNA_RX     18      // UART2 RX ← TF-Luna TX (if using TF-Luna)
#define PIN_LUNA_TX     19      // UART2 TX → TF-Luna RX (if using TF-Luna)
#define PIN_STATUS_LED   2      // Onboard LED (DevKitC)

// ── Rangefinder selection ──────────────────────────────────────────────────
// Uncomment exactly one. Determines which rangefinder driver is compiled in.
#define RANGEFINDER_VL53L1X     // Short-range I²C (up to 4m) — default
// #define RANGEFINDER_TF_LUNA  // Long-range UART (up to 8m)

// ── GPS baud rate ──────────────────────────────────────────────────────────
// u-blox NEO-M9N default is 115200. NEO-M8N default may be 9600.
// Match this to your module's configured rate.
#define GPS_BAUD  115200

// ── I²C speed ─────────────────────────────────────────────────────────────
#define I2C_FREQ_HZ  400000    // 400kHz fast mode