/**
 * Core TypeScript types for the wardriving dashboard
 * Defines all data structures for CSV parsing and application state
 */

export type AuthMode = 'OPEN' | 'WEP' | 'WPA' | 'WPA2' | 'WPA3' | 'WPA2/WPA3' | 'UNKNOWN';
export type ScanMode = 'radius' | 'cone';

/**
 * Represents a single WiFi network detection/observation
 */
export interface Detection {
  // Core identity
  mac: string;           // "AA:BB:CC:DD:EE:FF" - BSSID
  ssid: string;          // "" for hidden networks
  authMode: AuthMode;

  // Temporal
  firstSeen: Date;       // Parsed from FirstSeen column (UTC)

  // RF characteristics
  channel: number;       // 1-13
  frequencyMhz: number;  // e.g. 2437
  rssi: number;          // dBm, negative (e.g. -67)

  // Geographic position
  lat: number;
  lon: number;
  altitudeM: number;
  accuracyM: number;     // GPS accuracy radius in meters

  // Extended fields (null if loaded from WiGLE-only CSV)
  nodeId: number | null;           // Which ESP32 sensor node (1, 2, 3...)
  manufacturer: string | null;     // OUI-derived, e.g. "Cisco Systems"
  bearingDeg: number | null;       // IMU heading at detection; null if radius mode
  rangeM: number | null;           // Rangefinder distance in metres; null if not active
  inCone: boolean | null;          // true = within ±30° cone; false = outside
  hLockDeg: number | null;         // Locked cone heading; null if radius mode
  hdop: number | null;             // GPS quality (lower = better)
  satellites: number | null;       // GPS satellites in fix

  // Computed fields
  scanMode: ScanMode;              // 'cone' if bearingDeg !== null, else 'radius'
  oui: string;                     // First 3 bytes of MAC: "AA:BB:CC"
}

/**
 * Summary statistics computed from a session's detections
 */
export interface SessionSummary {
  totalDetections: number;
  uniqueNetworks: number;           // Unique MACs
  uniqueManufacturers: number;
  timeRange: { start: Date; end: Date };
  bounds: {                         // Map bounding box
    north: number;
    south: number;
    east: number;
    west: number;
  };
  channelDistribution: Record<number, number>;        // channel → count
  authModeDistribution: Record<AuthMode, number>;
  rssiHistogram: { bucket: string; count: number }[]; // e.g. "-70 to -60"
  nodeIds: number[];                // Which sensor nodes contributed
  hasConeData: boolean;
  avgHdop: number | null;
}

/**
 * A parsed session of wardriving data
 */
export interface Session {
  id: string;               // Generated from filename + hash
  filename: string;
  detections: Detection[];
  loadedAt: Date;

  // Computed summary (calculated once on load)
  summary: SessionSummary;
}
