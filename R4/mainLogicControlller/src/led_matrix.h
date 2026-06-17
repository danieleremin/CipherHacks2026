// firmware/arduino-r4-base/src/led_matrix.h
// Onboard 12x8 LED matrix: scanning sweep + flash on record receive + cone-mode indicator.

#pragma once
#include <stdint.h>

void led_matrix_init();
void led_matrix_update(uint8_t mode, bool cone_locked, bool record_just_received);
