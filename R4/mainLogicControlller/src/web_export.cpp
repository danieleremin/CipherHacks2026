// firmware/arduino-r4-base/src/web_export.cpp

#include <Arduino.h>
#include <WiFiS3.h>
#include <string.h>
#include <UnoR4WiFi_WebServer.h>   // pulls in net::WebSocketServer / net::WebSocket
#include "web_export.h"
#include "config.h"

// We talk to net::WebSocketServer directly instead of going through
// UnoR4WiFi_WebServer::enableWebSocket(). That wrapper's begin() bails unless
// WiFi.status() == WL_CONNECTED, which never holds in AP mode (status is
// WL_AP_LISTENING / WL_AP_CONNECTED). Driving the server ourselves lets the
// WebSocket listen on our own access point.
static net::WebSocketServer ws(WS_PORT);

// Count of detection lines forwarded to clients — reported in the hello
// message sent on connect.
static uint32_t records_forwarded = 0;

static void on_ws_message(net::WebSocket& conn, const net::WebSocket::DataType dataType,
                           const char* message, uint16_t length) {
    // Records are one-way (device -> frontend); nothing to do with inbound text yet.
}

static void on_ws_close(net::WebSocket& conn, const net::WebSocket::CloseCode code,
                         const char* reason, uint16_t length) {
    Serial.println("[web] client disconnected");
}

static void on_ws_open(net::WebSocket& conn) {
    Serial.println("[web] client connected");
    conn.onMessage(on_ws_message);
    conn.onClose(on_ws_close);

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

bool web_export_init() {
    if (WiFi.beginAP(WIFI_AP_SSID, WIFI_AP_PASSWORD) != WL_AP_LISTENING) {
        return false;
    }

    ws.onConnection(on_ws_open);
    ws.begin();

    Serial.print("[web] AP \"");
    Serial.print(WIFI_AP_SSID);
    Serial.print("\" up, ws://");
    Serial.print(WiFi.localIP());
    Serial.print(':');
    Serial.println(WS_PORT);

    return true;
}

void web_export_poll() {
    ws.listen();
}

void web_export_broadcast_line(const char* line) {
    ws.broadcast(net::WebSocket::DataType::TEXT, line, (uint16_t)strlen(line));
    records_forwarded++;
}
