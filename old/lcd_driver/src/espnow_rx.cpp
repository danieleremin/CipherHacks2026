// firmware/lcd-driver/src/espnow_rx.cpp

#include <Arduino.h>
#include <string.h>
#include "espnow_rx.h"
#include "config.h"
#include "../../shared/uart_bridge_protocol.h"

static WardrivingRecord rx_queue[RX_QUEUE_DEPTH];
static uint8_t q_head = 0;
static uint8_t q_tail = 0;
static uint32_t dropped = 0;

static uint8_t mirrored_mode = MODE_RADIUS;
static bool    mirrored_cone_locked = false;

static bool queue_push(const WardrivingRecord* rec) {
    uint8_t next_head = (q_head + 1) % RX_QUEUE_DEPTH;
    if (next_head == q_tail) {
        dropped++;
        return false;  // Queue full — drop record
    }
    memcpy(&rx_queue[q_head], rec, sizeof(WardrivingRecord));
    q_head = next_head;
    return true;
}

// ── Frame parser state machine (mirrors the bridge's own parser) ──────────

enum FrameState { WAIT_SYNC0, WAIT_SYNC1, WAIT_TYPE, WAIT_LEN, WAIT_PAYLOAD, WAIT_CHECKSUM };

static FrameState state = WAIT_SYNC0;
static uint8_t frame_type = 0;
static uint8_t frame_len = 0;
static uint8_t frame_payload[sizeof(WardrivingRecord)];
static uint8_t frame_idx = 0;

static void apply_cmd(uint8_t cmd) {
    switch (cmd) {
        case CMD_SET_RADIUS:
            mirrored_mode = MODE_RADIUS;
            mirrored_cone_locked = false;
            break;
        case CMD_SET_CONE:
            mirrored_mode = MODE_CONE;
            break;
        case CMD_CONE_LOCK:
            mirrored_cone_locked = true;
            break;
        default: break;
    }
}

static void handle_complete_frame(uint8_t type, const uint8_t* payload, uint8_t len) {
    if (type == BRIDGE_PKT_RECORD && len == sizeof(WardrivingRecord)) {
        queue_push((const WardrivingRecord*)payload);
    } else if (type == BRIDGE_PKT_CMD && len == 1) {
        apply_cmd(payload[0]);
    }
}

void espnow_rx_init() {
    BRIDGE_SERIAL.begin(BRIDGE_BAUD);
}

void espnow_rx_poll() {
    while (BRIDGE_SERIAL.available()) {
        uint8_t b = (uint8_t)BRIDGE_SERIAL.read();

        switch (state) {
            case WAIT_SYNC0:
                if (b == BRIDGE_SYNC_0) state = WAIT_SYNC1;
                break;
            case WAIT_SYNC1:
                state = (b == BRIDGE_SYNC_1) ? WAIT_TYPE : WAIT_SYNC0;
                break;
            case WAIT_TYPE:
                frame_type = b;
                state = WAIT_LEN;
                break;
            case WAIT_LEN:
                frame_len = b;
                frame_idx = 0;
                if (frame_len == 0) {
                    state = WAIT_CHECKSUM;
                } else if (frame_len > sizeof(frame_payload)) {
                    state = WAIT_SYNC0;  // bogus length — resync
                } else {
                    state = WAIT_PAYLOAD;
                }
                break;
            case WAIT_PAYLOAD:
                frame_payload[frame_idx++] = b;
                if (frame_idx >= frame_len) state = WAIT_CHECKSUM;
                break;
            case WAIT_CHECKSUM: {
                uint8_t expected = bridge_checksum(frame_type, frame_len, frame_payload);
                if (b == expected) {
                    handle_complete_frame(frame_type, frame_payload, frame_len);
                }
                state = WAIT_SYNC0;
                break;
            }
        }
    }
}

bool espnow_rx_dequeue(WardrivingRecord* out) {
    if (q_tail == q_head) return false;  // Empty
    memcpy(out, &rx_queue[q_tail], sizeof(WardrivingRecord));
    q_tail = (q_tail + 1) % RX_QUEUE_DEPTH;
    return true;
}

uint32_t espnow_rx_dropped() { return dropped; }

uint8_t espnow_rx_mode()        { return mirrored_mode; }
bool    espnow_rx_cone_locked() { return mirrored_cone_locked; }
