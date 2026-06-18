// firmware/arduino-r4-base/src/web_export.h
// Relays node3's JSON detection lines to a separately-built React frontend
// over a WiFi WebSocket. The R4 hosts its own access point (see config.h)
// and pushes each line as a text frame as it arrives.

#pragma once

bool web_export_init();

// Call every loop() iteration — services HTTP + WebSocket connections.
void web_export_poll();

// Broadcasts one already-formed JSON line to all connected WebSocket clients,
// verbatim. The caller is responsible for ensuring it is valid JSON (node3
// produces it; the R4 does not parse or re-encode).
void web_export_broadcast_line(const char* line);
