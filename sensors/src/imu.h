// firmware/esp32-sensor/src/imu.h
// BNO085 IMU interface: heading (yaw) from rotation vector, cone lock.
// Runs as a FreeRTOS task on Core 1.
// See SPEC_ESP32_SENSOR_NODE.md Section 7.

#pragma once

// Starts the IMU task on Core 1 at priority 3.
// Initializes I²C on PIN_I2C_SDA / PIN_I2C_SCL (config.h) at I2C_FREQ_HZ.
// Configures BNO085 rotation vector output at 10ms (100Hz).
// Creates the IMU mutex. Call once from setup() after Wire.begin().
void imu_task_start();

// Returns the current magnetic heading in degrees [0.0, 360.0).
// 0° = magnetic north, increases clockwise.
// Thread-safe: acquires mutex internally.
float imu_get_heading();

// Returns the locked cone heading in degrees [0.0, 360.0),
// or -1.0 if no lock has been set yet.
// Thread-safe: acquires mutex internally.
float imu_get_lock_heading();

// Saves the current heading as the cone lock direction.
// Called by the ESP-NOW receive callback when CMD_CONE_LOCK arrives.
// Thread-safe: acquires mutex internally.
void imu_lock_heading();

// Clears the cone lock (resets to -1.0).
// Call when switching back to radius mode.
void imu_clear_lock();