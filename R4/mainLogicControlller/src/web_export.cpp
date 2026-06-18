// firmware/arduino-r4-base/src/web_export.cpp

#include <Arduino.h>
#include <WiFiS3.h>
#include <UnoR4WiFi_WebServer.h>
#include "web_export.h"
#include "config.h"

static UnoR4WiFi_WebServer server;
static UnoR4WiFi_WebSocket* ws = nullptr;

// Count of detection records forwarded to clients — reported in the hello
// message (see serialUsbSpec.md §4.4).
static uint32_t records_forwarded = 0;

static void handle_status(WiFiClient& client, const String& method, const String& request,
                           const QueryParams& params, const String& jsonData) {
    server.sendResponse(client, "Wardriver R4 — connect a WebSocket client to /", "text/plain");
}

static void on_ws_open(net::WebSocket& conn) {
    Serial.println("[web] client connected");

    // Greet the client with a hello frame carrying the R4's IP and the number
    // of records forwarded so far (serialUsbSpec.md §4.4). The frontend keys on
    // "type":"hello" to distinguish this from detection records.
    IPAddress ip = WiFi.localIP();
    char hello[96];
    int n = snprintf(hello, sizeof(hello),
        "{\"type\":\"hello\",\"ip\":\"%u.%u.%u.%u\",\"forwarded\":%lu}",
        ip[0], ip[1], ip[2], ip[3], (unsigned long)records_forwarded);
    conn.send(net::WebSocket::DataType::TEXT, hello, (uint16_t)n);
}

static void on_ws_close(net::WebSocket& conn, const net::WebSocket::CloseCode code,
                         const char* reason, uint16_t length) {
    Serial.println("[web] client disconnected");
}

static void on_ws_message(net::WebSocket& conn, const net::WebSocket::DataType dataType,
                           const char* message, uint16_t length) {
    // Records are one-way (device -> frontend); nothing to do with inbound text yet.
}

bool web_export_init() {
    if (WiFi.beginAP(WIFI_AP_SSID, WIFI_AP_PASSWORD) != WL_AP_LISTENING) {
        return false;
    }

    server.addRoute("/", handle_status);
    server.begin();  // WiFi already up via beginAP — see UnoR4WiFi_WebServer.h

    ws = server.enableWebSocket(WS_PORT);
    if (ws == nullptr) return false;

    ws->onOpen(on_ws_open);
    ws->onMessage(on_ws_message);
    ws->onClose(on_ws_close);

    Serial.print("[web] AP \"");
    Serial.print(WIFI_AP_SSID);
    Serial.print("\" up, ws://");
    Serial.print(WiFi.localIP());
    Serial.print(':');
    Serial.println(WS_PORT);

    return true;
}

void web_export_poll() {
    server.handleClient();
    server.handleWebSocket();
}

// Escapes '"' and '\' for JSON string safety (SSIDs are attacker-controlled
// over the air — never trust them to already be clean).
static void json_escape(const char* in, char* out, size_t out_size) {
    size_t o = 0;
    for (size_t i = 0; in[i] != '\0' && o + 2 < out_size; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '"' || c == '\\') {
            out[o++] = '\\';
            out[o++] = (char)c;
        } else if (c >= 0x20 && c < 0x7F) {
            out[o++] = (char)c;
        }  // drop non-printable bytes rather than emit invalid JSON
    }
    out[o] = '\0';
}

void web_export_broadcast_record(const WardrivingRecord* r) {
    if (ws == nullptr) return;

    char ssid_esc[65];
    json_escape(r->ssid, ssid_esc, sizeof(ssid_esc));

    char bssid_str[18];
    snprintf(bssid_str, sizeof(bssid_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             r->bssid[0], r->bssid[1], r->bssid[2], r->bssid[3], r->bssid[4], r->bssid[5]);

    // Field names and ordering follow serialUsbSpec.md §2.1 so the frontend
    // can consume R4 records verbatim.
    char json[256];
    snprintf(json, sizeof(json),
        "{\"node\":%u,\"schema\":%u,\"mode\":%u,\"uptime\":%lu,"
        "\"bssid\":\"%s\",\"ssid\":\"%s\",\"rssi\":%d,\"ch\":%u,"
        "\"enc\":%u,\"bearing\":%.1f,\"range\":%.2f,\"in_cone\":%u,\"h_lock\":%.1f}",
        r->node_id, r->schema_version, r->mode, (unsigned long)r->uptime_ms,
        bssid_str, ssid_esc, r->rssi, r->channel,
        r->enc_type, r->bearing_deg, r->range_m, r->in_cone, r->h_lock_deg);

    ws->broadcastTXT(json);
    records_forwarded++;
}
