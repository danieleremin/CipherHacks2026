// firmware/esp32-sensor/src/rangefinder.h
// Rangefinder interface — abstracts VL53L1X (I²C) and TF-Luna (UART).
// Selection is made at compile time via config.h:
//   #define RANGEFINDER_VL53L1X   (default)
//   #define RANGEFINDER_TF_LUNA
//
// Runs as a FreeRTOS task on Core 1 at priority 3.
// See SPEC_ESP32_SENSOR_NODE.md Section 2.7.

#pragma once

// Starts the rangefinder task. Initializes the hardware for whichever
// rangefinder is selected in config.h. Call once from setup().
void rangefinder_task_start();

// Returns the most recent distance measurement in metres.
// Returns -1.0f if:
//   - No valid reading has been received yet
//   - The sensor reported an error or out-of-range condition
//   - Rangefinder is disabled (RANGEFINDER_VL53L1X and RANGEFINDER_TF_LUNA
//     are both undefined)
// Thread-safe: uses atomic float read (aligned 32-bit on Xtensa is atomic).
float rangefinder_get_distance();