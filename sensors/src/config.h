// firmware/esp32-sensor/src/config.h
// Compile-time configuration for the ESP32 wardriving sensor node.
// Target board: ESP32 DEVKIT V1 (original ESP32, not S3)

#pragma once

// ── Node identity ──────────────────────────────────────────────────────────
#ifndef NODE_ID
  #define NODE_ID  0x01
#endif

// ── ESP-NOW peer ───────────────────────────────────────────────────────────
// MAC address of the Arduino R4 WiFi base node.
// Read the R4's MAC from its setup() Serial output, then fill in here.
#define BASE_NODE_MAC  { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF }

// ── Cone mode ──────────────────────────────────────────────────────────────
#define CONE_HALF_ANGLE_DEG  30.0f

// ── WiFi scanning ──────────────────────────────────────────────────────────
#define CHANNEL_DWELL_MS  200
#define RSSI_FLOOR_DBM   -90

// ── Queue ──────────────────────────────────────────────────────────────────
#define RECORD_QUEUE_DEPTH  32

// ── Debug ──────────────────────────────────────────────────────────────────
// Uncomment to skip ESP-NOW and print records to Serial instead:
// #define DEBUG_SERIAL_ONLY

// ── GPIO pin assignments (ESP32 DEVKIT V1) ────────────────────────────────
// DEVKIT V1 safe I²C pins — GPIO 21 (SDA) and 22 (SCL) are the Arduino-style
// defaults on this board and have no conflicts with boot/strapping pins.
#define PIN_I2C_SDA     21
#define PIN_I2C_SCL     22

// TF-Luna UART pins (only used if RANGEFINDER_TF_LUNA is defined)
// GPIO 16/17 are safe on DEVKIT V1 for UART2
#define PIN_LUNA_RX     16
#define PIN_LUNA_TX     17

// Onboard LED — GPIO 2 on DEVKIT V1 (same as S3)
#define PIN_STATUS_LED   2

// ── Rangefinder selection ──────────────────────────────────────────────────
#define RANGEFINDER_VL53L1X     // Short-range I²C (up to 4m) — default
// #define RANGEFINDER_TF_LUNA  // Long-range UART (up to 8m)

// ── I²C speed ─────────────────────────────────────────────────────────────
#define I2C_FREQ_HZ  400000