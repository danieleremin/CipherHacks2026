// firmware/arduino-r4-base/src/led_matrix.h
// Onboard 12x8 LED matrix: heartbeat sweep + flash whenever a record is
// forwarded to the frontend.

#pragma once

void led_matrix_init();
void led_matrix_update(bool record_just_forwarded);
