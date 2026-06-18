// firmware/lcd-driver/src/espnow_rx.h
// Receives WardrivingRecord frames and mirrored mode-command frames from
// the ESP-NOW bridge over BRIDGE_SERIAL, queues records for the main loop,
// and tracks current mode/cone-lock state (the R4 owns that state — this
// board only ever sees a mirror of it, never sends commands itself).

#pragma once
#include "../../shared/packet_schema.h"

// Sized down from the original 32 for the Mega's 8KB SRAM — see config.h.
#define RX_QUEUE_DEPTH 16

void espnow_rx_init();

// Reads any bytes currently available on BRIDGE_SERIAL and advances the
// frame parser. Call every loop() iteration. Cheap when no bytes pending.
void espnow_rx_poll();

bool espnow_rx_dequeue(WardrivingRecord* out);
uint32_t espnow_rx_dropped();

uint8_t espnow_rx_mode();
bool    espnow_rx_cone_locked();
