// firmware/lcd-driver/src/sd_logger.h
// WiGLE-compatible CSV + extended CSV writer.

#pragma once
#include "../../shared/packet_schema.h"

bool sd_logger_init();
void sd_logger_write_wigle(const WardrivingRecord* r);
void sd_logger_write_extended(const WardrivingRecord* r, const char* manufacturer);
