// shared/packet_schema.h
// Shared between ESP32 sensor node and Arduino R4 base node.
// DO NOT modify without coordinating with the other firmware owner.

#pragma once
#include <stdint.h>

#pragma pack(1)
typedef struct {
    uint8_t  node_id;           // Which sensor node captured this
    uint8_t  schema_version;    // Increment on any struct change; both sides must match
    uint8_t  mode;              // 0 = radius, 1 = cone
    uint8_t  in_cone;           // 1 if within ±30° cone, 0 if outside (always 1 in radius mode)
    uint32_t uptime_ms;         // millis() at time of detection — relative timestamp
    uint8_t  bssid[6];          // Raw MAC address (6 bytes)
    char     ssid[33];          // Null-terminated, max 32 chars; empty string = hidden SSID
    int8_t   rssi;              // Signal strength in dBm (negative)
    uint8_t  channel;           // WiFi channel (1–13)
    uint8_t  enc_type;          // See ENC_* constants below
    float    bearing_deg;       // IMU heading at moment of detection (-1.0 if radius mode)
    float    range_m;           // Rangefinder distance in metres (-1.0 if not active)
    float    h_lock_deg;        // Cone lock heading (-1.0 if radius mode)
} WardrivingRecord;             // ~60 bytes — well within ESP-NOW 250-byte limit
#pragma pack()

#define SCHEMA_VERSION  1

// Scan mode constants (WardrivingRecord.mode)
#define MODE_RADIUS  0
#define MODE_CONE    1

// Encryption type constants (WardrivingRecord.enc_type)
#define ENC_OPEN   0
#define ENC_WEP    1
#define ENC_WPA    2
#define ENC_WPA2   3
#define ENC_WPA3   4
#define ENC_WPA2E  5   // WPA2/WPA3 mixed or Enterprise

// ESP-NOW command bytes (sent from base node to sensor nodes, 1-byte payload)
#define CMD_SET_RADIUS  0x01
#define CMD_SET_CONE    0x02
#define CMD_CONE_LOCK   0x03