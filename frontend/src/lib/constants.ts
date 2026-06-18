// src/lib/constants.ts

// MAC address of the anchor node (Node 3).
// Any detection record with this BSSID is a reference observation,
// not a real AP. Used for differential RSSI bearing estimation.
export const ANCHOR_BSSID = '30:76:F5:06:28:C5';

// Physical geometry of the scanner node rig (antenna-to-antenna, metres).
// Node 1 and Node 2 are on the horizontal axis.
// Node 1 and Node 3 (anchor) are on the vertical axis.
// These values are used by the bearing estimation algorithm.
export const NODE_BASELINE_M = 0.2; // 20cm horizontal (nodes 1↔2)
export const ANCHOR_OFFSET_M = 0.2; // 20cm vertical (node 1↔anchor)

// Smoothed RSSI noise floor after 20-sample averaging on the ESP32 nodes.
// Used for confidence scoring.
export const RSSI_NOISE_FLOOR_DBM = 1.1;

// Minimum RSSI delta (dBm) between node 1 and node 2 to produce a
// bearing estimate. Below this the differential is within the noise floor.
export const MIN_RSSI_DELTA_DBM = 2.0;

// Correlation window — two anchor observations from different nodes are
// considered simultaneous if their uptimeMs values are within this range.
export const CORRELATION_WINDOW_MS = 500;
