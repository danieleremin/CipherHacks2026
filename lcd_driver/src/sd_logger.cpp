// firmware/lcd-driver/src/sd_logger.cpp
//
// GPS was removed from the sensor nodes' WardrivingRecord (see
// ../../shared/packet_schema.h), but the WiGLE CSV format requires a
// position and timestamp per row. Until GPS is reintroduced, position
// columns use the fixed PLACEHOLDER_* constants from config.h, and the
// timestamp is a fake date with a real time-of-day derived from the
// record's uptime_ms (relative to the sending sensor node's boot, not
// wall-clock — good enough to eyeball ordering, not for WiGLE upload).

#include <SD.h>
#include <SPI.h>
#include "sd_logger.h"
#include "config.h"

static File wigle_file;
static File ext_file;

bool sd_logger_init() {
    if (!SD.begin(SD_CS_PIN)) return false;

    bool wigle_exists = SD.exists(WIGLE_CSV_FILENAME);
    bool ext_exists   = SD.exists(EXTENDED_CSV_FILENAME);

    wigle_file = SD.open(WIGLE_CSV_FILENAME, FILE_WRITE);
    ext_file   = SD.open(EXTENDED_CSV_FILENAME, FILE_WRITE);

    if (!wigle_file || !ext_file) return false;

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

// Channel -> frequency conversion (2.4GHz band)
static uint16_t channel_to_mhz(uint8_t ch) {
    return (ch == 14) ? 2484 : (2407 + ch * 5);
}

// Encryption type -> WiGLE AuthMode string
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

// Fake date + real time-of-day derived from uptime_ms (no RTC, no GPS clock)
static void fmt_timestamp(uint32_t uptime_ms, char* buf) {
    uint32_t total_s = uptime_ms / 1000;
    uint32_t s = total_s % 60; total_s /= 60;
    uint32_t m = total_s % 60; total_s /= 60;
    uint32_t h = total_s % 24;
    snprintf(buf, 20, "2024-01-01 %02lu:%02lu:%02lu",
             (unsigned long)h, (unsigned long)m, (unsigned long)s);
}

static void format_mac(const uint8_t* bssid, char* out) {
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
}

static uint16_t flush_counter = 0;

static void maybe_flush() {
    if (++flush_counter % 10 == 0) {
        wigle_file.flush();
        ext_file.flush();
    }
}

void sd_logger_write_wigle(const WardrivingRecord* r) {
    char ts[20];
    fmt_timestamp(r->uptime_ms, ts);

    char mac_str[18];
    format_mac(r->bssid, mac_str);

    wigle_file.print(mac_str);                    wigle_file.print(',');
    wigle_file.print(r->ssid);                     wigle_file.print(',');
    wigle_file.print(enc_to_str(r->enc_type));      wigle_file.print(',');
    wigle_file.print(ts);                           wigle_file.print(',');
    wigle_file.print(r->channel);                   wigle_file.print(',');
    wigle_file.print(channel_to_mhz(r->channel));   wigle_file.print(',');
    wigle_file.print(r->rssi);                      wigle_file.print(',');
    wigle_file.print(PLACEHOLDER_LATITUDE, 6);      wigle_file.print(',');
    wigle_file.print(PLACEHOLDER_LONGITUDE, 6);     wigle_file.print(',');
    wigle_file.print(PLACEHOLDER_ALTITUDE_M, 1);    wigle_file.print(',');
    wigle_file.print(0.0f, 1);  /* AccuracyMeters */ wigle_file.print(',');
    wigle_file.println("WIFI");

    maybe_flush();
}

void sd_logger_write_extended(const WardrivingRecord* r, const char* manufacturer) {
    char ts[20];
    fmt_timestamp(r->uptime_ms, ts);

    char mac_str[18];
    format_mac(r->bssid, mac_str);

    ext_file.print(mac_str);                   ext_file.print(',');
    ext_file.print(r->ssid);                    ext_file.print(',');
    ext_file.print(enc_to_str(r->enc_type));     ext_file.print(',');
    ext_file.print(ts);                          ext_file.print(',');
    ext_file.print(r->channel);                  ext_file.print(',');
    ext_file.print(channel_to_mhz(r->channel));  ext_file.print(',');
    ext_file.print(r->rssi);                     ext_file.print(',');
    ext_file.print(PLACEHOLDER_LATITUDE, 6);     ext_file.print(',');
    ext_file.print(PLACEHOLDER_LONGITUDE, 6);    ext_file.print(',');
    ext_file.print(PLACEHOLDER_ALTITUDE_M, 1);   ext_file.print(',');
    ext_file.print(0.0f, 1);  /* AccuracyMeters */ ext_file.print(',');
    ext_file.print("WIFI");                      ext_file.print(',');
    ext_file.print(r->node_id);                  ext_file.print(',');
    ext_file.print(manufacturer);                ext_file.print(',');
    ext_file.print(r->bearing_deg, 1);            ext_file.print(',');
    ext_file.print(r->range_m, 1);                ext_file.print(',');
    ext_file.print(r->in_cone);                   ext_file.print(',');
    ext_file.print(r->h_lock_deg, 1);             ext_file.print(',');
    ext_file.print(0.0f, 1);  /* HDOP — no GPS */ ext_file.print(',');
    ext_file.println(0);      /* Satellites — no GPS */

    maybe_flush();
}
