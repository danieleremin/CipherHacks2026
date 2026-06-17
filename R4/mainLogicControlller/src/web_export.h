// firmware/arduino-r4-base/src/web_export.h
// Exposes detection records to a separately-built React frontend over a
// WiFi WebSocket. The R4 hosts its own access point (see config.h) and
// pushes each record as a JSON text frame as it arrives.

#pragma once
#include "../../shared/packet_schema.h"

bool web_export_init();

// Call every loop() iteration — services HTTP + WebSocket connections.
void web_export_poll();

// Broadcasts one record to all connected WebSocket clients as JSON.
void web_export_broadcast_record(const WardrivingRecord* r);
