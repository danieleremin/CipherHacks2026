// firmware/lcd-driver/src/config.h
// Compile-time configuration for the Mega LCD/SD driver node.
// See ../../shared/uart_bridge_protocol.h for the full topology rationale.

#pragma once

// Link to the ESP-NOW bridge (RECORD frames + mirrored CMD frames, one-way).
// Wiring: bridge TX (GPIO25) -> Mega RX1 (pin 19), bridge RX (GPIO26) <- Mega TX1 (pin 18),
// common GND. The Mega's digital pins run at 5V logic; the ESP32's RX pin is
// NOT 5V tolerant, so Mega TX1 -> bridge RX MUST go through a level shifter
// or voltage divider. Bridge TX -> Mega RX1 needs no shifting.
#define BRIDGE_SERIAL  Serial1
#define BRIDGE_BAUD    115200

// LCD dimensions (20x4 character-equivalent layout on the TFT)
#define LCD_COLS  20
#define LCD_ROWS  4

// SD log filenames (same physical SD slot as the TFT shield, CS pin 10 —
// this is the same "regular UNO shield" pinout MCUFRIEND_kbv already
// supports natively on ATmega2560).
#define WIGLE_CSV_FILENAME    "/wardrive_wigle.csv"
#define EXTENDED_CSV_FILENAME "/wardrive_ext.csv"
#define OUI_TABLE_FILENAME    "/oui.bin"
#define SD_CS_PIN  10

// Deduplication window — skip re-logging same BSSID within this many seconds
#define DEDUP_WINDOW_SEC  30

// RAM budget note: the original spec sized these for the R4's 32KB SRAM.
// This logic now runs on a Mega2560, which has only 8KB SRAM total (shared
// with the SD library's sector buffer, Adafruit GFX, and the stack), so
// these are cut down accordingly: dedup map 64*10B=640B, RX queue
// 16*~60B=~960B, OUI cache below ~450B — leaves headroom for the rest.
#define DEDUP_MAP_SIZE  64

// In-RAM OUI cache size (cuts repeat SD seeks for recently seen OUIs)
#define OUI_CACHE_SIZE  16

// ── GPS placeholder ──────────────────────────────────────────────────────
// GPS was removed from the sensor nodes' WardrivingRecord (see
// ../../shared/packet_schema.h). The WiGLE CSV format requires a position,
// so until GPS is reintroduced, every row is logged with this fixed
// placeholder coordinate. Fill in your actual deployment location.
#define PLACEHOLDER_LATITUDE   0.0
#define PLACEHOLDER_LONGITUDE  0.0
#define PLACEHOLDER_ALTITUDE_M 0.0f
