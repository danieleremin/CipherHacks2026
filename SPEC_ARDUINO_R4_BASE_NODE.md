# Wardriving Base Node — Arduino R4 WiFi Firmware Specification

**Project:** Autonomous wardriving / RF detection system  
**Document role:** Complete build specification for the Arduino R4 WiFi base node  
**Interface contract:** `shared/packet_schema.h` — defines the ESP-NOW payload structure shared with the ESP32 sensor node(s)  
**Toolchain:** PlatformIO (VSCode extension or CLI)

---

## 1. System Overview

This node is the coordinator, logger, and user interface of the wardriving system. It receives structured detection records from one or more ESP32 sensor nodes over ESP-NOW, performs manufacturer lookup from a local OUI database, writes complete records to SD card, drives an LCD display for real-time status, and accepts serial commands over USB to toggle the scan mode and broadcast it back to all sensor nodes.

You do not need to know how the ESP32 nodes capture WiFi frames, parse GPS, or read sensors. Your job begins when a `WardrivingRecord` arrives via ESP-NOW. Everything below describes how to handle it correctly.

The Arduino R4 WiFi also has its own onboard WiFi radio, which can run in promiscuous monitor mode simultaneously — giving you an additional capture channel. This is optional but increases coverage.

---

## 2. Background Concepts

### 2.1 What the Sensor Nodes Send You

Each ESP32 sensor node transmits `WardrivingRecord` structs via ESP-NOW — a connectionless peer-to-peer WiFi MAC-layer protocol with no association, no SSID, and no DHCP. Each struct is 87 bytes and arrives asynchronously.

A single detection event contains:
- Which sensor node saw it (`node_id`)
- When and where (`timestamp_unix`, `latitude`, `longitude`, `altitude_m`)
- What was detected (`bssid`, `ssid`, `rssi`, `channel`, `enc_type`)
- GPS quality metrics (`hdop_x10`, `satellites`)
- Directional data if in cone mode (`bearing_deg`, `range_m`, `h_lock_deg`, `in_cone`)

Your node stores, enriches (OUI lookup), and displays this data.

### 2.2 ESP-NOW Receive Side

ESP-NOW callbacks fire in an ISR (interrupt service routine) context on the WiFi driver's task. You must not do heavy work (SD writes, I²C, Serial) inside the callback. The correct pattern is:

1. Receive callback copies the raw bytes into a FreeRTOS queue
2. A separate task drains the queue and does the actual processing

The Arduino R4's WiFi library wraps ESP-NOW differently from the ESP-IDF used on the ESP32. The R4 uses the **WiFiS3** library (built-in to Arduino Uno R4 WiFi board support), which exposes a higher-level API. See Section 5.2 for the exact implementation.

### 2.3 OUI Manufacturer Lookup

Every network device has a **MAC address** (6 bytes). The first 3 bytes are the **OUI (Organizationally Unique Identifier)**, assigned by IEEE to manufacturers. For example, `FC:FB:FB` → "Raspberry Pi Trading Ltd", `00:1A:79` → "Cisco Systems".

The full IEEE OUI database has ~50,000 entries (~4MB uncompressed). You pre-process this into a compact binary lookup table using the `tools/oui_builder.py` script (see Section 8), which produces a sorted binary file stored on the SD card. At runtime, you do a binary search in O(log n) time using the 3-byte OUI as the key.

The binary table format (per entry, 26 bytes):
```
[OUI: 3 bytes][Manufacturer name: 23 bytes, null-padded]
```

With ~30,000 entries (filtered to common consumer OUIs), the file is approximately 780KB — fits comfortably on any SD card.

### 2.4 SD Card and CSV Format

All detection records are written to the SD card as a **WiGLE-compatible CSV file**. WiGLE (Wireless Geographic Logging Engine) is the standard database for crowdsourced WiFi mapping, and their CSV format is widely supported by analysis tools.

WiGLE CSV format (header + one row per detection):

```
WigleWifi-1.4,appRelease=1.0,model=WardriverR4,release=1.0,device=ESP32Mesh,display=LCD,board=UnoR4WiFi,brand=Custom
MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,Type
```

Data rows:
```
AA:BB:CC:DD:EE:FF,MyNetwork,[WPA2],2024-01-15 14:23:01,6,2437,-67,32.715736,-117.161087,45.2,5.0,WIFI
```

Additionally, write an **extended CSV** with the extra fields that WiGLE doesn't support:
```
...all WiGLE fields...,NodeID,Manufacturer,BearingDeg,RangeM,InCone,HLockDeg,HDOP,Satellites
```

### 2.5 Scan Modes

The base node owns the current scan mode. When the user sends a mode command over USB Serial, the base node:
1. Updates its own mode variable
2. Broadcasts a mode command to all ESP32 sensor nodes via ESP-NOW
3. Updates the LCD display

**Mode A — Radius:** All omnidirectional detections. Log everything received.

**Mode B — Cone:** When the user sends the lock command, the base node sends a `CMD_CONE_LOCK` to all nodes, which causes them to save their current IMU heading as the cone lock direction. Records arriving with `in_cone = 0` are logged but visually dimmed on the LCD.

### 2.6 LCD Display (Arduino R4 LED Matrix vs TFT Shield)

The Arduino R4 WiFi has an onboard **12×8 red LED matrix**. This is useful for very simple status (a dot moving = scanning, all lit = error) but too small for meaningful text.

**This spec uses an external 2.8" TFT LCD Shield.** These shields plug directly into the Arduino and usually include a built-in microSD card reader on the SPI pins (CS=10, MOSI=11, MISO=12, SCK=13). They use an 8-bit parallel interface for the display, utilizing `MCUFRIEND_kbv` and `Adafruit_GFX` libraries.

The onboard LED matrix can still be used as a secondary indicator (e.g., animate a scanning pattern, flash on each record received).

### 2.7 Deduplication

The same AP sends a beacon every 100ms. A sensor node capturing for 30 seconds can generate hundreds of records for the same BSSID. You should deduplicate before writing to the extended log — but always write to the WiGLE CSV (which expects one row per observation, not one per unique network).

Deduplication logic: maintain a hash map of `bssid[6] → last_seen_unix`. If a BSSID was seen within the last `DEDUP_WINDOW_SEC` seconds, update its stats (RSSI average, last position) but do not write a new WiGLE row. Write to the extended log only on first detection or every `DEDUP_WINDOW_SEC` seconds.

---

## 3. Hardware

### 3.1 Board

**Arduino Uno R4 WiFi** — the only board this spec targets.

Key capabilities:
- RA4M1 ARM Cortex-M4 at 48 MHz
- ESP32-S3 co-processor handling WiFi (this is separate from the sensor node ESP32s)
- 32KB SRAM, 256KB flash
- Onboard 12×8 LED matrix
- I²C (SDA=A4, SCL=A5 on standard Arduino pinout)
- SPI (for SD card)
- Hardware UART (pins 0, 1; also USB Serial)

**RAM constraint:** 32KB is tight. Keep dynamic allocations minimal. Use fixed-size ring buffers rather than `std::vector`. The BSSID deduplication map should be capped at a compile-time maximum (256 entries is safe; clear oldest when full).

### 3.2 Bill of Materials

| Component | Part | Interface | Notes |
|---|---|---|---|
| Board | Arduino Uno R4 WiFi | — | Includes onboard ESP32-S3 for WiFi |
| LCD / SD | 2.8" TFT LCD Shield Arduino | 8-bit / SPI | Plugs directly into Uno R4; includes SD card reader |
| microSD card | SanDisk Industrial 8GB+ | — | Wear-leveled; avoid cheap cards |

### 3.3 Pin Assignment (Arduino Uno R4 WiFi)

| Signal | Pin | Notes |
|---|---|---|
| LCD Data | D2-D9 | 8-bit parallel data (D0/D1 on shield use D8/D9) |
| LCD Ctrl | A0-A4 | RD, WR, RS, CS, RST |
| SD CS | Pin 10 | SPI chip select for built-in SD reader |
| SD MOSI | Pin 11 | SPI MOSI |
| SD MISO | Pin 12 | SPI MISO |
| SD SCK | Pin 13 | SPI clock |

**Note on Shield Pins:** The 2.8" TFT shield consumes almost all digital and analog pins. UI control is handled entirely through the USB Serial connection to avoid pin starvation.

**Note on SD card voltage:** The TFT shield handles 3.3V logic conversion for the LCD and SD card internally.

---

## 4. Software Architecture

### 4.1 Repository Structure

```
firmware/arduino-r4-base/
├── platformio.ini
├── src/
│   ├── main.cpp            — setup(), loop(), mode state machine
│   ├── espnow_rx.cpp/.h    — ESP-NOW init, receive callback, queue
│   ├── record_processor.cpp/.h — dedup, OUI lookup, field population
│   ├── sd_logger.cpp/.h    — CSV write, file rotation, SD init
│   ├── oui_lookup.cpp/.h   — binary search against OUI table on SD
│   ├── lcd_display.cpp/.h  — LCD layout, update functions
│   ├── led_matrix.cpp/.h   — R4 onboard LED matrix animations
│   ├── mode_manager.cpp/.h — mode state, button handling, ESP-NOW broadcast
│   └── config.h            — compile-time constants
└── lib/
```

Shared with ESP32 firmware (symlink or copy):
```
shared/packet_schema.h      — WardrivingRecord struct definition
```

### 4.2 Main Loop Architecture

The R4 is not running FreeRTOS directly (unlike the ESP32). The Arduino framework uses a cooperative single-threaded loop. Structure accordingly:

```
setup()
  ├── init SD card
  ├── init TFT LCD
  ├── init LED matrix
  ├── init ESP-NOW receive
  └── open log file on SD

loop()
  ├── process serial commands
  ├── drain ESP-NOW receive queue
  │     └── for each record:
  │           ├── OUI lookup
  │           ├── dedup check
  │           ├── write to SD (WiGLE CSV + extended CSV)
  │           └── update LCD
  ├── update LED matrix animation
  └── heartbeat / watchdog
```

Because the loop is single-threaded, keep each operation fast. SD writes are the slowest (~5–20ms per write). If this causes missed records, use a ring buffer: accumulate records in RAM and flush to SD in batches.

### 4.3 State Machine

```
INIT → SD_CHECK → READY
                    │
           ┌────────┴────────┐
        MODE_RADIUS      MODE_CONE
         (scanning)       (scanning)
                               │
                         CONE_LOCKED
                        (H_lock broadcast)
```

State is stored as a single `uint8_t mode` variable (values from `packet_schema.h`: `MODE_RADIUS = 0`, `MODE_CONE = 1`) plus a separate `bool cone_locked` flag.

---

## 5. Implementation Details

### 5.1 `config.h`

```c
#pragma once

// MAC addresses of all ESP32 sensor nodes (fill in after flashing each node)
// Broadcast MAC FF:FF:FF:FF:FF:FF sends to all ESP-NOW peers simultaneously
#define BROADCAST_MAC  { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }

// LCD dimensions
#define LCD_COLS  20
#define LCD_ROWS  4

// SD log filenames
#define WIGLE_CSV_FILENAME    "/wardrive_wigle.csv"
#define EXTENDED_CSV_FILENAME "/wardrive_ext.csv"
#define OUI_TABLE_FILENAME    "/oui.bin"

// Deduplication window — skip re-logging same BSSID within this many seconds
#define DEDUP_WINDOW_SEC  30

// Maximum unique BSSIDs to track for deduplication (RAM constraint)
#define DEDUP_MAP_SIZE  256

// ESP-NOW command bytes (sent in 1-byte payload to sensor nodes)
#define CMD_SET_RADIUS  0x01
#define CMD_SET_CONE    0x02
#define CMD_CONE_LOCK   0x03
```

### 5.2 `packet_schema.h` (shared with ESP32 node — do not modify unilaterally)

```c
// shared/packet_schema.h
// Shared between ESP32 sensor node(s) and Arduino R4 base node.
// DO NOT modify without coordinating with the other firmware owner.

#pragma once
#include <stdint.h>

#pragma pack(1)
typedef struct {
    uint8_t  node_id;           // Which sensor node captured this
    uint8_t  mode;              // 0 = radius, 1 = cone
    uint8_t  in_cone;           // 1 if within ±30° cone, 0 if outside (always 1 in radius mode)
    uint32_t timestamp_unix;    // UTC seconds since epoch
    uint16_t timestamp_ms;      // Milliseconds part (0–999)
    double   latitude;          // Decimal degrees, WGS84
    double   longitude;         // Decimal degrees, WGS84
    float    altitude_m;        // Metres above sea level
    uint8_t  hdop_x10;          // GPS HDOP × 10 (e.g., 12 = HDOP 1.2)
    uint8_t  satellites;        // GPS satellites in fix
    uint8_t  bssid[6];          // Raw MAC address (6 bytes)
    char     ssid[33];          // Null-terminated, max 32 chars; empty = hidden
    int8_t   rssi;              // Signal strength in dBm (negative)
    uint8_t  channel;           // WiFi channel (1–13)
    uint8_t  enc_type;          // 0=open, 1=WEP, 2=WPA, 3=WPA2, 4=WPA3, 5=WPA2/WPA3
    float    bearing_deg;       // IMU heading at detection (-1.0 in radius mode)
    float    range_m;           // Rangefinder distance (-1.0 if not active)
    float    h_lock_deg;        // Cone lock heading (-1.0 in radius mode)
} WardrivingRecord;             // ~87 bytes

// Mode constants
#define MODE_RADIUS  0
#define MODE_CONE    1

// Encryption type constants
#define ENC_OPEN   0
#define ENC_WEP    1
#define ENC_WPA    2
#define ENC_WPA2   3
#define ENC_WPA3   4
#define ENC_WPA2E  5
#pragma pack()
```

### 5.3 ESP-NOW Receive (Arduino R4 WiFi)

The R4's onboard ESP32-S3 co-processor handles WiFi. You interact with it through the **`WiFiS3`** library (included in Arduino Uno R4 board support package). As of board support package v1.1+, ESP-NOW is accessible via `espnow.h`.

```c
// espnow_rx.h
#pragma once
#include "packet_schema.h"

#define RX_QUEUE_DEPTH 32

void espnow_rx_init();
bool espnow_rx_dequeue(WardrivingRecord* out);
uint32_t espnow_rx_dropped();  // Records dropped due to full queue
```

```c
// espnow_rx.cpp
#include <WiFiS3.h>
#include <espnow.h>
#include "espnow_rx.h"
#include "config.h"

static WardrivingRecord rx_queue[RX_QUEUE_DEPTH];
static volatile uint8_t q_head = 0;
static volatile uint8_t q_tail = 0;
static volatile uint32_t dropped = 0;

static void on_data_recv(const uint8_t* mac, const uint8_t* data, int len) {
    if (len != sizeof(WardrivingRecord)) return;  // Wrong size — ignore

    uint8_t next_head = (q_head + 1) % RX_QUEUE_DEPTH;
    if (next_head == q_tail) {
        dropped++;
        return;  // Queue full — drop record
    }
    memcpy(&rx_queue[q_head], data, sizeof(WardrivingRecord));
    q_head = next_head;
}

void espnow_rx_init() {
    WiFi.begin();  // Required before ESP-NOW init on R4
    esp_now_init();
    esp_now_register_recv_cb(on_data_recv);
}

bool espnow_rx_dequeue(WardrivingRecord* out) {
    if (q_tail == q_head) return false;  // Empty
    memcpy(out, &rx_queue[q_tail], sizeof(WardrivingRecord));
    q_tail = (q_tail + 1) % RX_QUEUE_DEPTH;
    return true;
}

uint32_t espnow_rx_dropped() { return dropped; }
```

**Queue note:** This queue is a simple circular buffer. Since `on_data_recv` fires in an ISR context, use `volatile` on head/tail and keep the callback minimal (copy bytes, advance pointer, return). Never call `Serial`, `SD`, or `Wire` from inside the callback.

### 5.4 Mode Manager and ESP-NOW Broadcast

```c
// mode_manager.cpp
#include <Arduino.h>
#include <espnow.h>
#include "mode_manager.h"
#include "config.h"

static uint8_t current_mode = MODE_RADIUS;
static bool    cone_locked  = false;

static void broadcast_cmd(uint8_t cmd) {
    uint8_t bcast[] = BROADCAST_MAC;
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, bcast, 6);
    peer.channel = 0;
    peer.encrypt = false;
    // Add peer if not already added (ignore error if already exists)
    esp_now_add_peer(&peer);
    esp_now_send(bcast, &cmd, 1);
}

void mode_manager_init() {
    // Nothing to init for serial
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

uint8_t mode_manager_get_mode()      { return current_mode; }
bool    mode_manager_cone_locked()   { return cone_locked; }
```

---

## 6. OUI Lookup

### 6.1 Binary Table Format

The pre-built `oui.bin` file (generated by `tools/oui_builder.py`) is a sorted array of 26-byte entries:

```
Offset  Size  Field
0       3     OUI bytes (e.g., 0xFC, 0xFB, 0xFB)
3       23    Manufacturer name, null-padded (e.g., "Raspberry Pi Trading")
```

Sorted ascending by OUI as a 3-byte big-endian integer. Binary search is O(log₂ 30000) ≈ 15 comparisons per lookup.

### 6.2 OUI Lookup Implementation

```c
// oui_lookup.cpp
#include <SD.h>
#include "oui_lookup.h"

#define OUI_ENTRY_SIZE 26
#define OUI_NAME_LEN   23

static File oui_file;
static uint32_t oui_count = 0;

bool oui_lookup_init() {
    oui_file = SD.open(OUI_TABLE_FILENAME, FILE_READ);
    if (!oui_file) return false;
    oui_count = oui_file.size() / OUI_ENTRY_SIZE;
    return true;
}

// Returns true if found; writes name (null-terminated) into buf (must be ≥ 23 bytes)
bool oui_lookup(const uint8_t* mac, char* buf) {
    if (!oui_file || oui_count == 0) {
        strncpy(buf, "Unknown", 23);
        return false;
    }

    uint32_t target = ((uint32_t)mac[0] << 16) |
                      ((uint32_t)mac[1] << 8)  |
                       (uint32_t)mac[2];

    uint32_t lo = 0, hi = oui_count - 1;

    while (lo <= hi) {
        uint32_t mid = (lo + hi) / 2;
        oui_file.seek(mid * OUI_ENTRY_SIZE);

        uint8_t entry[OUI_ENTRY_SIZE];
        oui_file.read(entry, OUI_ENTRY_SIZE);

        uint32_t oui_val = ((uint32_t)entry[0] << 16) |
                           ((uint32_t)entry[1] << 8)  |
                            (uint32_t)entry[2];

        if (oui_val == target) {
            memcpy(buf, entry + 3, OUI_NAME_LEN);
            buf[OUI_NAME_LEN] = '\0';
            return true;
        } else if (oui_val < target) {
            lo = mid + 1;
        } else {
            if (mid == 0) break;
            hi = mid - 1;
        }
    }

    strncpy(buf, "Unknown", 23);
    return false;
}
```

**Performance note:** Each lookup does ~15 SD seek+read operations. At ~1ms per SD seek, this is ~15ms per record. If records arrive faster than this, process OUI lookup asynchronously — queue records for logging and do OUI lookups in a low-priority background pass.

---

## 7. SD Logger

### 7.1 File Initialization

```c
// sd_logger.cpp
#include <SD.h>
#include <SPI.h>
#include "sd_logger.h"
#include "config.h"

static File wigle_file;
static File ext_file;

bool sd_logger_init() {
    if (!SD.begin(10)) return false;  // CS on pin 10

    bool wigle_exists = SD.exists(WIGLE_CSV_FILENAME);
    bool ext_exists   = SD.exists(EXTENDED_CSV_FILENAME);

    wigle_file = SD.open(WIGLE_CSV_FILENAME, FILE_WRITE);
    ext_file   = SD.open(EXTENDED_CSV_FILENAME, FILE_WRITE);

    if (!wigle_file || !ext_file) return false;

    // Write headers only if files are new
    if (!wigle_exists) {
        wigle_file.println(
            "WigleWifi-1.4,appRelease=1.0,model=WardriverR4,release=1.0,"
            "device=ESP32Mesh,display=LCD,board=UnoR4WiFi,brand=Custom");
        wigle_file.println(
            "MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,"
            "CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,Type");
    }

    if (!ext_exists) {
        ext_file.println(
            "MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,"
            "CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,Type,"
            "NodeID,Manufacturer,BearingDeg,RangeM,InCone,HLockDeg,HDOP,Satellites");
    }

    return true;
}
```

### 7.2 Writing a Record

```c
// Channel → frequency conversion (2.4GHz band)
static uint16_t channel_to_mhz(uint8_t ch) {
    return (ch == 14) ? 2484 : (2407 + ch * 5);
}

// Encryption type → WiGLE AuthMode string
static const char* enc_to_str(uint8_t enc) {
    switch (enc) {
        case ENC_OPEN:  return "[ESS]";
        case ENC_WEP:   return "[WEP]";
        case ENC_WPA:   return "[WPA]";
        case ENC_WPA2:  return "[WPA2]";
        case ENC_WPA3:  return "[WPA3]";
        case ENC_WPA2E: return "[WPA2][WPA3]";
        default:        return "[?]";
    }
}

// Format unix timestamp as "YYYY-MM-DD HH:MM:SS"
static void fmt_timestamp(uint32_t unix_t, char* buf) {
    // Simple implementation — use a time library if available
    // Alternatively, store as raw unix timestamp and convert in post-processing
    uint32_t s  = unix_t % 60; unix_t /= 60;
    uint32_t m  = unix_t % 60; unix_t /= 60;
    uint32_t h  = unix_t % 24; unix_t /= 24;
    // Date calculation omitted for brevity — use a proper calendar function
    snprintf(buf, 20, "2024-01-01 %02lu:%02lu:%02lu", h, m, s);
}

void sd_logger_write(const WardrivingRecord* r, const char* manufacturer) {
    char ts[20];
    fmt_timestamp(r->timestamp_unix, ts);

    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             r->bssid[0], r->bssid[1], r->bssid[2],
             r->bssid[3], r->bssid[4], r->bssid[5]);

    // WiGLE row
    wigle_file.print(mac_str);          wigle_file.print(',');
    wigle_file.print(r->ssid);          wigle_file.print(',');
    wigle_file.print(enc_to_str(r->enc_type)); wigle_file.print(',');
    wigle_file.print(ts);               wigle_file.print(',');
    wigle_file.print(r->channel);       wigle_file.print(',');
    wigle_file.print(channel_to_mhz(r->channel)); wigle_file.print(',');
    wigle_file.print(r->rssi);          wigle_file.print(',');
    wigle_file.print(r->latitude, 6);   wigle_file.print(',');
    wigle_file.print(r->longitude, 6);  wigle_file.print(',');
    wigle_file.print(r->altitude_m, 1); wigle_file.print(',');
    wigle_file.print((float)r->hdop_x10 / 10.0f, 1); wigle_file.print(',');
    wigle_file.println("WIFI");

    // Extended row (all WiGLE fields + extras)
    ext_file.print(mac_str);           ext_file.print(',');
    ext_file.print(r->ssid);           ext_file.print(',');
    ext_file.print(enc_to_str(r->enc_type)); ext_file.print(',');
    ext_file.print(ts);                ext_file.print(',');
    ext_file.print(r->channel);        ext_file.print(',');
    ext_file.print(channel_to_mhz(r->channel)); ext_file.print(',');
    ext_file.print(r->rssi);           ext_file.print(',');
    ext_file.print(r->latitude, 6);    ext_file.print(',');
    ext_file.print(r->longitude, 6);   ext_file.print(',');
    ext_file.print(r->altitude_m, 1);  ext_file.print(',');
    ext_file.print((float)r->hdop_x10 / 10.0f, 1); ext_file.print(',');
    ext_file.print("WIFI");            ext_file.print(',');
    ext_file.print(r->node_id);        ext_file.print(',');
    ext_file.print(manufacturer);      ext_file.print(',');
    ext_file.print(r->bearing_deg, 1); ext_file.print(',');
    ext_file.print(r->range_m, 1);     ext_file.print(',');
    ext_file.print(r->in_cone);        ext_file.print(',');
    ext_file.print(r->h_lock_deg, 1);  ext_file.print(',');
    ext_file.print((float)r->hdop_x10 / 10.0f, 1); ext_file.print(',');
    ext_file.println(r->satellites);

    // Flush every N records to avoid data loss on power cut
    static uint16_t write_count = 0;
    if (++write_count % 10 == 0) {
        wigle_file.flush();
        ext_file.flush();
    }
}
```

---

## 8. LCD Display

### 8.1 Layout — TFT Display (using 20x4 character layout equivalent)

```
┌────────────────────┐
│ MODE: RADIUS   [R] │   Row 0: Mode indicator
│ NETS: 0142 NODES:2 │   Row 1: Unique BSSIDs seen, active nodes
│ AA:BB:CC Cisco -67 │   Row 2: Last detected: OUI/mfr, RSSI
│ WIFI: OK   Q_DRP:0 │   Row 3: Base node status
└────────────────────┘
```

In cone mode, row 0 changes:
```
│ CONE: LOCKED  [C*] │
```

In cone mode with an out-of-cone detection, row 2 shows `(OOC)` suffix.

### 8.2 LCD Implementation

```c
// tft_display.cpp
#include <Adafruit_GFX.h>
#include <MCUFRIEND_kbv.h>
#include "lcd_display.h"
#include "config.h"
#include "espnow_rx.h"

static MCUFRIEND_kbv tft;

void lcd_init() {
    uint16_t ID = tft.readID();
    if (ID == 0xD3D3) ID = 0x9486; // Write-only shield
    tft.begin(ID);
    tft.setRotation(1); // Landscape
    tft.fillScreen(0x0000); // Black
    tft.setTextColor(0xFFFF, 0x0000); // White on black
    tft.setTextSize(2);
    
    tft.setCursor(0, 0);
    tft.print("Wardriver R4");
    tft.setCursor(0, 20);
    tft.print("Initializing...");
}

void lcd_update(uint8_t mode, bool cone_locked,
                uint16_t unique_nets, uint8_t active_nodes,
                const WardrivingRecord* last_rec, const char* mfr) {

    // Row 0: Mode
    tft.setCursor(0, 0);
    if (mode == MODE_RADIUS) {
        tft.print("MODE: RADIUS    [R] ");
    } else if (cone_locked) {
        tft.print("CONE: LOCKED   [C*] ");
    } else {
        tft.print("CONE: UNLOCKED  [C] ");
    }

    // Row 1: Network stats
    tft.setCursor(0, 20);
    char row1[21];
    snprintf(row1, sizeof(row1), "NETS:%04u NODES:%u    ", unique_nets, active_nodes);
    tft.print(row1);

    // Row 2: Last detection
    tft.setCursor(0, 40);
    if (last_rec) {
        char mac_short[9];  // First 3 bytes only: "AA:BB:CC"
        snprintf(mac_short, sizeof(mac_short), "%02X:%02X:%02X",
                 last_rec->bssid[0], last_rec->bssid[1], last_rec->bssid[2]);

        char mfr_short[9];
        strncpy(mfr_short, mfr, 8);
        mfr_short[8] = '\0';

        char row2[21];
        snprintf(row2, sizeof(row2), "%s %-8s %4d",
                 mac_short, mfr_short, last_rec->rssi);
        tft.print(row2);
    } else {
        tft.print("Waiting for data... ");
    }

    // Row 3: Status
    tft.setCursor(0, 60);
    char row3[21];
    snprintf(row3, sizeof(row3), "WIFI: OK   Q_DRP:%-3lu ", espnow_rx_dropped());
    tft.print(row3);
}
```

---

## 9. Deduplication Map

```c
// record_processor.cpp (dedup section)

struct DedupEntry {
    uint8_t  bssid[6];
    uint32_t last_seen;
};

static DedupEntry dedup_map[DEDUP_MAP_SIZE];
static uint8_t dedup_count = 0;
static uint16_t total_unique_nets = 0;

// Returns true if this is a new or expired detection (should be logged)
bool dedup_check(const WardrivingRecord* r) {
    uint32_t now = r->timestamp_unix;

    for (uint8_t i = 0; i < dedup_count; i++) {
        if (memcmp(dedup_map[i].bssid, r->bssid, 6) == 0) {
            // Known BSSID
            if ((now - dedup_map[i].last_seen) < DEDUP_WINDOW_SEC) {
                dedup_map[i].last_seen = now;
                return false;  // Too recent — skip write
            } else {
                dedup_map[i].last_seen = now;
                return true;   // Expired — re-log
            }
        }
    }

    // New BSSID
    if (dedup_count < DEDUP_MAP_SIZE) {
        memcpy(dedup_map[dedup_count].bssid, r->bssid, 6);
        dedup_map[dedup_count].last_seen = now;
        dedup_count++;
    } else {
        // Map full — evict oldest entry (linear scan)
        uint8_t oldest_idx = 0;
        uint32_t oldest_time = dedup_map[0].last_seen;
        for (uint8_t i = 1; i < DEDUP_MAP_SIZE; i++) {
            if (dedup_map[i].last_seen < oldest_time) {
                oldest_time = dedup_map[i].last_seen;
                oldest_idx = i;
            }
        }
        memcpy(dedup_map[oldest_idx].bssid, r->bssid, 6);
        dedup_map[oldest_idx].last_seen = now;
    }

    total_unique_nets++;
    return true;
}

uint16_t dedup_unique_count() { return total_unique_nets; }
```

---

## 10. LED Matrix Animations (Onboard R4)

The R4's 12×8 LED matrix is driven by the `Arduino_LED_Matrix` library (built into the R4 board support package).

```c
// led_matrix.cpp
#include <Arduino_LED_Matrix.h>
#include "led_matrix.h"

static ArduinoLEDMatrix matrix;
static uint32_t last_flash = 0;

// Simple sweeping dot animation = scanning
static uint8_t scan_col = 0;

void led_matrix_init() {
    matrix.begin();
}

void led_matrix_update(uint8_t mode, bool record_just_received) {
    uint32_t now = millis();
    uint8_t frame[8][12] = {};  // 8 rows, 12 cols, all off

    if (record_just_received && (now - last_flash) < 80) {
        // Flash all LEDs briefly on record receive
        memset(frame, 1, sizeof(frame));
    } else {
        // Sweep animation: one dot scrolling across middle row
        if (now - last_flash > 60) {
            scan_col = (scan_col + 1) % 12;
            last_flash = now;
        }
        frame[3][scan_col] = 1;  // Middle-ish row
        frame[4][scan_col] = 1;

        // Cone mode indicator: top-left corner lit
        if (mode == MODE_CONE) {
            frame[0][0] = 1;
            frame[0][1] = 1;
            frame[1][0] = 1;
        }
    }

    matrix.renderBitmap(frame, 8, 12);
}
```

---

## 11. `platformio.ini`

```ini
[env:uno_r4_wifi]
platform  = renesas-ra
board     = uno_r4_wifi
framework = arduino

lib_deps =
    adafruit/Adafruit GFX Library @ ^1.11.5
    prenticedavid/MCUFRIEND_kbv @ ^3.1.0-Beta

monitor_speed = 115200
upload_speed  = 921600
```

---

## 12. Tools — OUI Binary Table Builder

Save as `tools/oui_builder.py`. Run once on a development machine to generate `oui.bin` for the SD card.

```python
#!/usr/bin/env python3
"""
oui_builder.py — Converts the IEEE OUI text database to a sorted binary lookup table.

Usage:
    python oui_builder.py --input oui.txt --output oui.bin [--limit 30000]

Download the IEEE OUI list from:
    https://standards-oui.ieee.org/oui/oui.txt
"""

import struct
import argparse

ENTRY_SIZE = 26  # 3 bytes OUI + 23 bytes name (null-padded)

def parse_oui_txt(path, limit):
    entries = []
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            # Lines look like: "00-00-00   (hex)  XEROX CORPORATION"
            if '(hex)' in line:
                parts = line.strip().split('(hex)')
                if len(parts) != 2:
                    continue
                oui_str = parts[0].strip().replace('-', ':')
                name    = parts[1].strip()[:22]  # Truncate to 22 chars + null

                try:
                    oui_bytes = bytes(int(x, 16) for x in oui_str.split(':'))
                except ValueError:
                    continue

                entries.append((oui_bytes, name))

                if limit and len(entries) >= limit:
                    break

    return entries

def build_binary(entries, output_path):
    # Sort by OUI as 3-byte big-endian integer
    entries.sort(key=lambda e: (e[0][0] << 16) | (e[0][1] << 8) | e[0][2])

    with open(output_path, 'wb') as f:
        for oui_bytes, name in entries:
            name_encoded = name.encode('ascii', errors='replace')
            name_padded  = name_encoded[:23].ljust(23, b'\x00')
            f.write(oui_bytes + name_padded)

    print(f"Written {len(entries)} entries to {output_path} "
          f"({len(entries) * ENTRY_SIZE / 1024:.1f} KB)")

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--input',  required=True)
    parser.add_argument('--output', default='oui.bin')
    parser.add_argument('--limit',  type=int, default=30000)
    args = parser.parse_args()

    entries = parse_oui_txt(args.input, args.limit)
    build_binary(entries, args.output)
```

Copy the output `oui.bin` to the root of the SD card before deployment.

---

## 13. Main Loop (Full Integration)

```c
// main.cpp
#include <Arduino.h>
#include "config.h"
#include "packet_schema.h"
#include "espnow_rx.h"
#include "sd_logger.h"
#include "oui_lookup.h"
#include "lcd_display.h"
#include "led_matrix.h"
#include "mode_manager.h"

static WardrivingRecord last_record;
static bool has_last_record = false;
static char last_mfr[24] = "---";
static bool record_flash = false;

void setup() {
    Serial.begin(115200);

    if (!sd_logger_init()) {
        Serial.println("SD init failed");
        while (true) { delay(200); }
    }

    if (!oui_lookup_init()) {
        Serial.println("OUI table not found on SD — manufacturer lookup disabled");
    }

    lcd_init();
    led_matrix_init();
    mode_manager_init();
    espnow_rx_init();

    Serial.println("Wardriver R4 ready.");
    Serial.println("Commands: [R]adius mode, [C]one mode, [L]ock cone");
}

void loop() {
    mode_manager_update();

    WardrivingRecord rec;
    while (espnow_rx_dequeue(&rec)) {
        // OUI lookup
        char mfr[24];
        oui_lookup(rec.bssid, mfr);

        // Dedup check + write
        if (dedup_check(&rec)) {
            sd_logger_write(&rec, mfr);
        }

        // Update display state
        memcpy(&last_record, &rec, sizeof(rec));
        strncpy(last_mfr, mfr, 23);
        has_last_record = true;
        record_flash = true;
    }

    // LCD update (throttled to avoid flicker)
    static uint32_t last_lcd = 0;
    if (millis() - last_lcd > 250) {
        last_lcd = millis();
        lcd_update(
            mode_manager_get_mode(),
            mode_manager_cone_locked(),
            dedup_unique_count(),
            1,  // TODO: track active node count from received node_ids
            has_last_record ? &last_record : nullptr,
            last_mfr
        );
    }

    // LED matrix update
    led_matrix_update(mode_manager_get_mode(), record_flash);
}
```

---

## 14. Testing Without ESP32 Nodes

To test this firmware before the ESP32 sensor nodes are available, inject synthetic `WardrivingRecord` structs over USB Serial:

```c
// In setup(), add:
#ifdef DEBUG_INJECT
  // Inject a fake record every 2 seconds
  static uint32_t last_inject = 0;
  if (millis() - last_inject > 2000) {
      last_inject = millis();
      WardrivingRecord fake = {};
      fake.node_id = 0x01;
      fake.mode = MODE_RADIUS;
      fake.in_cone = 1;
      fake.bssid[0] = 0xFC; fake.bssid[1] = 0xFB; fake.bssid[2] = 0xFB;
      fake.bssid[3] = 0x11; fake.bssid[4] = 0x22; fake.bssid[5] = 0x33;
      strncpy(fake.ssid, "TestNetwork", 32);
      fake.rssi    = -65;
      fake.channel = 6;
      fake.enc_type = ENC_WPA2;
      fake.latitude  = 32.715736;
      fake.longitude = -117.161087;
      fake.timestamp_unix = 1700000000;
      espnow_rx_inject(&fake);  // Add this helper to espnow_rx.cpp for testing
  }
#endif
```

This lets you verify SD logging, OUI lookup, deduplication, and LCD layout entirely without hardware from the ESP32 side.

---

## 15. Known Limitations and Notes

- **32KB RAM:** The dedup map (256 × 10 bytes = 2.5KB), RX queue (32 × 87 bytes = 2.8KB), and SD write buffers (~512B) consume significant RAM. Monitor usage with `ESP_getFreeHeap()` and reduce `DEDUP_MAP_SIZE` or `RX_QUEUE_DEPTH` if you get heap exhaustion.
- **SD write speed:** Buffered writes (`flush()` every 10 records) reduce latency. If you see dropped records in the ESP-NOW queue, increase `RX_QUEUE_DEPTH` or batch SD writes more aggressively.
- **ESP-NOW channel:** The R4's ESP32-S3 co-processor must be on the same channel as the sensor nodes for ESP-NOW to work. The sensor nodes hop channels; ESP-NOW uses the current channel. Set the R4 to a fixed channel (e.g., 6) and configure sensor nodes to use ch 6 for ESP-NOW regardless of their current scan channel.
- **No RTC:** The R4 has no hardware real-time clock. Timestamps come from the GPS fix of the sending sensor node. If the sensor node has no GPS fix, `timestamp_unix` will be 0 — log it anyway and flag it in post-processing.
- **OUI lookup latency:** ~15ms per lookup (SD seeks). At high detection rates this creates a processing backlog. Pre-warm a small in-RAM OUI cache (LRU, 32 entries) for recently seen OUIs to cut most lookups to zero SD I/O.
