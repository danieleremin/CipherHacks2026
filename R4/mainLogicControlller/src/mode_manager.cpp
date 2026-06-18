// firmware/arduino-r4-base/src/mode_manager.cpp

#include <Arduino.h>
#include "mode_manager.h"
#include "config.h"
#include "../../../shared/packet_schema.h"
#include "../../../shared/uart_bridge_protocol.h"

static uint8_t current_mode = MODE_RADIUS;
static bool    cone_locked  = false;

static void broadcast_cmd(uint8_t cmd) {
    uint8_t checksum = bridge_checksum(BRIDGE_PKT_CMD, 1, &cmd);
    BRIDGE_SERIAL.write(BRIDGE_SYNC_0);
    BRIDGE_SERIAL.write(BRIDGE_SYNC_1);
    BRIDGE_SERIAL.write((uint8_t)BRIDGE_PKT_CMD);
    BRIDGE_SERIAL.write((uint8_t)1);
    BRIDGE_SERIAL.write(cmd);
    BRIDGE_SERIAL.write(checksum);
}

void mode_manager_init() {
    // Nothing to init for serial — BRIDGE_SERIAL is opened by espnow_rx_init().
}

void mode_manager_update() {
    if (!Serial.available()) return;

    char c = Serial.read();

    // R = Radius Mode
    if (c == 'R' || c == 'r') {
        if (current_mode != MODE_RADIUS) {
            current_mode = MODE_RADIUS;
            cone_locked = false;
            broadcast_cmd(CMD_SET_RADIUS);
            Serial.println("Switched to RADIUS mode");
        }
    }
    // C = Cone Mode
    else if (c == 'C' || c == 'c') {
        if (current_mode != MODE_CONE) {
            current_mode = MODE_CONE;
            broadcast_cmd(CMD_SET_CONE);
            Serial.println("Switched to CONE mode");
        }
    }
    // L = Toggle Cone Lock
    else if (c == 'L' || c == 'l') {
        if (current_mode == MODE_CONE) {
            cone_locked = !cone_locked;
            if (cone_locked) {
                broadcast_cmd(CMD_CONE_LOCK);
                Serial.println("Cone LOCKED");
            } else {
                Serial.println("Cone UNLOCKED");
            }
        } else {
            Serial.println("Error: Must be in CONE mode to lock");
        }
    }
}

uint8_t mode_manager_get_mode()    { return current_mode; }
bool    mode_manager_cone_locked() { return cone_locked; }
