// firmware/arduino-r4-base/src/main.cpp
// Wardriving base node — top level.
//
// The R4 is a dumb serial->WebSocket relay. It reads JSON detection lines
// from node3 (the ESP32 anchor / base receiver) over NODE3_SERIAL and
// forwards every line that looks like a JSON record to the frontend over a
// WiFi WebSocket. Non-JSON lines (node3 diagnostics) are logged to USB and
// dropped. See config.h for wiring.

#include <Arduino.h>
#include "config.h"
#include "led_matrix.h"
#include "web_export.h"

// Accumulates one line from node3 without blocking (unlike readStringUntil,
// which would stall loop() — and the WebSocket keep-alive — waiting for '\n').
static char   line_buf[MAX_LINE_LEN];
static size_t line_len = 0;

void setup() {
    Serial.begin(115200);
    NODE3_SERIAL.begin(NODE3_BAUD);

    led_matrix_init();

    if (!web_export_init()) {
        Serial.println("WiFi AP / WebSocket server failed to start");
    }

    Serial.println("Wardriver R4 ready — relaying node3 JSON to WebSocket.");
}

void loop() {
    web_export_poll();

    bool forwarded = false;

    while (NODE3_SERIAL.available()) {
        char c = (char)NODE3_SERIAL.read();

        if (c != '\n') {
            if (line_len < MAX_LINE_LEN - 1) {
                line_buf[line_len++] = c;
            } else {
                line_len = 0;  // Oversized line — drop it and resync on next '\n'
            }
            continue;
        }

        // End of line — finalize and dispatch.
        if (line_len > 0 && line_buf[line_len - 1] == '\r') line_len--;  // strip CR
        line_buf[line_len] = '\0';

        if (line_len > 0) {
            if (line_buf[0] == '{') {
                // JSON detection record — forward verbatim to all clients.
                web_export_broadcast_line(line_buf);
                forwarded = true;
            } else {
                // node3 diagnostic line — log locally, do not forward.
                Serial.print("[NODE3] ");
                Serial.println(line_buf);
            }
        }
        line_len = 0;
    }

    led_matrix_update(forwarded);
}
