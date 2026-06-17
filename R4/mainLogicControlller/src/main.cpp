// firmware/arduino-r4-base/src/main.cpp
// Wardriving base node — top level.
//
// Receives detection records from the ESP-NOW bridge, and sends mode
// commands back out to it. Record export to the frontend is not wired up
// yet — see the TODO in loop() below.

#include <Arduino.h>
#include "config.h"
#include "../../shared/packet_schema.h"
#include "espnow_rx.h"
#include "mode_manager.h"
#include "led_matrix.h"
#include "web_export.h"

void setup() {
    Serial.begin(115200);
    espnow_rx_init();
    mode_manager_init();
    led_matrix_init();

    if (!web_export_init()) {
        Serial.println("WiFi AP / WebSocket server failed to start");
    }

    Serial.println("Wardriver R4 ready.");
    Serial.println("Commands: [R]adius mode, [C]one mode, [L]ock cone");
}

void loop() {
    mode_manager_update();
    espnow_rx_poll();
    web_export_poll();

    bool record_flash = false;
    WardrivingRecord rec;
    while (espnow_rx_dequeue(&rec)) {
        record_flash = true;
        web_export_broadcast_record(&rec);
    }

    led_matrix_update(mode_manager_get_mode(), mode_manager_cone_locked(), record_flash);
}
