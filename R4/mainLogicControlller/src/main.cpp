// R4/mainLogicControlller/src/main.cpp
// Arduino R4 WiFi — JSON relay + multi-AP bearing estimator.
//
// Primary job: read JSON lines from node3 over Serial1 (pin 0 RX),
// forward every line starting with '{' to WebSocket clients via web_export,
// and feed detection records into the MultiApBearing correlator.
//
// Every BEARING_EMIT_MS a bearing estimate message is pushed to all
// WebSocket clients alongside the forwarded detection records:
//   {"type":"bearing","bearing":127.4,"confidence":0.82,"ap_count":6,"delta_avg":7.2}
//
// Non-JSON diagnostic lines from node3 (anything not starting with '{')
// are echoed to USB Serial prefixed with [NODE3] and not forwarded.
//
// Modules:
//   web_export  — WiFi AP + WebSocket server (broadcast, hello message)
//   led_matrix  — onboard 12x8 LED matrix flash on each forwarded record
//   multi_ap_bearing — differential RSSI correlator

#include <Arduino.h>
#include "web_export.h"
#include "led_matrix.h"
#include "multi_ap_bearing.h"

// ── Config ─────────────────────────────────────────────────────────────────
#define SERIAL1_BAUD        115200   // Must match node3 Serial baud rate
#define BEARING_EMIT_MS     500      // How often to push bearing to clients
#define EXPIRE_INTERVAL_MS  5000     // How often to expire stale AP records
#define STATUS_INTERVAL_MS  10000    // USB Serial diagnostic interval

// ── Globals ────────────────────────────────────────────────────────────────
static MultiApBearing g_bearing;

static uint32_t g_records_forwarded = 0;
static uint32_t g_last_bearing_emit = 0;
static uint32_t g_last_expire       = 0;
static uint32_t g_last_status       = 0;

// ── JSON field parser ──────────────────────────────────────────────────────
// Extracts a single field value from a flat JSON line by key name.
// Handles quoted strings and bare numbers. Returns empty string if not found.
// Does not handle nested objects — the detection record format is flat so
// this is sufficient.

static String jsonGet(const String& json, const char* key) {
    String search = String("\"") + key + "\":";
    int idx = json.indexOf(search);
    if (idx < 0) return "";
    idx += search.length();
    if (idx >= (int)json.length()) return "";

    if (json[idx] == '"') {
        int end = json.indexOf('"', idx + 1);
        if (end < 0) return "";
        return json.substring(idx + 1, end);
    }
    int end = idx;
    while (end < (int)json.length() &&
           json[end] != ',' && json[end] != '}') end++;
    return json.substring(idx, end);
}

// ── Bearing emit ───────────────────────────────────────────────────────────

static void emitBearing() {
    BearingEstimate est = g_bearing.compute();
    if (!est.valid) return;

    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"bearing\","
        "\"bearing\":%.1f,"
        "\"confidence\":%.2f,"
        "\"ap_count\":%d,"
        "\"delta_avg\":%.1f}",
        est.bearing_deg,
        est.confidence,
        (int)est.ap_count,
        est.rssi_delta_avg
    );
    web_export_broadcast_line(buf);
}

// ── Process one JSON detection line ───────────────────────────────────────

static void processDetection(const String& line) {
    // Forward verbatim to all WebSocket clients
    web_export_broadcast_line(line.c_str());
    g_records_forwarded++;

    // Flash LED matrix
    led_matrix_update(true);

    // Parse fields needed for bearing correlator
    String bssid_s  = jsonGet(line, "bssid");
    String node_s   = jsonGet(line, "node");
    String rssi_s   = jsonGet(line, "rssi");
    String uptime_s = jsonGet(line, "uptime");

    if (bssid_s.length() == 0 || node_s.length() == 0) return;

    uint8_t  node_id  = (uint8_t)node_s.toInt();
    int8_t   rssi     = (int8_t)rssi_s.toInt();
    uint32_t uptime   = (uint32_t)uptime_s.toInt();

    if (node_id != 1 && node_id != 2) return;

    g_bearing.update(bssid_s.c_str(), node_id, rssi, uptime);
}

// ── setup() ────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("Wardriver R4 ready — relaying node3 JSON to WebSocket.");

    // UART from node3 (pin 0 RX1)
    Serial1.begin(SERIAL1_BAUD);

    // WiFi AP + WebSocket server
    web_export_init();

    // LED matrix
    led_matrix_init();

    Serial.print("[main] bearing correlator ready — max ");
    Serial.print(MAX_TRACKED_APS);
    Serial.println(" APs");
}

// ── loop() ─────────────────────────────────────────────────────────────────

void loop() {
    uint32_t now = millis();

    // ── Accept and poll WebSocket clients ─────────────────────────────────
    web_export_poll();

    // ── Read lines from node3 ─────────────────────────────────────────────
    while (Serial1.available()) {
        String line = Serial1.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        if (line.charAt(0) == '{') {
            processDetection(line);
        } else {
            // Diagnostic line from node3 — log to USB Serial only
            Serial.print("[NODE3] "); Serial.println(line);
        }
    }

    // ── Emit bearing estimate ─────────────────────────────────────────────
    if (now - g_last_bearing_emit >= BEARING_EMIT_MS) {
        g_last_bearing_emit = now;
        emitBearing();
    }

    // ── Expire stale AP records ───────────────────────────────────────────
    if (now - g_last_expire >= EXPIRE_INTERVAL_MS) {
        g_last_expire = now;
        // Use millis() as proxy for uptime — the correlator only cares
        // about relative age, not absolute time
        g_bearing.expire(now);
    }

    // ── Periodic USB Serial status ────────────────────────────────────────
    if (now - g_last_status >= STATUS_INTERVAL_MS) {
        g_last_status = now;

        BearingEstimate est = g_bearing.compute();

        Serial.print("[main] uptime="); Serial.print(now/1000);
        Serial.print("s fwd="); Serial.print(g_records_forwarded);
        Serial.print(" tracked="); Serial.print(g_bearing.tableSize());
        Serial.print(" correlated="); Serial.print(g_bearing.correlatedCount());
        Serial.print(" bearing=");

        if (est.valid) {
            Serial.print(est.bearing_deg, 1); Serial.print("deg conf=");
            Serial.print(est.confidence, 2); Serial.print(" ap=");
            Serial.println(est.ap_count);
        } else {
            Serial.println("n/a (need 2+ correlated APs)");
        }
    }

    // ── LED matrix update ─────────────────────────────────────────────────
    led_matrix_update(false);
}