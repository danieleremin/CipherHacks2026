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
#include "config.h"
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
    String bssid_s = jsonGet(line, "bssid");
    String node_s  = jsonGet(line, "node");
    String rssi_s  = jsonGet(line, "rssi");

    if (bssid_s.length() == 0 || node_s.length() == 0) return;

    uint8_t node_id = (uint8_t)node_s.toInt();
    int8_t  rssi    = (int8_t)rssi_s.toInt();

    if (node_id != 1 && node_id != 2) return;

    // Timestamp with the R4's own clock — NOT the scanner's uptime_ms.
    // node1 and node2 are separate boards with independent uptime clocks,
    // so their uptime_ms values are not comparable for correlation or
    // expiry. Records arrive over serial in near-real-time, so millis()
    // is the correct shared time base. (expire() below also uses it.)
    g_bearing.update(bssid_s.c_str(), node_id, rssi, millis());
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

    // ── Read lines from node3 (non-blocking) ──────────────────────────────
    // Accumulate one line char-at-a-time so loop() never stalls on a partial
    // line (unlike readStringUntil, which blocks up to the Stream timeout and
    // would starve web_export_poll() / the WebSocket keep-alive).
    static char   line_buf[MAX_LINE_LEN];
    static size_t line_len = 0;
    while (Serial1.available()) {
        char c = (char)Serial1.read();
        if (c != '\n') {
            if (line_len < MAX_LINE_LEN - 1) line_buf[line_len++] = c;
            else line_len = 0;   // oversized line — drop and resync on next '\n'
            continue;
        }
        if (line_len > 0 && line_buf[line_len - 1] == '\r') line_len--;  // strip CR
        line_buf[line_len] = '\0';
        if (line_len > 0) {
            if (line_buf[0] == '{') {
                processDetection(String(line_buf));
            } else {
                // Diagnostic line from node3 — log to USB Serial only
                Serial.print("[NODE3] "); Serial.println(line_buf);
            }
        }
        line_len = 0;
    }

    // ── Emit bearing estimate ─────────────────────────────────────────────
    if (now - g_last_bearing_emit >= BEARING_EMIT_MS) {
        g_last_bearing_emit = now;
        emitBearing();
    }

    // ── Expire stale AP records ───────────────────────────────────────────
    if (now - g_last_expire >= EXPIRE_INTERVAL_MS) {
        g_last_expire = now;
        // Both update() and expire() use the R4's millis() clock
        // (see processDetection), so record ages are well-defined.
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