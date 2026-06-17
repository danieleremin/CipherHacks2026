// firmware/esp32-sensor/src/wifi_scan.h
// WiFi promiscuous capture, 802.11 beacon/probe frame parsing,
// channel hopping timer, and ESP-NOW initialization.
//
// Call order in setup():
//   wifi_scan_init()       — configures WiFi + ESP-NOW, starts channel hop timer
//   wifi_scan_start()      — enables promiscuous mode and the sniffer callback
//
// The sniffer callback fires on Core 0 in the WiFi driver task context.
// It pushes parsed WardrivingRecord structs into record_queue (defined in
// espnow_tx.h). All heavy work (ESP-NOW send, Serial print) happens
// in the espnow_tx_task on Core 1.

#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Initializes NVS, WiFi stack (STA mode), ESP-NOW, peer registration,
// and the channel-hop timer. Does NOT yet enable promiscuous capture.
void wifi_scan_init(QueueHandle_t queue);

// Enables promiscuous mode and registers the sniffer callback.
// Call only after wifi_scan_init() and after GPS fix is acquired.
void wifi_scan_start();

// Stops promiscuous capture without tearing down WiFi/ESP-NOW.
void wifi_scan_stop();

// Called by mode_manager when a CMD_SET_CONE / CMD_SET_RADIUS arrives
// via ESP-NOW from the base node.
void wifi_scan_set_mode(uint8_t mode);

// Current channel being monitored (1–13). Updated by the hop timer.
uint8_t wifi_scan_current_channel();