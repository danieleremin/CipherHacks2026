# Wardriving Dashboard — Next.js Frontend Phase 2 Specification

**Project:** Autonomous wardriving / RF detection system  
**Document role:** Phase 2 frontend spec — covers schema changes, anchor node integration, multi-node RSSI correlation, and bearing estimation UI  
**Prerequisite:** Phase 1 spec (SPEC_NEXTJS_FRONTEND.md) — read that first  
**Who this is for:** The frontend developer picking up where Phase 1 left off  

---

## 1. What changed since Phase 1

Phase 1 was written before the hardware was finalized. Several things changed during firmware development that affect the frontend directly.

### 1.1 GPS removed entirely

The GPS module was cut from the project. Every GPS-derived field is gone from the data pipeline. The frontend must be updated to reflect this — any reference to latitude, longitude, altitude, HDOP, satellites, or timestamp_unix in the Phase 1 code is now dead.

**Removed fields:**

| Field | Was | Now |
|---|---|---|
| `latitude` | `double` | **gone** |
| `longitude` | `double` | **gone** |
| `altitude_m` | `float` | **gone** |
| `hdop_x10` | `uint8_t` | **gone** |
| `satellites` | `uint8_t` | **gone** |
| `timestamp_unix` | `uint32_t` | **gone** |
| `timestamp_ms` | `uint16_t` | **gone** |

**Added fields:**

| Field | Type | Description |
|---|---|---|
| `schema_version` | `uint8_t` | Schema version number, currently `1` |
| `uptime_ms` | `uint32_t` | Milliseconds since the detecting node booted — monotonic, relative |

**Implications for the frontend:**

The map view from Phase 1 cannot be populated with real data — there are no coordinates. The map should remain in the UI as a placeholder for a future GPS integration pass, but should display a clear "no location data" state rather than silently showing nothing.

All time-based display should use `uptime_ms` for sequencing and relative timestamps. Two records can be compared by their `uptime_ms` delta to determine ordering and gap durations. Absolute wall-clock time is not available.

The GPS quality row in the `SummaryBar` should be hidden when no GPS data is present. The `SessionSummary` type should reflect this — `avgHdop` and related fields become permanently `null`.

### 1.2 Three-node hardware setup

The system now runs three ESP32 DEVKIT V1 modules:

| Node | ID | Role |
|---|---|---|
| Node 1 | `0x01` | Scanner — captures WiFi beacons, transmits records to R4 |
| Node 2 | `0x02` | Scanner — same as node 1 |
| Node 3 | `0x03` | Anchor — broadcasts a reference beacon, does no scanning |

Node 3 does not transmit `WardrivingRecord` structs. It only broadcasts a WiFi beacon. Nodes 1 and 2 detect that beacon and include it in their capture stream like any other AP — but it is distinguishable by its known BSSID.

### 1.3 Known anchor BSSID

The anchor node's MAC address (its BSSID as seen by the scanner nodes) is:

```
30:76:F5:06:28:C5
```

This is a compile-time constant. Every detection record where `bssid === "30:76:F5:06:28:C5"` is an anchor observation, not a real AP detection. The frontend must treat these differently from regular network detections.

### 1.4 Physical node geometry

The two scanner nodes are mounted in an L-shaped rig with the following fixed geometry:

```
[Node 1] ←── 20cm ──→ [Node 2]
    |
   20cm
    |
[Node 3 / Anchor]
```

Antenna-to-antenna spacing: 20cm on both axes. This geometry is used by the bearing estimation algorithm. It is a fixed constant — the user does not configure it in the UI.

### 1.5 RSSI smoothing on nodes

Both scanner nodes now run a 20-sample rolling RSSI averager. The RSSI value in every transmitted record is already smoothed — the frontend does not need to apply additional smoothing. Effective noise floor after averaging is approximately ±1.1 dBm.

---

## 2. Updated TypeScript types

Replace the `Detection` interface from Phase 1 with this:

```typescript
// src/types/detection.ts

export type AuthMode = 'OPEN' | 'WEP' | 'WPA' | 'WPA2' | 'WPA3' | 'WPA2/WPA3' | 'UNKNOWN';
export type ScanMode = 'radius' | 'cone';
export type NodeId = 1 | 2 | 3;

export interface Detection {
  // Core identity
  mac: string;            // "AA:BB:CC:DD:EE:FF"
  ssid: string;           // "" for hidden networks
  authMode: AuthMode;

  // Temporal — uptime_ms is milliseconds since the detecting node booted.
  // Use for ordering and delta calculations only, not wall-clock display.
  uptimeMs: number;

  // RF
  channel: number;        // 1–13
  frequencyMhz: number;
  rssi: number;           // dBm, already smoothed by 20-sample averager on node

  // Position — GPS removed. lat/lon are always null.
  lat: null;
  lon: null;

  // Node metadata
  nodeId: NodeId;
  schemaVersion: number;

  // Extended fields
  manufacturer: string | null;
  bearingDeg: number | null;   // null if radius mode or -1 in CSV
  rangeM: number | null;
  inCone: boolean | null;
  hLockDeg: number | null;

  // Computed
  scanMode: ScanMode;
  oui: string;                 // First 8 chars of MAC: "AA:BB:CC"
  isAnchor: boolean;           // true if mac === ANCHOR_BSSID
}

export interface AnchorObservation {
  // A pair of RSSI readings for the anchor BSSID from both scanner nodes,
  // captured within a correlation window. Used for bearing estimation.
  uptimeMs: number;            // midpoint timestamp of the window
  rssiNode1: number | null;    // smoothed RSSI from node 1, null if not seen
  rssiNode2: number | null;    // smoothed RSSI from node 2, null if not seen
  rssiDelta: number | null;    // rssiNode1 - rssiNode2, null if either missing
  bearingEstimateDeg: number | null;  // computed bearing, null if insufficient data
  confidenceScore: number;     // 0.0–1.0, based on sample count and delta magnitude
}

export interface Session {
  id: string;
  filename: string;
  detections: Detection[];        // All detections including anchor observations
  anchorObservations: AnchorObservation[];  // Correlated anchor records
  loadedAt: Date;
  summary: SessionSummary;
}

export interface SessionSummary {
  totalDetections: number;
  uniqueNetworks: number;         // Unique MACs, excluding anchor
  uniqueManufacturers: number;
  uptimeRange: { start: number; end: number };  // ms, from first to last detection
  durationMs: number;
  channelDistribution: Record<number, number>;
  authModeDistribution: Record<AuthMode, number>;
  rssiHistogram: { bucket: string; count: number }[];
  nodeIds: NodeId[];
  hasConeData: boolean;
  hasAnchorData: boolean;
  bearingEstimates: AnchorObservation[];  // Alias for anchorObservations
  // GPS fields permanently absent — no avgHdop, no bounds
}
```

---

## 3. Updated CSV format

The R4 base node writes two CSV files. The column layout has changed from Phase 1 — GPS columns are removed and `uptime_ms` is added.

### 3.1 WiGLE CSV (`wardrive_wigle.csv`)

WiGLE format is unchanged structurally but GPS fields will be `0` or empty since no GPS is present. When parsing, treat `latitude=0` and `longitude=0` as no-fix and set `lat: null`, `lon: null`.

### 3.2 Extended CSV (`wardrive_ext.csv`)

Updated column layout:

| Index | Field | Type | Notes |
|---|---|---|---|
| 0 | MAC | string | BSSID |
| 1 | SSID | string | Empty = hidden |
| 2 | AuthMode | string | `[WPA2]` etc. |
| 3 | FirstSeen | string | Formatted uptime, not wall clock |
| 4 | Channel | number | 1–13 |
| 5 | Frequency | number | MHz |
| 6 | RSSI | number | dBm, smoothed |
| 7 | CurrentLatitude | number | Always 0 — no GPS |
| 8 | CurrentLongitude | number | Always 0 — no GPS |
| 9 | AltitudeMeters | number | Always 0 — no GPS |
| 10 | AccuracyMeters | number | Always -1 — no GPS |
| 11 | Type | string | Always `"WIFI"` |
| 12 | NodeID | number | 1 or 2 (never 3 — anchor doesn't log) |
| 13 | Manufacturer | string | OUI lookup result |
| 14 | BearingDeg | number | -1 if radius mode |
| 15 | RangeM | number | -1 if not active |
| 16 | InCone | number | 0 or 1 |
| 17 | HLockDeg | number | -1 if radius mode |
| 18 | UptimeMs | number | Replaces HDOP — ms since node boot |
| 19 | SchemaVersion | number | Currently 1 |

**Parsing note:** Column 18 is now `UptimeMs`, not `HDOP`. Any existing parser code that reads column 18 as HDOP must be updated.

---

## 4. Anchor constant

Add this to a new `src/lib/constants.ts` file:

```typescript
// src/lib/constants.ts

// MAC address of the anchor node (Node 3).
// Any detection record with this BSSID is a reference observation,
// not a real AP. Used for differential RSSI bearing estimation.
export const ANCHOR_BSSID = '30:76:F5:06:28:C5';

// Physical geometry of the scanner node rig (antenna-to-antenna, metres).
// Node 1 and Node 2 are on the horizontal axis.
// Node 1 and Node 3 (anchor) are on the vertical axis.
// These values are used by the bearing estimation algorithm.
export const NODE_BASELINE_M = 0.20;  // 20cm horizontal (nodes 1↔2)
export const ANCHOR_OFFSET_M = 0.20;  // 20cm vertical (node 1↔anchor)

// Smoothed RSSI noise floor after 20-sample averaging on the ESP32 nodes.
// Used for confidence scoring.
export const RSSI_NOISE_FLOOR_DBM = 1.1;

// Minimum RSSI delta (dBm) between node 1 and node 2 to produce a
// bearing estimate. Below this the differential is within the noise floor.
export const MIN_RSSI_DELTA_DBM = 2.0;

// Correlation window — two anchor observations from different nodes are
// considered simultaneous if their uptimeMs values are within this range.
export const CORRELATION_WINDOW_MS = 500;
```

---

## 5. Updated parser

The parser needs three changes from Phase 1: GPS fields mapped to null, `uptime_ms` read from column 18, and anchor detection flagged on load.

```typescript
// src/lib/parser.ts (updated sections only)

import { ANCHOR_BSSID } from './constants';

function rowToDetection(row: string[], isExtended: boolean): Detection | null {
  if (row.length < 12) return null;

  const lat = parseFloat(row[7]);
  const lon = parseFloat(row[8]);
  const hasGps = !(lat === 0 && lon === 0);  // 0,0 = no fix

  const uptimeMs = isExtended ? parseFloat(row[18]) : 0;
  const mac = row[0].trim().toUpperCase();

  return {
    mac,
    ssid:          row[1].trim(),
    authMode:      parseAuthMode(row[2]),
    uptimeMs,
    channel:       parseInt(row[4], 10),
    frequencyMhz:  parseInt(row[5], 10),
    rssi:          parseInt(row[6], 10),
    lat:           null,   // GPS removed
    lon:           null,
    nodeId:        isExtended ? parseInt(row[12], 10) as NodeId : 1,
    schemaVersion: isExtended ? parseInt(row[19], 10) : 1,
    manufacturer:  isExtended ? row[13].trim() || null : null,
    bearingDeg:    isExtended ? (parseFloat(row[14]) >= 0 ? parseFloat(row[14]) : null) : null,
    rangeM:        isExtended ? (parseFloat(row[15]) >= 0 ? parseFloat(row[15]) : null) : null,
    inCone:        isExtended ? row[16] === '1' : null,
    hLockDeg:      isExtended ? (parseFloat(row[17]) >= 0 ? parseFloat(row[17]) : null) : null,
    oui:           mac.substring(0, 8),
    scanMode:      (isExtended && parseFloat(row[14]) >= 0) ? 'cone' : 'radius',
    isAnchor:      mac === ANCHOR_BSSID,
  };
}
```

### 5.1 Anchor observation builder

After parsing all rows, compute correlated anchor observations:

```typescript
// src/lib/anchor.ts

import {
  ANCHOR_BSSID,
  CORRELATION_WINDOW_MS,
  MIN_RSSI_DELTA_DBM,
  NODE_BASELINE_M,
  RSSI_NOISE_FLOOR_DBM,
} from './constants';
import { Detection, AnchorObservation } from '@/types/detection';

export function buildAnchorObservations(
  detections: Detection[]
): AnchorObservation[] {
  // Separate anchor detections by node
  const node1 = detections.filter(d => d.isAnchor && d.nodeId === 1);
  const node2 = detections.filter(d => d.isAnchor && d.nodeId === 2);

  const observations: AnchorObservation[] = [];

  // For each node 1 anchor observation, find a matching node 2 observation
  // within the correlation window
  for (const a of node1) {
    const match = node2.find(
      b => Math.abs(b.uptimeMs - a.uptimeMs) <= CORRELATION_WINDOW_MS
    );

    const rssiNode1 = a.rssi;
    const rssiNode2 = match?.rssi ?? null;
    const delta = rssiNode2 !== null ? rssiNode1 - rssiNode2 : null;

    let bearingEstimateDeg: number | null = null;
    let confidenceScore = 0;

    if (delta !== null && Math.abs(delta) >= MIN_RSSI_DELTA_DBM) {
      // Bearing estimation using differential RSSI and known baseline.
      // Uses the inverse sine of the normalized delta to estimate angle.
      // delta > 0 → node 1 is closer → target is to the node-1 side
      // delta < 0 → node 2 is closer → target is to the node-2 side
      //
      // This gives bearing relative to the rig's axis.
      // 0° = directly toward node 1 side
      // 180° = directly toward node 2 side
      const normalized = Math.max(-1, Math.min(1, delta / 20));
      bearingEstimateDeg = (Math.asin(normalized) * 180) / Math.PI + 90;

      // Confidence: ratio of delta to noise floor, capped at 1.0
      confidenceScore = Math.min(1.0, Math.abs(delta) / (RSSI_NOISE_FLOOR_DBM * 10));
    }

    observations.push({
      uptimeMs: a.uptimeMs,
      rssiNode1,
      rssiNode2,
      rssiDelta: delta,
      bearingEstimateDeg,
      confidenceScore,
    });
  }

  return observations.sort((a, b) => a.uptimeMs - b.uptimeMs);
}
```

---

## 6. New UI panels

### 6.1 Bearing estimation panel (`BearingPanel.tsx`)

This is the primary new visual for Phase 2. Only rendered when `session.summary.hasAnchorData === true`.

**Layout:**

```
┌─────────────────────────────────────────────────────┐
│  Bearing estimation                                  │
│                                                      │
│   ┌──────────────┐   ┌──────────────────────────┐  │
│   │  Compass     │   │  Node RSSI comparison     │  │
│   │  rose SVG    │   │  Anchor RSSI node1: -52   │  │
│   │              │   │  Anchor RSSI node2: -61   │  │
│   │   ↑ 127°     │   │  Delta: +9 dBm            │  │
│   │              │   │  Confidence: 0.82          │  │
│   └──────────────┘   └──────────────────────────┘  │
│                                                      │
│  Bearing over time ─────────────────────────────    │
│  [Recharts AreaChart — bearing estimate vs uptimeMs] │
└─────────────────────────────────────────────────────┘
```

**Compass rose:** An SVG compass rose centered on the panel. A single needle rotates to the current (most recent) bearing estimate. Color the needle by confidence — full teal at confidence 1.0, fading to muted gray at confidence 0.0. Show the bearing in degrees as a label below the rose.

**Node RSSI comparison:** A simple stat card showing the most recent anchor RSSI from each node and the delta. Color the delta positive/negative — green if node 1 is stronger (target is on that side), amber if node 2 is stronger.

**Bearing over time:** A Recharts `LineChart` with `uptimeMs` on X axis (formatted as elapsed time `MM:SS`) and `bearingEstimateDeg` on Y axis (0–360). Plot only observations with `confidenceScore > 0.3`. Add a shaded band showing ±15° around the line to represent uncertainty.

### 6.2 Updated NetworkTable

Add one column and one filter:

- **Anchor** column: a small badge on any row where `isAnchor === true`. Anchor rows should sort to the top by default since they're the most important reference.
- **Hide anchor** toggle in the filter bar: lets the user exclude anchor detections from the table when they want to focus on real AP detections only.

### 6.3 Updated SummaryBar

Remove the GPS quality stat. Add two new stats:

```
[ N networks ]  [ N manufacturers ]  [ HH:MM:SS runtime ]  
[ N nodes ]  [ avg -XX dBm ]  [ bearing XX° ]
```

The bearing stat shows the most recent bearing estimate with confidence in parentheses: `127° (82%)`. Hidden if no anchor data.

### 6.4 No-GPS map state

The map should render with a visible empty state instead of an empty Leaflet map:

```
┌──────────────────────────────────────────────┐
│                                              │
│        No location data available            │
│   GPS was not present in this session.       │
│   Detection records are logged without       │
│   coordinates.                               │
│                                              │
│   [ View network table instead → ]           │
│                                              │
└──────────────────────────────────────────────┘
```

Do not render the Leaflet map at all when all coordinates are null — Leaflet's tile fetching will fire against a 0,0 coordinate and generate noise traffic. Check `session.summary.bounds` — if north, south, east, west are all 0, skip the map entirely.

---

## 7. Updated Zustand store

Add anchor-specific state:

```typescript
// Additional fields in SessionStore

// Anchor
anchorBssid: string;                      // From constants — not user-configurable
selectedObservationIdx: number | null;    // Which bearing observation is highlighted

// Actions
setSelectedObservation: (idx: number | null) => void;

// New derived hook
// useAnchorObservations() → AnchorObservation[]
// useLatestBearing() → AnchorObservation | null
```

---

## 8. Updated sample data

The synthetic sample data in `public/sample/` must be regenerated to match the new schema. Key requirements:

- No GPS coordinates — all lat/lon fields are `0`
- `uptime_ms` as the timestamp field (column 18), values increasing from 0 to ~1,800,000 (30 minutes)
- `schema_version = 1` in column 19
- Records from both node 1 and node 2 interleaved
- Multiple rows where `MAC = 30:76:F5:06:28:C5` (anchor detections from both nodes) with different RSSI values to simulate realistic differential — node 1 should consistently read the anchor ~6–10 dBm stronger than node 2 to produce a clear bearing estimate
- At least 40 anchor observation pairs that survive the correlation window so the bearing timeline has enough data to be interesting
- A cone mode segment: 30 rows with `bearing_deg` populated and `in_cone` values of both 0 and 1

---

## 9. Updated `package.json`

No new dependencies needed beyond Phase 1. The bearing math is plain JavaScript. However TinyGPS++ is a firmware dependency not a frontend one — confirm it isn't accidentally included anywhere in the frontend package.

---

## 10. Workstream split

If two people are working on the frontend simultaneously:

| Workstream | Deliverable |
|---|---|
| Data layer | Updated parser, new `anchor.ts`, updated types, regenerated sample data, updated Zustand store |
| Visualization | `BearingPanel.tsx` compass rose + timeline, updated `NetworkTable`, updated `SummaryBar`, no-GPS map state |

Interface between workstreams: `useAnchorObservations()` hook and `AnchorObservation[]` type. The visualization developer stubs this with synthetic data and wires in the real hook when the data layer is ready.

---

## 11. Things to wait on

Two things cannot be fully implemented until the R4 firmware is complete:

The exact extended CSV column layout — specifically whether the R4 writes `uptime_ms` as a formatted string or a raw integer, and whether `schema_version` appears in the output at all. The column indices in Section 3.2 are based on current R4 spec but should be verified against actual R4 output before shipping the parser.

Real anchor RSSI differentials — the synthetic sample data will simulate the bearing calculation but the actual confidence thresholds (`MIN_RSSI_DELTA_DBM`, `CORRELATION_WINDOW_MS`) may need tuning once real hardware data is available. Build the bearing panel with these as easily adjustable constants rather than hardcoded values.