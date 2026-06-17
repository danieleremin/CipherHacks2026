// firmware/lcd-driver/src/lcd_display.cpp

#include <Adafruit_GFX.h>
#include <MCUFRIEND_kbv.h>
#include <string.h>
#include "lcd_display.h"
#include "config.h"
#include "espnow_rx.h"

static MCUFRIEND_kbv tft;

void lcd_init() {
    uint16_t ID = tft.readID();
    if (ID == 0xD3D3) ID = 0x9486;  // Write-only shield
    tft.begin(ID);
    tft.setRotation(1);            // Landscape
    tft.fillScreen(0x0000);        // Black
    tft.setTextColor(0xFFFF, 0x0000);  // White on black
    tft.setTextSize(2);

    tft.setCursor(0, 0);
    tft.print("Wardriver R4");
    tft.setCursor(0, 20);
    tft.print("Initializing...");
}

void lcd_update(uint8_t mode, bool cone_locked,
                uint16_t unique_nets, uint8_t active_nodes,
                const WardrivingRecord* last_rec, const char* mfr) {

    // Row 0: Mode
    tft.setCursor(0, 0);
    if (mode == MODE_RADIUS) {
        tft.print("MODE: RADIUS    [R] ");
    } else if (cone_locked) {
        tft.print("CONE: LOCKED   [C*] ");
    } else {
        tft.print("CONE: UNLOCKED  [C] ");
    }

    // Row 1: Network stats
    tft.setCursor(0, 20);
    char row1[21];
    snprintf(row1, sizeof(row1), "NETS:%04u NODES:%u    ", unique_nets, active_nodes);
    tft.print(row1);

    // Row 2: Last detection
    tft.setCursor(0, 40);
    if (last_rec) {
        char mac_short[9];  // First 3 bytes only: "AA:BB:CC"
        snprintf(mac_short, sizeof(mac_short), "%02X:%02X:%02X",
                 last_rec->bssid[0], last_rec->bssid[1], last_rec->bssid[2]);

        char mfr_short[9];
        strncpy(mfr_short, mfr, 8);
        mfr_short[8] = '\0';

        char row2[21];
        snprintf(row2, sizeof(row2), "%s %-8s %4d",
                 mac_short, mfr_short, last_rec->rssi);
        tft.print(row2);

        if (mode == MODE_CONE && !last_rec->in_cone) {
            tft.print(" (OOC)");
        }
    } else {
        tft.print("Waiting for data... ");
    }

    // Row 3: Status
    tft.setCursor(0, 60);
    char row3[21];
    snprintf(row3, sizeof(row3), "WIFI: OK   Q_DRP:%-3lu ", (unsigned long)espnow_rx_dropped());
    tft.print(row3);
}
