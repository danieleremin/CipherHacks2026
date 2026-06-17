// shared/uart_bridge_protocol.h
// Wire framing for the hardware-UART link between the Arduino R4 base node
// and the companion ESP32 ESP-NOW bridge node.
//
// Background: the Uno R4 WiFi's onboard ESP32-S3 co-processor only runs
// Arduino's stock WiFiS3/NINA bridge firmware, which exposes no ESP-NOW API
// to the host RA4M1. A second, ordinary ESP32 does real ESP-NOW (same API as
// the sensor nodes) and relays frames to/from the R4 over a plain UART pair.
// Topology:
//
//   sensor nodes --ESP-NOW--> bridge --UART(RECORD)--> R4
//   R4 --UART(CMD)--> bridge --ESP-NOW broadcast--> sensor nodes
//
// (An earlier revision of this firmware routed records to a Mega-driven
// LCD/SD shield instead of the R4; that path has been dropped — the R4 is
// now the sole consumer, and is expected to expose this data to a separate
// frontend itself.)
//
// DO NOT modify without coordinating with the other firmware owners.

#pragma once
#include <stdint.h>

#define BRIDGE_SYNC_0  0xAA
#define BRIDGE_SYNC_1  0x55

// Packet types
#define BRIDGE_PKT_RECORD   0x01  // bridge -> R4 : payload is a WardrivingRecord
#define BRIDGE_PKT_CMD      0x02  // R4 -> bridge : payload is 1 command byte (CMD_*)

// Frame layout: [SYNC0][SYNC1][TYPE][LEN][PAYLOAD x LEN][CHECKSUM]
// CHECKSUM = XOR of TYPE, LEN, and all payload bytes.
#define BRIDGE_FRAME_OVERHEAD  5  // sync0 + sync1 + type + len + checksum

inline uint8_t bridge_checksum(uint8_t type, uint8_t len, const uint8_t* payload) {
    uint8_t sum = type ^ len;
    for (uint8_t i = 0; i < len; i++) sum ^= payload[i];
    return sum;
}
