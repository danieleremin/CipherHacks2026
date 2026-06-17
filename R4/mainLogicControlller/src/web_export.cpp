// firmware/arduino-r4-base/src/web_export.cpp

#include <Arduino.h>
#include <WiFiS3.h>
#include <UnoR4WiFi_WebServer.h>
#include "web_export.h"
#include "config.h"

static UnoR4WiFi_WebServer server;
static UnoR4WiFi_WebSocket* ws = nullptr;

static void handle_status(WiFiClient& client, const String& method, const String& request,
                           const QueryParams& params, const String& jsonData) {
    server.sendResponse(client, "Wardriver R4 — connect a WebSocket client to /", "text/plain");
}

static void on_ws_open(net::WebSocket& conn) {
    Serial.println("[web] client connected");
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

    char json[256];
    snprintf(json, sizeof(json),
        "{\"node_id\":%u,\"mode\":%u,\"in_cone\":%u,\"uptime_ms\":%lu,"
        "\"bssid\":\"%s\",\"ssid\":\"%s\",\"rssi\":%d,\"channel\":%u,"
        "\"enc_type\":%u,\"bearing_deg\":%.1f,\"range_m\":%.1f,\"h_lock_deg\":%.1f}",
        r->node_id, r->mode, r->in_cone, (unsigned long)r->uptime_ms,
        bssid_str, ssid_esc, r->rssi, r->channel,
        r->enc_type, r->bearing_deg, r->range_m, r->h_lock_deg);

    ws->broadcastTXT(json);
}
