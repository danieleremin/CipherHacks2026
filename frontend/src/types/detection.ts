/**
 * Core TypeScript types for the wardriving dashboard
 * Phase 2: GPS removed, anchor-node bearing estimation added.
 */

export type AuthMode = 'OPEN' | 'WEP' | 'WPA' | 'WPA2' | 'WPA3' | 'WPA2/WPA3' | 'UNKNOWN';
export type ScanMode = 'radius' | 'cone';
export type NodeId = 1 | 2 | 3;

/**
 * Represents a single WiFi network detection/observation
 */
export interface Detection {
  // Core identity
  mac: string; // "AA:BB:CC:DD:EE:FF"
  ssid: string; // "" for hidden networks
  authMode: AuthMode;

  // Temporal — uptime_ms is milliseconds since the detecting node booted.
  // Use for ordering and delta calculations only, not wall-clock display.
  uptimeMs: number;

  // RF
  channel: number; // 1–13
  frequencyMhz: number;
  rssi: number; // dBm, already smoothed by 20-sample averager on node

  // Position — GPS removed. lat/lon are always null.
  lat: null;
  lon: null;

  // Node metadata
  nodeId: NodeId;
  schemaVersion: number;

  // Extended fields
  manufacturer: string | null;
  bearingDeg: number | null; // null if radius mode or -1 in CSV
  rangeM: number | null;
  inCone: boolean | null;
  hLockDeg: number | null;

  // Computed
  scanMode: ScanMode;
  oui: string; // First 8 chars of MAC: "AA:BB:CC"
  isAnchor: boolean; // true if mac === ANCHOR_BSSID
}

/**
 * A pair of RSSI readings for the anchor BSSID from both scanner nodes,
 * captured within a correlation window. Used for bearing estimation.
 */
export interface AnchorObservation {
  uptimeMs: number; // midpoint timestamp of the window
  rssiNode1: number | null; // smoothed RSSI from node 1, null if not seen
  rssiNode2: number | null; // smoothed RSSI from node 2, null if not seen
  rssiDelta: number | null; // rssiNode1 - rssiNode2, null if either missing
  bearingEstimateDeg: number | null; // computed bearing, null if insufficient data
  confidenceScore: number; // 0.0–1.0, based on sample count and delta magnitude
}

/**
 * A parsed session of wardriving data
 */
export interface Session {
  id: string;
  filename: string;
  detections: Detection[]; // All detections including anchor observations
  anchorObservations: AnchorObservation[]; // Correlated anchor records
  loadedAt: Date;
  summary: SessionSummary;
}

/**
 * Summary statistics computed from a session's detections
 */
export interface SessionSummary {
  totalDetections: number;
  uniqueNetworks: number; // Unique MACs, excluding anchor
  uniqueManufacturers: number;
  uptimeRange: { start: number; end: number }; // ms, from first to last detection
  durationMs: number;
  channelDistribution: Record<number, number>;
  authModeDistribution: Record<AuthMode, number>;
  rssiHistogram: { bucket: string; count: number }[];
  nodeIds: NodeId[];
  hasConeData: boolean;
  hasAnchorData: boolean;
  bearingEstimates: AnchorObservation[]; // Alias for anchorObservations
  // GPS fields permanently absent — no avgHdop, no bounds
}
