// firmware/lcd-driver/src/lcd_display.h
// 2.8" TFT shield status layout (4 text rows).

#pragma once
#include "../../shared/packet_schema.h"

void lcd_init();

void lcd_update(uint8_t mode, bool cone_locked,
                uint16_t unique_nets, uint8_t active_nodes,
                const WardrivingRecord* last_rec, const char* mfr);
