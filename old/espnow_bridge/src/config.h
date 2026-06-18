// firmware/espnow-bridge/src/config.h
// Compile-time configuration for the ESP-NOW <-> UART bridge node.
// Target board: ESP32 DEVKIT V1 (same family as the sensor nodes).
//
// Single UART leg to the R4: forwards RECORD frames from the scanning
// nodes, and relays CMD bytes from the R4 out to the scanning nodes over
// ESP-NOW. See ../../shared/uart_bridge_protocol.h for the framing.

#pragma once

#ifndef BRIDGE_UART_BAUD
  #define BRIDGE_UART_BAUD  115200
#endif

// Wiring: bridge TX (GPIO17) -> R4 RX1 (pin 0), bridge RX (GPIO16) <- R4 TX1 (pin 1),
// common GND. The R4's digital pins run at 5V logic; the ESP32's RX pin is
// NOT 5V tolerant, so the R4 TX1 -> bridge RX line MUST go through a level
// shifter or voltage divider (e.g. 1k/2k to ~3.3V). Bridge TX -> R4 RX1 is
// fine without shifting since the R4 reads 3.3V as a valid high.
#define PIN_R4_UART_RX  16
#define PIN_R4_UART_TX  17

// ESP-NOW must run on the same fixed channel as the sensor nodes' ESP-NOW
// traffic (sensor nodes hop channels for scanning but use this fixed
// channel for ESP-NOW regardless).
#define ESPNOW_CHANNEL  6

// Onboard LED — GPIO2 on DEVKIT V1, flashes on any forwarded traffic.
#define PIN_STATUS_LED  2

#define RECORD_QUEUE_DEPTH  32
