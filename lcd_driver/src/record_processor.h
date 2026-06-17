// firmware/lcd-driver/src/record_processor.h
// BSSID deduplication. Uses this board's own millis() as the clock rather
// than the record's uptime_ms, since uptime_ms is relative to each sensor
// node's own boot time and isn't comparable across nodes.

#pragma once
#include "../../shared/packet_schema.h"

// Returns true if this is a new or expired detection (should be logged to
// the extended CSV). Always log to the WiGLE CSV regardless of the result.
bool dedup_check(const WardrivingRecord* r);

uint16_t dedup_unique_count();
