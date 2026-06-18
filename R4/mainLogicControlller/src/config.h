// firmware/arduino-r4-base/src/config.h
// Compile-time configuration for the Arduino R4 WiFi base node.
//
// The R4 is a dumb relay: it reads JSON detection lines from node3 (the
// ESP32 anchor / base receiver) over a wired serial link and forwards each
// line to the React frontend over a WiFi WebSocket. No parsing, no mode
// logic, no display intelligence — node3 produces the JSON, the R4 just
// relays it.

#pragma once

// ── Uplink from node3 (anchor / base receiver) ───────────────────────────
// node3 prints one JSON detection record per line to its USB serial; that
// line is wired into the R4's hardware UART (Serial1, pins 0/1).
//
// Wiring note: R4 digital pins are 5V logic; node3's RX pin is NOT 5V
// tolerant. R4 TX1 (pin 1) -> node3 RX MUST go through a level shifter /
// voltage divider. node3 TX -> R4 RX1 (pin 0) needs no shifting (3.3V reads
// as a valid high on the R4). Share a common ground.
#define NODE3_SERIAL  Serial1
#define NODE3_BAUD    115200   // Must match node3's Serial baud rate

// Longest JSON line we will buffer from node3; longer lines are dropped.
#define MAX_LINE_LEN  512

// ── Frontend WiFi access point ──────────────────────────────────────────
// The R4 hosts its own WiFi network rather than joining an existing one,
// so it works as a self-contained field device. Connect the machine
// running the React frontend to this network. Change the password before
// any real deployment — this is a placeholder.
#define WIFI_AP_SSID      "wardriver-r4"
#define WIFI_AP_PASSWORD  "wardrive123"

// WebSocket port the frontend connects to (ws://<r4-ip>:WS_PORT).
#define WS_PORT  8080
