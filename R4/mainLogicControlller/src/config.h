// firmware/arduino-r4-base/src/config.h
// Compile-time configuration for the Arduino R4 WiFi base node.
//
// The R4 receives detection records from the ESP-NOW bridge over wired
// UART (it has no native ESP-NOW itself), sends mode commands back out the
// same link, and exposes records to a separately-built React frontend over
// a WiFi WebSocket. See ../../shared/uart_bridge_protocol.h for the bridge
// link rationale.

#pragma once

// Link to the ESP-NOW bridge.
// Wiring note: R4 digital pins are 5V logic; the bridge ESP32's RX pin is
// NOT 5V tolerant. R4 TX1 (pin 1) -> bridge RX MUST go through a level
// shifter / voltage divider. Bridge TX -> R4 RX1 (pin 0) needs no shifting.
#define BRIDGE_SERIAL  Serial1
#define BRIDGE_BAUD    115200

// ── Frontend WiFi access point ──────────────────────────────────────────
// The R4 hosts its own WiFi network rather than joining an existing one,
// so it works as a self-contained field device. Connect the machine
// running the React frontend to this network. Change the password before
// any real deployment — this is a placeholder.
#define WIFI_AP_SSID      "wardriver-r4"
#define WIFI_AP_PASSWORD  "wardrive123"

// WebSocket port the frontend connects to (ws://<r4-ip>:WS_PORT)
#define WS_PORT  81
