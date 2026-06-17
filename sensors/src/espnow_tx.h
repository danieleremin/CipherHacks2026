// firmware/esp32-sensor/src/espnow_tx.h
// ESP-NOW transmit task: drains the detection queue and sends records
// to the Arduino R4 base node. Handles serial debug fallback.
// See SPEC_ESP32_SENSOR_NODE.md Section 9 and 10.

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Creates the record queue and starts the ESP-NOW TX task on Core 1
// at priority 6 (highest of all sensor tasks — sends take precedence).
// Returns the queue handle that wifi_scan_init() and other tasks push into.
QueueHandle_t espnow_tx_init();

// Returns the number of records successfully sent since boot.
uint32_t espnow_tx_sent_count();

// Returns the number of records dropped due to queue overflow.
uint32_t espnow_tx_dropped_count();

// Returns the number of ESP-NOW send failures (ACK not received).
uint32_t espnow_tx_fail_count();