# Wardriving Sensor Node — ESP32 Firmware Specification

**Project:** Autonomous wardriving / RF detection system  
**Document role:** Complete build specification for the ESP32 sensor node  
**Interface contract:** `shared/packet_schema.h` — defines the ESP-NOW payload structure shared with the Arduino R4 base node  
**Toolchain:** PlatformIO (VSCode extension or CLI)

---

## 1. System Overview

This node is the primary sensing unit of the wardriving system. It runs continuously, capturing 802.11 WiFi frames from the air in promiscuous (monitor) mode, reads GPS position and heading, optionally ranges a target with a laser/IR rangefinder, and transmits structured detection records to an Arduino R4 WiFi base node over ESP-NOW.

You do not need to know what the base node does with the data. Your job ends at transmitting a valid `WardrivingRecord` struct via ESP-NOW. Everything below describes how to produce that struct correctly.

The system supports two scan modes, selected by the base node and broadcast to all sensor nodes:

- **Mode A — Radius:** Omnidirectional. Log all detected devices within radio range.
- **Mode B — Cone:** Directional. A 60° field of view (±30° from a locked heading). Detections outside this cone are still captured but flagged as out-of-cone in the record; filtering is left to the base node.

Multiple ESP32 nodes can operate simultaneously. Each node is assigned a unique `node_id` at compile time (see Section 5.1).

---

## 2. Background Concepts

### 2.1 802.11 Monitor Mode and Promiscuous Capture

Normally a WiFi radio only processes frames addressed to its own MAC. **Promiscuous mode** lifts this restriction — the radio passes every frame it receives to a callback, regardless of destination. This is the foundation of wardriving.

The frames you care about are:

| Frame type | Subtype | What it tells you |
|---|---|---|
| Management | Beacon (0x08) | An AP announcing itself: SSID, BSSID, channel, capabilities, encryption |
| Management | Probe Response (0x05) | AP responding to a client's probe request — same info as beacon |
| Management | Probe Request (0x04) | A client device hunting for known networks — gives you client MAC and sometimes SSID |

Beacon frames are broadcast every 100ms by every AP. They're the richest data source and the primary target.

In the ESP-IDF (the native Espressif SDK used under the hood by PlatformIO/Arduino), promiscuous mode is enabled via:

```c
esp_wifi_set_promiscuous(true);
esp_wifi_set_promiscuous_rx_cb(&wifi_sniffer_cb);
```

The callback receives a `wifi_promiscuous_pkt_t*` pointer containing the raw frame bytes and an `wifi_pkt_rx_ctrl_t` struct with RSSI, channel, and noise floor.

### 2.2 Parsing Beacon Frames

An 802.11 beacon frame has a fixed header followed by **Tagged Parameters** (also called Information Elements, IEs). Each IE has the format:

```
[Tag ID: 1 byte][Tag Length: 1 byte][Tag Data: N bytes]
```

Key IEs:

| Tag ID | Name | Contents |
|---|---|---|
| 0x00 | SSID | Network name, UTF-8, 0–32 bytes |
| 0x03 | DS Parameter Set | Current channel (1 byte) |
| 0x30 | RSN (Robust Security Network) | WPA2/WPA3 capabilities |
| 0xDD | Vendor Specific | WPA (older), WPS, etc. |

If the SSID IE has length 0, the network is a **hidden SSID** — log the BSSID but store an empty string.

Encryption detection logic:

1. If the RSN IE (0x30) is present → WPA2 or WPA3. Check RSN capabilities field for SAE (WPA3).
2. If only vendor IE (0xDD) with OUI `00:50:f2:01` is present → WPA.
3. If the `Capability Information` field (bytes 34–35 of the fixed header) has bit 4 set but no RSN/WPA IE → WEP.
4. None of the above → Open.

### 2.3 RSSI and Distance Estimation

RSSI (Received Signal Strength Indicator) is reported in dBm (typically −30 to −90 dBm). You can estimate distance using the **log-distance path loss model**:

```
distance_m = 10 ^ ((TxPower_dBm - RSSI_dBm) / (10 * n))
```

Where:
- `TxPower_dBm` ≈ −20 dBm (typical AP transmit power at the antenna; use −20 as a conservative default)
- `n` = path loss exponent: 2.0 for free space, 2.7–3.5 for indoor/obstructed environments

This is an approximation. RSSI varies with multipath, antenna orientation, and obstructions. Log the raw RSSI — let post-processing tools compute distance.

### 2.4 OUI Manufacturer Lookup

The first 3 bytes of any MAC address are the **Organizationally Unique Identifier (OUI)**, assigned by IEEE to device manufacturers. `AA:BB:CC:DD:EE:FF` has OUI `AA:BB:CC`.

Because the full IEEE OUI database is ~4MB uncompressed, this node does **not** perform OUI lookup. It transmits the raw BSSID bytes. The base node handles OUI resolution.

### 2.5 GPS — NMEA Sentences

The GPS module outputs **NMEA 0183** sentences over UART at 9600 or 115200 baud. The two sentences you need:

- **`$GPRMC`** — Recommended Minimum: lat, lon, speed, date, time, fix validity
- **`$GPGGA`** — Fix data: lat, lon, altitude, number of satellites, HDOP

Parse only these two. A minimal NMEA parser needs to:
1. Buffer incoming bytes until `\n`
2. Verify the checksum (XOR of all bytes between `$` and `*`)
3. Split on commas and extract fields by position

The TinyGPS++ library handles all of this. Use it.

**HDOP** (Horizontal Dilution of Precision) is a unitless quality indicator. Values below 2.0 are good; above 5.0 the fix is unreliable. Store it as `uint8_t hdop_x10` (multiply by 10 before storing — preserves one decimal place in an integer).

### 2.6 IMU — Heading

The BNO085 is an integrated AHRS (Attitude and Heading Reference System). It internally fuses accelerometer, gyroscope, and magnetometer data and outputs quaternions or Euler angles directly. Use the **rotation vector** output for heading — it's the most stable.

Heading (yaw) ranges 0°–360°. 0° is magnetic north. Calibrate by waving the sensor in a figure-8 pattern after power-up.

**Magnetic declination:** True north ≠ magnetic north. In San Diego, declination is approximately +11° East. The cone mode math uses magnetic heading throughout; apply declination only if you need true-north alignment (not required for this project).

### 2.7 Rangefinder

Two options, choose based on range requirement:

| Module | Interface | Max range | Best for |
|---|---|---|---|
| VL53L1X | I²C | ~4m | Pedestrian / handheld use |
| TF-Luna | UART or I²C | ~8m | Vehicle or longer-range use |

The rangefinder measures distance to whatever is directly in front of the device. In cone mode, this reading is stored in the record as `range_m`. It does not filter which WiFi devices are detected — all WiFi frames are still captured. The range reading contextualizes detections (e.g., "there's a physical obstacle 2.3m away in this bearing").

### 2.8 ESP-NOW

**ESP-NOW** is a connectionless peer-to-peer protocol developed by Espressif. It operates at the WiFi MAC layer without requiring an association (no SSID, no handshake, no DHCP). Packets are sent to a peer's MAC address directly.

Key properties:
- Max payload: **250 bytes**
- Latency: ~1ms
- Range: same as standard WiFi (~100m open air)
- Does not interfere with promiscuous capture when configured correctly (see Section 5.3)

The `WardrivingRecord` struct is 87 bytes — well within the 250-byte limit.

### 2.9 Cone Mode — Bearing Filter (60° / ±30°)

In cone mode, the node locks a heading `H_lock` at mode activation (read from IMU). Each detection is tagged with the current IMU heading `H_detect`. The **angular difference** between them:

```c
float delta = fmodf(fabsf(H_detect - H_lock), 360.0f);
if (delta > 180.0f) delta = 360.0f - delta;
bool in_cone = (delta <= 30.0f);
```

Store `in_cone` as a flag in the record. Do not suppress out-of-cone records — transmit all detections and let the base node decide. This preserves data and allows post-processing to re-tune the cone angle.

---

## 3. Hardware

### 3.1 Recommended MCU

**ESP32-S3** (e.g., Espressif ESP32-S3-DevKitC-1 or equivalent)

- Dual-core Xtensa LX7 at 240 MHz
- 512KB SRAM, 8MB PSRAM (on most modules)
- 802.11 b/g/n WiFi with full promiscuous mode support
- Hardware I²C (two buses), multiple hardware UART
- Do **not** use ESP32-C3 — single antenna, weaker promiscuous mode support

### 3.2 Bill of Materials

| Component | Part | Interface | Notes |
|---|---|---|---|
| MCU | ESP32-S3-DevKitC-1 | — | Or equivalent S3 module |
| GPS | u-blox NEO-M9N or M8N | UART | 10Hz fix rate, SMA antenna connector |
| IMU | BNO085 (Adafruit breakout) | I²C | Integrated sensor fusion, 400kHz I²C |
| Rangefinder (short) | VL53L1X (Adafruit breakout) | I²C | Up to 4m; shares I²C bus with BNO085 |
| Rangefinder (long) | Benewake TF-Luna | UART or I²C | Up to 8m; use if vehicle-mounted |
| Storage | SD card module (SPI) | SPI | Optional — base node handles primary storage |
| Power | 3.7V LiPo + TP4056 charger | — | Or 5V USB power bank |
| Antenna | 2.4GHz external SMA | — | Improves range over PCB trace antenna |

### 3.3 Pin Assignment (ESP32-S3-DevKitC-1)

| Signal | GPIO | Notes |
|---|---|---|
| GPS TX → ESP RX | GPIO 16 | UART1 RX |
| GPS RX → ESP TX | GPIO 17 | UART1 TX |
| I²C SDA (BNO085 + VL53L1X) | GPIO 8 | 4.7kΩ pull-up to 3.3V |
| I²C SCL (BNO085 + VL53L1X) | GPIO 9 | 4.7kΩ pull-up to 3.3V |
| TF-Luna TX → ESP RX (if used) | GPIO 18 | UART2 RX — only if using TF-Luna |
| TF-Luna RX → ESP TX (if used) | GPIO 19 | UART2 TX |
| Mode input (from base node) | GPIO 4 | Active LOW, internal pull-up, or via ESP-NOW |
| Status LED | GPIO 2 | Built-in on DevKitC |
| SD CS (optional) | GPIO 10 | SPI CS |
| SD MOSI | GPIO 11 | SPI |
| SD MISO | GPIO 13 | SPI |
| SD SCK | GPIO 12 | SPI |

**I²C address conflicts:** BNO085 default I²C address is `0x4A`. VL53L1X default is `0x29`. No conflict. If using two VL53L1X sensors, use the XSHUT pin to assign a second address.

---

## 4. Software Architecture

### 4.1 Repository Structure

```
firmware/esp32-sensor/
├── platformio.ini
├── src/
│   ├── main.cpp           — setup(), loop(), mode state machine
│   ├── wifi_scan.cpp/.h   — promiscuous mode, frame parsing, channel hop
│   ├── gps_reader.cpp/.h  — NMEA parse via TinyGPS++, fix quality check
│   ├── imu.cpp/.h         — BNO085 init, heading read, lock logic
│   ├── rangefinder.cpp/.h — VL53L1X or TF-Luna read
│   ├── espnow_tx.cpp/.h   — ESP-NOW peer registration, send queue
│   └── config.h           — node_id, peer MAC, compile-time constants
└── lib/                   — (PlatformIO manages dependencies via platformio.ini)
```

Shared with base node (symlink or copy):
```
shared/packet_schema.h     — WardrivingRecord struct definition
```

### 4.2 Task Architecture (FreeRTOS)

The ESP32 runs FreeRTOS. Use separate tasks for each subsystem to avoid blocking the WiFi callback:

| Task | Core | Priority | Stack | Function |
|---|---|---|---|---|
| `wifi_sniffer_task` | Core 0 | 5 | 4096 | Hops channels, receives frames via queue |
| `gps_task` | Core 1 | 4 | 4096 | Reads UART1, feeds TinyGPS++, updates fix |
| `imu_task` | Core 1 | 3 | 2048 | Polls BNO085, updates heading |
| `range_task` | Core 1 | 3 | 2048 | Polls rangefinder every 100ms |
| `espnow_tx_task` | Core 1 | 6 | 4096 | Drains send queue, transmits records |
| `main` / `loop()` | Core 1 | 1 | 4096 | Mode state machine, watchdog |

Use a `QueueHandle_t record_queue` (depth 32) to pass `WardrivingRecord` structs from the WiFi sniffer callback to the ESP-NOW TX task.

**Why separate cores?** The WiFi subsystem prefers Core 0. Running GPS, IMU, and rangefinder on Core 1 avoids contention.

### 4.3 State Machine

```
INIT → WAIT_FIX → SCANNING
                       │
              ┌────────┴────────┐
           MODE_A           MODE_B
         (radius)           (cone)
                               │
                         CONE_LOCKED
                        (heading saved)
```

Transitions:
- `INIT → WAIT_FIX`: After peripherals initialized, wait for GPS HDOP < 3.0 and at least 4 satellites
- `WAIT_FIX → SCANNING`: GPS fix acquired
- `SCANNING → MODE_A / MODE_B`: On mode command received via ESP-NOW from base node
- `MODE_B → CONE_LOCKED`: On lock command (physical button or ESP-NOW command); saves `H_lock` from current IMU heading

### 4.4 Channel Hopping

WiFi operates on channels 1–13 (2.4GHz). An AP only transmits beacons on its home channel, so you must hop through all channels to discover all networks.

Implement a timer-driven hop every **200ms**:

```c
const uint8_t channels[] = {1,2,3,4,5,6,7,8,9,10,11,12,13};
uint8_t ch_idx = 0;

void channel_hop_timer_cb(void* arg) {
    ch_idx = (ch_idx + 1) % 13;
    esp_wifi_set_channel(channels[ch_idx], WIFI_SECOND_CHAN_NONE);
}
```

200ms per channel × 13 channels = 2.6 seconds per full sweep. This is the detection latency floor — a network can be missed for up to 2.6 seconds if you're on the wrong channel.

In cone mode, consider dwelling longer on channels where you've already seen a target device's beacon.

---

## 5. Implementation Details

### 5.1 `config.h`

```c
#pragma once

// Unique ID for this node. Change per device at compile time.
#define NODE_ID  0x01  // 0x01, 0x02, 0x03 etc.

// MAC address of the Arduino R4 base node (fill in after flashing R4)
#define BASE_NODE_MAC  { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF }

// Cone half-angle in degrees
#define CONE_HALF_ANGLE_DEG  30.0f

// Minimum GPS quality to begin scanning
#define MIN_SATELLITES  4
#define MAX_HDOP        3.0f

// Channel dwell time in milliseconds
#define CHANNEL_DWELL_MS  200

// RSSI floor — detections weaker than this are discarded (noise filter)
#define RSSI_FLOOR_DBM  -90

// ESP-NOW send queue depth
#define RECORD_QUEUE_DEPTH  32
```

### 5.2 `packet_schema.h` (shared with base node — do not modify unilaterally)

```c
// shared/packet_schema.h
// Shared between ESP32 sensor node and Arduino R4 base node.
// DO NOT modify without coordinating with the other firmware owner.

#pragma once
#include <stdint.h>

#pragma pack(1)
typedef struct {
    uint8_t  node_id;           // Which sensor node captured this
    uint8_t  mode;              // 0 = radius, 1 = cone
    uint8_t  in_cone;           // Cone mode: 1 if within ±30°, 0 if outside (always 1 in radius mode)
    uint32_t timestamp_unix;    // UTC seconds since epoch
    uint16_t timestamp_ms;      // Milliseconds part (0–999)
    double   latitude;          // Decimal degrees, WGS84
    double   longitude;         // Decimal degrees, WGS84
    float    altitude_m;        // Metres above sea level
    uint8_t  hdop_x10;          // GPS HDOP × 10 (e.g., 12 = HDOP 1.2)
    uint8_t  satellites;        // Number of GPS satellites in fix
    uint8_t  bssid[6];          // Raw MAC address (6 bytes)
    char     ssid[33];          // Null-terminated, max 32 chars; empty = hidden
    int8_t   rssi;              // Signal strength in dBm (negative)
    uint8_t  channel;           // WiFi channel (1–13)
    uint8_t  enc_type;          // 0=open, 1=WEP, 2=WPA, 3=WPA2, 4=WPA3, 5=WPA2/WPA3
    float    bearing_deg;       // IMU heading at moment of detection (-1.0 if radius mode)
    float    range_m;           // Rangefinder distance (-1.0 if not active or radius mode)
    float    h_lock_deg;        // Cone lock heading (-1.0 if radius mode)
} WardrivingRecord;             // Total: ~87 bytes — within ESP-NOW 250-byte limit
#pragma pack()

// Mode constants
#define MODE_RADIUS  0
#define MODE_CONE    1

// Encryption type constants
#define ENC_OPEN   0
#define ENC_WEP    1
#define ENC_WPA    2
#define ENC_WPA2   3
#define ENC_WPA3   4
#define ENC_WPA2E  5  // WPA2/WPA3 Enterprise or mixed
```

### 5.3 WiFi Initialization for Simultaneous Promiscuous + ESP-NOW

This is the trickiest part of the firmware. ESP-NOW and promiscuous mode both use the WiFi radio, and they must be configured in the right order:

```c
void wifi_init() {
    // 1. Initialize NVS (required by WiFi stack)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // 2. Init network interface and event loop
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    // 3. WiFi config — STA mode is required for ESP-NOW
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    // 4. Set channel (start on ch 1; timer will hop)
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

    // 5. Initialize ESP-NOW (must be AFTER esp_wifi_start)
    esp_now_init();
    esp_now_register_send_cb(espnow_send_cb);

    // 6. Register base node as ESP-NOW peer
    esp_now_peer_info_t peer = {};
    uint8_t base_mac[] = BASE_NODE_MAC;
    memcpy(peer.peer_addr, base_mac, 6);
    peer.channel = 0;        // 0 = use current channel
    peer.encrypt = false;
    esp_now_add_peer(&peer);

    // 7. Enable promiscuous mode LAST
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_filter(&filt);  // filter to management frames only
    esp_wifi_set_promiscuous_rx_cb(&wifi_sniffer_cb);
}
```

**Important:** Set `WIFI_PROMIS_FILTER_MASK_MGMT` in the filter to only receive management frames. Receiving data frames too burns CPU and fills the queue with irrelevant traffic.

### 5.4 WiFi Sniffer Callback

```c
void wifi_sniffer_cb(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;

    wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    uint8_t* frame = pkt->payload;
    uint8_t subtype = (frame[0] >> 4) & 0x0F;

    // Only process beacon (8) and probe response (5) frames
    if (subtype != 0x08 && subtype != 0x05) return;

    // Minimum sanity check on frame length
    if (pkt->rx_ctrl.sig_len < 38) return;

    // RSSI floor filter
    if (pkt->rx_ctrl.rssi < RSSI_FLOOR_DBM) return;

    WardrivingRecord rec = {};
    rec.node_id  = NODE_ID;
    rec.rssi     = (int8_t)pkt->rx_ctrl.rssi;
    rec.channel  = pkt->rx_ctrl.channel;

    // BSSID is at bytes 16–21 of the 802.11 MAC header
    memcpy(rec.bssid, frame + 16, 6);

    // Fixed-length fields: 24 bytes MAC header + 8 bytes timestamp +
    // 2 bytes interval + 2 bytes capability = offset 36 for tagged params
    parse_beacon_ies(frame + 36, pkt->rx_ctrl.sig_len - 36, &rec);

    // Populate GPS, IMU, rangefinder from shared globals (updated by other tasks)
    populate_sensor_fields(&rec);

    // Push to queue (from ISR context — use FromISR variant)
    xQueueSendFromISR(record_queue, &rec, NULL);
}
```

### 5.5 Beacon IE Parser

```c
void parse_beacon_ies(uint8_t* ies, int len, WardrivingRecord* rec) {
    int i = 0;
    bool has_rsn = false;
    bool has_wpa = false;

    while (i < len - 2) {
        uint8_t tag_id  = ies[i];
        uint8_t tag_len = ies[i + 1];
        uint8_t* tag_data = ies + i + 2;

        if (i + 2 + tag_len > len) break;  // Malformed, stop parsing

        switch (tag_id) {
            case 0x00:  // SSID
                if (tag_len > 0 && tag_len <= 32) {
                    memcpy(rec->ssid, tag_data, tag_len);
                    rec->ssid[tag_len] = '\0';
                }
                break;

            case 0x03:  // DS Parameter Set — current channel
                if (tag_len >= 1) rec->channel = tag_data[0];
                break;

            case 0x30:  // RSN — WPA2 or WPA3
                has_rsn = true;
                // Check for SAE (WPA3): RSN AKM suite 00-0F-AC:8
                // Simplified: if RSN present, assume WPA2 minimum
                rec->enc_type = ENC_WPA2;
                break;

            case 0xDD:  // Vendor Specific
                // WPA OUI: 00:50:F2:01
                if (tag_len >= 4 &&
                    tag_data[0] == 0x00 && tag_data[1] == 0x50 &&
                    tag_data[2] == 0xF2 && tag_data[3] == 0x01) {
                    has_wpa = true;
                }
                break;
        }

        i += 2 + tag_len;
    }

    // Determine encryption type
    if (!has_rsn && !has_wpa) {
        rec->enc_type = ENC_OPEN;  // May still be WEP — check capability bits separately
    } else if (has_wpa && !has_rsn) {
        rec->enc_type = ENC_WPA;
    }
    // ENC_WPA2 / ENC_WPA3 already set above if RSN found
}
```

### 5.6 `platformio.ini`

```ini
[env:esp32s3]
platform  = espressif32
board     = esp32-s3-devkitc-1
framework = arduino

lib_deps =
    mikalhart/TinyGPSPlus @ ^1.0.3
    adafruit/Adafruit BNO08x @ ^1.2.3
    adafruit/Adafruit VL53L1X @ ^3.1.0
    adafruit/Adafruit BusIO @ ^1.16.1

build_flags =
    -DNODE_ID=1
    -DBOARD_HAS_PSRAM
    -mfix-esp32-psram-cache-issue

monitor_speed = 115200
upload_speed  = 921600
```

Change `-DNODE_ID=1` to `2`, `3`, etc. for additional nodes without touching source code.

---

## 6. GPS Task

```c
HardwareSerial gpsSerial(1);  // UART1
TinyGPSPlus gps;

// Shared fix struct (written by gps_task, read by sniffer callback)
struct GpsFix {
    double lat, lon, alt;
    uint32_t unix_time;
    uint16_t ms;
    uint8_t  hdop_x10;
    uint8_t  satellites;
    bool     valid;
} current_fix = {};
SemaphoreHandle_t fix_mutex;

void gps_task(void* arg) {
    gpsSerial.begin(115200, SERIAL_8N1, 16, 17);  // RX=16, TX=17
    fix_mutex = xSemaphoreCreateMutex();

    while (true) {
        while (gpsSerial.available()) {
            gps.encode(gpsSerial.read());
        }

        if (gps.location.isUpdated() && gps.location.isValid()) {
            xSemaphoreTake(fix_mutex, portMAX_DELAY);
            current_fix.lat        = gps.location.lat();
            current_fix.lon        = gps.location.lng();
            current_fix.alt        = gps.altitude.meters();
            current_fix.satellites = gps.satellites.value();
            current_fix.hdop_x10   = (uint8_t)(gps.hdop.hdop() * 10);
            current_fix.valid      = (current_fix.satellites >= MIN_SATELLITES &&
                                      gps.hdop.hdop() <= MAX_HDOP);
            // Build Unix timestamp from GPS date + time
            // (use a helper or store raw GPS time and convert)
            xSemaphoreGive(fix_mutex);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

---

## 7. IMU Task (BNO085)

```c
#include <Adafruit_BNO08x.h>
Adafruit_BNO08x bno;
sh2_SensorValue_t imu_val;
float current_heading_deg = 0.0f;
float h_lock_deg = -1.0f;
SemaphoreHandle_t imu_mutex;

void imu_task(void* arg) {
    imu_mutex = xSemaphoreCreateMutex();
    Wire.begin(8, 9);  // SDA=8, SCL=9
    bno.begin_I2C(0x4A, &Wire);
    bno.enableReport(SH2_ROTATION_VECTOR, 10000);  // 10ms = 100Hz

    while (true) {
        if (bno.getSensorEvent(&imu_val)) {
            if (imu_val.sensorId == SH2_ROTATION_VECTOR) {
                // Convert quaternion to yaw (heading)
                float qw = imu_val.un.rotationVector.real;
                float qx = imu_val.un.rotationVector.i;
                float qy = imu_val.un.rotationVector.j;
                float qz = imu_val.un.rotationVector.k;

                float yaw = atan2f(2.0f * (qw * qz + qx * qy),
                                   1.0f - 2.0f * (qy * qy + qz * qz));
                float heading = fmodf(yaw * 180.0f / M_PI + 360.0f, 360.0f);

                xSemaphoreTake(imu_mutex, portMAX_DELAY);
                current_heading_deg = heading;
                xSemaphoreGive(imu_mutex);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void cone_lock() {
    xSemaphoreTake(imu_mutex, portMAX_DELAY);
    h_lock_deg = current_heading_deg;
    xSemaphoreGive(imu_mutex);
}
```

---

## 8. Cone Filter Logic

Called in `populate_sensor_fields()` after all sensor values are read:

```c
void apply_cone_logic(WardrivingRecord* rec) {
    if (rec->mode == MODE_RADIUS) {
        rec->in_cone     = 1;
        rec->bearing_deg = -1.0f;
        rec->range_m     = -1.0f;
        rec->h_lock_deg  = -1.0f;
        return;
    }

    rec->bearing_deg = current_heading_deg;
    rec->range_m     = current_range_m;
    rec->h_lock_deg  = h_lock_deg;

    float delta = fmodf(fabsf(current_heading_deg - h_lock_deg), 360.0f);
    if (delta > 180.0f) delta = 360.0f - delta;
    rec->in_cone = (delta <= CONE_HALF_ANGLE_DEG) ? 1 : 0;
}
```

---

## 9. ESP-NOW Transmit Task

```c
void espnow_tx_task(void* arg) {
    WardrivingRecord rec;
    uint8_t base_mac[] = BASE_NODE_MAC;

    while (true) {
        if (xQueueReceive(record_queue, &rec, pdMS_TO_TICKS(100)) == pdTRUE) {
            esp_now_send(base_mac, (uint8_t*)&rec, sizeof(WardrivingRecord));
        }
    }
}

void espnow_send_cb(const uint8_t* mac, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) {
        // Optionally increment a dropped_count counter for diagnostics
    }
}
```

---

## 10. Testing Without the Base Node

You can test this firmware entirely independently of the Arduino R4. Replace the ESP-NOW transmit with a serial dump:

```c
void debug_print_record(const WardrivingRecord* r) {
    Serial.printf("[NODE %02X] BSSID=%02X:%02X:%02X:%02X:%02X:%02X "
                  "SSID='%s' RSSI=%d ch=%d enc=%d "
                  "lat=%.6f lon=%.6f hdop=%.1f "
                  "bearing=%.1f range=%.1f in_cone=%d\n",
        r->node_id,
        r->bssid[0],r->bssid[1],r->bssid[2],
        r->bssid[3],r->bssid[4],r->bssid[5],
        r->ssid, r->rssi, r->channel, r->enc_type,
        r->latitude, r->longitude, (float)r->hdop_x10 / 10.0f,
        r->bearing_deg, r->range_m, r->in_cone);
}
```

Enable this by defining `DEBUG_SERIAL_ONLY` in `config.h`. When that flag is set, skip `esp_now_send()` and call `debug_print_record()` instead.

To test cone filtering, a Python script on a laptop can feed synthetic IMU headings over serial to verify the ±30° window logic before IMU hardware is available.

---

## 11. Known Limitations and Notes

- **ESP-NOW channel constraint:** ESP-NOW transmits on the current WiFi channel. When the sniffer hops channels, ESP-NOW hops with it. The base node must also be on a compatible channel, or set to listen on all channels. In practice, configure the base node to not hop (stay on ch 6) and set the sensor node's ESP-NOW channel to 6 separately from its promiscuous channel.
- **Clock drift:** The ESP32 has no RTC. GPS provides accurate UTC time, but only after a fix. Log a `fix_valid` flag; timestamps before fix are unreliable.
- **MAC randomization:** Modern phones randomize their probe request MAC addresses. Client MAC logging is therefore of limited use for tracking individual phones; BSSID (AP MAC) logging remains reliable.
- **5GHz:** The ESP32's WiFi radio is 2.4GHz only. 5GHz networks will not appear.
