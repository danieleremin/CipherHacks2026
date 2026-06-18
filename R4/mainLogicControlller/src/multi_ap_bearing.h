// multi_ap_bearing.h
// Multi-AP differential RSSI bearing estimator.
//
// How it works:
//   Both scanner nodes (1 and 2) see the same set of APs in the area.
//   For each AP visible to both nodes, the RSSI difference (node1 - node2)
//   encodes which scanner is physically closer to that AP.
//   Each AP gives one independent bearing estimate.
//   Averaging across N APs reduces noise by sqrt(N).
//
// The anchor AP (WARDRIVE_ANCHOR) is treated as a high-confidence reference
// since its TX power and position relative to the nodes is known.
// All other shared APs are treated as standard references.
//
// Output: bearing in degrees relative to the node baseline axis.
//   0°   = target is directly to the node-1 side
//   90°  = target is directly ahead (perpendicular to baseline)
//   180° = target is directly to the node-2 side
//
// This runs on the R4 in real time as records arrive.
// The same logic is replicated in the React frontend (src/lib/bearing.ts)
// for post-session analysis.

#pragma once
#include <Arduino.h>

// Physical geometry of the scanner rig (antenna-to-antenna, metres)
#define NODE_BASELINE_M       0.20f

// Anchor BSSID — receives a confidence multiplier since TX power is known
#define ANCHOR_BSSID          "30:76:F5:06:28:C5"
#define ANCHOR_CONFIDENCE_MUL 2.0f

// Correlation window — records from different nodes for the same BSSID
// are treated as simultaneous if uptime_ms values are within this range.
#define CORRELATION_WINDOW_MS 2000UL

// Minimum RSSI delta (dBm) to produce a bearing estimate from a single AP
#define MIN_RSSI_DELTA_DBM    2.0f

// Maximum number of APs tracked simultaneously
#define MAX_TRACKED_APS       48

// Maximum age of a record before it expires from the table
#define RECORD_MAX_AGE_MS     10000UL

// Minimum correlated APs needed for a valid bearing estimate
#define MIN_AP_COUNT          2

struct ApRecord {
    char     bssid[18];        // "AA:BB:CC:DD:EE:FF\0"
    int8_t   rssi_node1;       // Latest smoothed RSSI from node 1
    int8_t   rssi_node2;       // Latest smoothed RSSI from node 2
    uint32_t uptime_node1;     // uptime_ms when node 1 last saw this AP
    uint32_t uptime_node2;     // uptime_ms when node 2 last saw this AP
    bool     has_node1;
    bool     has_node2;
    bool     is_anchor;
};

struct BearingEstimate {
    float   bearing_deg;       // Degrees 0-180 relative to baseline axis
    float   confidence;        // 0.0-1.0
    uint8_t ap_count;          // APs that contributed
    float   rssi_delta_avg;    // Mean RSSI delta across contributing APs
    bool    valid;             // False if insufficient data
};

class MultiApBearing {
public:
    MultiApBearing();

    // Feed a detection record. node_id must be 1 or 2.
    void update(const char* bssid, uint8_t node_id,
                int8_t rssi, uint32_t uptime_ms);

    // Compute bearing estimate from all correlated AP pairs.
    BearingEstimate compute();

    // Expire stale records. Call periodically from loop().
    void expire(uint32_t current_uptime_ms);

    uint8_t tableSize() const { return _count; }
    uint8_t correlatedCount() const;

private:
    ApRecord _table[MAX_TRACKED_APS];
    uint8_t  _count;

    int   _findOrAlloc(const char* bssid);
    float _bearingFromDelta(float delta_dbm) const;
    float _confidenceFromDelta(float delta_dbm, bool is_anchor) const;
};
