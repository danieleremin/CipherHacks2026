// firmware/arduino-r4-base/src/mode_manager.h
// Owns the current scan mode. USB-serial commands update local state and
// are relayed to all sensor nodes via the ESP-NOW bridge.

#pragma once
#include <stdint.h>

void mode_manager_init();
void mode_manager_update();  // Call every loop(); polls USB Serial for commands

uint8_t mode_manager_get_mode();
bool    mode_manager_cone_locked();
