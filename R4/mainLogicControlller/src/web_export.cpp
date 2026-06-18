// firmware/arduino-r4-base/src/web_export.cpp

#include <Arduino.h>
#include <WiFiS3.h>
#include <UnoR4WiFi_WebServer.h>
#include "web_export.h"
#include "config.h"

static UnoR4WiFi_WebServer server;
static UnoR4WiFi_WebSocket* ws = nullptr;

// Count of detection lines forwarded to clients — reported in the hello
// message sent on connect.
static uint32_t records_forwarded = 0;

static void handle_status(WiFiClient& client, const String& method, const String& request,
                           const QueryParams& params, const String& jsonData) {
    server.sendResponse(client, "Wardriver R4 — connect a WebSocket client to /", "text/plain");
}

static void on_ws_open(net::WebSocket& conn) {
    Serial.println("[web] client connected");

    // Greet the client with a hello frame carrying the R4's IP and the number
    // of records forwarded so far. The frontend keys on "type":"hello" to
    // distinguish this from detection records.
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

void web_export_broadcast_line(const char* line) {
    if (ws == nullptr) return;
    ws->broadcastTXT(line);
    records_forwarded++;
}
