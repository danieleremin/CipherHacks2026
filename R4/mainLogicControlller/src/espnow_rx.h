// firmware/arduino-r4-base/src/espnow_rx.h
// Receives WardrivingRecord frames forwarded by the companion ESP-NOW
// bridge over BRIDGE_SERIAL (see config.h) and queues them for the main
// loop to drain. The R4 host MCU has no native ESP-NOW, so "rx" here means
// "rx from the bridge's UART link", not a direct radio callback.

#pragma once
#include "../../shared/packet_schema.h"

#define RX_QUEUE_DEPTH 32

void espnow_rx_init();

// Reads any bytes currently available on BRIDGE_SERIAL and advances the
// frame parser. Call every loop() iteration. Cheap when no bytes pending.
void espnow_rx_poll();

bool espnow_rx_dequeue(WardrivingRecord* out);
uint32_t espnow_rx_dropped();
