# Wardriving Dashboard — Next.js Frontend Specification

**Project:** Autonomous wardriving / RF detection system  
**Document role:** Complete build specification for the web frontend  
**Data source:** SD card CSV files exported from the Arduino R4 base node (uploaded via drag-and-drop or HTTP POST from a local ingest script)  
**Stack:** Next.js 14 (App Router), TypeScript, Tailwind CSS, shadcn/ui, Leaflet, Recharts  
**Self-contained:** You do not need to understand the firmware. Everything you need about the data format is in Section 2.

---

## 1. System Overview

This frontend is a **post-mission analysis dashboard**. After a wardriving session, the operator uploads the two CSV files written by the Arduino R4 base node and gets an interactive map, detection timeline, device roster, and directional analysis for cone-mode sessions.

It is not a real-time streaming dashboard (that is a stretch goal described in Section 11). The primary workflow is:

```
Upload CSV files → Parse + validate → Interactive map + panels → Export / share
```

The app runs locally (via `npm run dev` or `npm run build && npm start`) or is deployed as a static export to any web host. It has no backend requirements in its base configuration — all parsing and state lives in the browser. An optional lightweight Express ingest server (Section 10) enables live SD-card sync over local network.

---

## 2. Data Formats

You receive two CSV files from the Arduino R4 base node. You must support both.

### 2.1 WiGLE CSV (`wardrive_wigle.csv`)

Standard WiGLE format. Two header rows followed by data:

```
WigleWifi-1.4,appRelease=1.0,model=WardriverR4,...
MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,Type
AA:BB:CC:DD:EE:FF,MyNetwork,[WPA2],2024-01-15 14:23:01,6,2437,-67,32.715736,-117.161087,45.2,5.0,WIFI
```

Column index reference:

| Index | Field | Type | Notes |
|---|---|---|---|
| 0 | MAC | string | BSSID, colon-separated hex |
| 1 | SSID | string | Empty string = hidden network |
| 2 | AuthMode | string | `[WPA2]`, `[WPA3]`, `[WEP]`, `[ESS]` (open), `[WPA2][WPA3]` |
| 3 | FirstSeen | string | `"YYYY-MM-DD HH:MM:SS"`, UTC |
| 4 | Channel | number | 1–13 |
| 5 | Frequency | number | MHz (e.g. 2437 for ch 6) |
| 6 | RSSI | number | dBm, negative (e.g. -67) |
| 7 | CurrentLatitude | number | Decimal degrees WGS84 |
| 8 | CurrentLongitude | number | Decimal degrees WGS84 |
| 9 | AltitudeMeters | number | |
| 10 | AccuracyMeters | number | Derived from HDOP |
| 11 | Type | string | Always `"WIFI"` |

### 2.2 Extended CSV (`wardrive_ext.csv`)

Same as WiGLE columns 0–11, plus:

| Index | Field | Type | Notes |
|---|---|---|---|
| 12 | NodeID | number | Which ESP32 sensor node (1, 2, 3…) |
| 13 | Manufacturer | string | OUI-derived, e.g. "Cisco Systems" or "Unknown" |
| 14 | BearingDeg | number | IMU heading at detection; `-1` if radius mode |
| 15 | RangeM | number | Rangefinder distance in metres; `-1` if not active |
| 16 | InCone | number | `1` = within ±30° cone; `0` = outside; always `1` in radius mode |
| 17 | HLockDeg | number | Locked cone heading; `-1` if radius mode |
| 18 | HDOP | number | GPS quality (lower = better; < 2.0 is good) |
| 19 | Satellites | number | GPS satellites in fix at detection time |

### 2.3 TypeScript Type Definitions

```typescript
// src/types/detection.ts

export type AuthMode = 'OPEN' | 'WEP' | 'WPA' | 'WPA2' | 'WPA3' | 'WPA2/WPA3' | 'UNKNOWN';
export type ScanMode = 'radius' | 'cone';

export interface Detection {
  // Core identity
  mac: string;           // "AA:BB:CC:DD:EE:FF"
  ssid: string;          // "" for hidden networks
  authMode: AuthMode;

  // Temporal
  firstSeen: Date;       // Parsed from FirstSeen column

  // RF
  channel: number;
  frequencyMhz: number;
  rssi: number;          // dBm, negative

  // Position
  lat: number;
  lon: number;
  altitudeM: number;
  accuracyM: number;

  // Extended fields (null if loaded from WiGLE-only CSV)
  nodeId: number | null;
  manufacturer: string | null;
  bearingDeg: number | null;   // null if -1 in CSV
  rangeM: number | null;       // null if -1 in CSV
  inCone: boolean | null;
  hLockDeg: number | null;     // null if -1 in CSV
  hdop: number | null;
  satellites: number | null;

  // Computed
  scanMode: ScanMode;          // 'cone' if bearingDeg !== null, else 'radius'
  oui: string;                 // First 3 bytes of MAC: "AA:BB:CC"
}

export interface Session {
  id: string;               // Generated from filename + hash
  filename: string;
  detections: Detection[];
  loadedAt: Date;

  // Computed summary (calculated once on load)
  summary: SessionSummary;
}

export interface SessionSummary {
  totalDetections: number;
  uniqueNetworks: number;       // Unique MACs
  uniqueManufacturers: number;
  timeRange: { start: Date; end: Date };
  bounds: {                     // Map bounding box
    north: number; south: number;
    east: number;  west: number;
  };
  channelDistribution: Record<number, number>;  // channel → count
  authModeDistribution: Record<AuthMode, number>;
  rssiHistogram: { bucket: string; count: number }[];  // e.g. "-70 to -60"
  nodeIds: number[];            // Which sensor nodes contributed
  hasConeData: boolean;
  avgHdop: number | null;
}
```

### 2.4 CSV Parsing Notes

- Skip the first two rows of WiGLE CSV (the app header line and the column header line). Data starts at row 3.
- The extended CSV has one header row; data starts at row 2.
- Detect which format by checking if column index 12 exists.
- SSID may contain commas — the Arduino writes SSIDs without quoting. If a row has more columns than expected, merge columns 1 through `(total - 10)` as the SSID.
- Some rows may have `0.000000` lat/lon — these are pre-fix records and should be flagged with `accuracyM = -1` and excluded from map rendering but retained in tables.
- Parse `AuthMode` string: `[ESS]` → `OPEN`, `[WEP]` → `WEP`, `[WPA]` → `WPA`, `[WPA2]` → `WPA2`, `[WPA3]` → `WPA3`, `[WPA2][WPA3]` → `WPA2/WPA3`.

---

## 3. Tech Stack

| Layer | Choice | Version | Notes |
|---|---|---|---|
| Framework | Next.js (App Router) | 14.x | Static export compatible |
| Language | TypeScript | 5.x | Strict mode |
| Styling | Tailwind CSS | 3.x | + CSS variables for theme tokens |
| Components | shadcn/ui | latest | Install components as needed via CLI |
| Map | Leaflet + react-leaflet | 4.x | Dynamic import (no SSR) |
| Charts | Recharts | 2.x | |
| CSV parsing | Papa Parse | 5.x | |
| State | Zustand | 4.x | |
| Date handling | date-fns | 3.x | |
| File handling | Browser File API | native | No upload to server |
| Icons | Lucide React | latest | |

### Why these choices

**Leaflet over Mapbox/Google Maps:** No API key required, works offline, open source. The wardriving use case often involves sensitive location data — keeping it entirely client-side by default is a deliberate privacy choice.

**Zustand over Redux / Context:** The session store and filter state are simple enough that a single Zustand store with selectors covers everything. No boilerplate.

**Static export compatibility:** `next.config.js` with `output: 'export'` lets the built app run from a USB drive or any file server with no Node.js runtime needed post-build.

---

## 4. Project Structure

```
wardriving-dashboard/
├── src/
│   ├── app/
│   │   ├── layout.tsx              — Root layout, fonts, global styles
│   │   ├── page.tsx                — Landing / upload page
│   │   ├── dashboard/
│   │   │   └── page.tsx            — Main dashboard (requires session loaded)
│   │   └── globals.css
│   ├── components/
│   │   ├── upload/
│   │   │   ├── DropZone.tsx        — Drag-and-drop CSV upload
│   │   │   └── ParseProgress.tsx   — Loading indicator during parse
│   │   ├── map/
│   │   │   ├── DetectionMap.tsx    — Leaflet map (dynamic import)
│   │   │   ├── HeatmapLayer.tsx    — RSSI-weighted heatmap overlay
│   │   │   ├── ConeOverlay.tsx     — 60° cone visualization
│   │   │   └── MarkerCluster.tsx   — Clustered detection markers
│   │   ├── panels/
│   │   │   ├── SummaryBar.tsx      — Top stats strip
│   │   │   ├── NetworkTable.tsx    — Sortable/filterable device roster
│   │   │   ├── RSSIChart.tsx       — RSSI distribution histogram
│   │   │   ├── ChannelChart.tsx    — Channel usage bar chart
│   │   │   ├── TimelineChart.tsx   — Detections over time
│   │   │   ├── AuthPieChart.tsx    — Encryption type breakdown
│   │   │   ├── NodePanel.tsx       — Per-node breakdown (multi-node sessions)
│   │   │   └── ConeAnalysis.tsx    — Cone mode bearing + range analysis
│   │   ├── filters/
│   │   │   ├── FilterBar.tsx       — Active filter chips + clear all
│   │   │   └── FilterDrawer.tsx    — Full filter panel (slide-in)
│   │   └── ui/                     — shadcn/ui components (auto-generated)
│   ├── lib/
│   │   ├── parser.ts               — CSV parsing, validation, normalization
│   │   ├── summarize.ts            — SessionSummary computation
│   │   ├── filters.ts              — Filter logic and types
│   │   └── export.ts               — KML / GeoJSON export
│   ├── store/
│   │   └── session.ts              — Zustand store
│   └── types/
│       └── detection.ts            — All TypeScript types (Section 2.3)
├── public/
│   └── tiles/                      — Optional: offline tile cache (see Section 9)
├── next.config.js
├── tailwind.config.ts
└── package.json
```

---

## 5. Application Pages

### 5.1 Landing Page (`/`)

The entry point when no session is loaded. Shows the upload interface.

**Layout:**

```
┌──────────────────────────────────────────────┐
│  WARDRIVER                          [DOCS]   │
├──────────────────────────────────────────────┤
│                                              │
│         Drop CSV files here                  │
│         or click to browse                   │
│                                              │
│    [ wardrive_wigle.csv  ✓ ]                 │
│    [ wardrive_ext.csv    ✓ ]                 │
│                                              │
│         [ Load session → ]                   │
│                                              │
│  ─────────── or ───────────                  │
│                                              │
│         [ Load sample data ]                 │
│                                              │
└──────────────────────────────────────────────┘
```

- Accept both files simultaneously (multi-select or two separate drops)
- Accept either file alone — WiGLE-only loads with reduced feature set (no manufacturer, no cone data)
- Show file size and estimated detection count after selection, before parsing
- "Load sample data" loads a bundled synthetic dataset (≥ 200 detections) so reviewers can evaluate the UI without real hardware
- On parse completion, `router.push('/dashboard')`

### 5.2 Dashboard (`/dashboard`)

Redirects to `/` if no session is loaded in the store.

**Full layout (desktop, ≥1280px):**

```
┌─────────────────────────────────────────────────────────────────────────┐
│  WARDRIVER  ▸ session_2024-01-15  [Filters ▾]  [Export ▾]  [+ Upload]  │
├──────────────────────────────────────────────────────────────────────────┤
│  142 networks  ·  8 manufacturers  ·  00:47 duration  ·  2 nodes        │
├────────────────────────────────┬────────────────────────────────────────┤
│                                │  NETWORKS                               │
│                                │  ┌──────────────────────────────────┐  │
│                                │  │ Filter: [___________] [▾ Auth]   │  │
│         MAP                    │  │ BSSID          SSID    RSSI  ENC │  │
│    (Leaflet, full height)      │  │ AA:BB:CC...    Home    -62   WPA2│  │
│                                │  │ ...                              │  │
│                                │  └──────────────────────────────────┘  │
│                                ├────────────────────────────────────────┤
│                                │  RSSI DISTRIBUTION                      │
│                                │  ▓▓▓▓▓▓▓▓▒▒▒▒▓▓▓▓▒▒░░░              │
│                                ├────────────────────────────────────────┤
│                                │  CHANNEL USAGE    AUTH TYPES           │
│                                │  [bar chart]      [pie chart]          │
├────────────────────────────────┴────────────────────────────────────────┤
│  TIMELINE  ──────────────────────────────────────────────────────────── │
└──────────────────────────────────────────────────────────────────────────┘
```

**Mobile layout (< 768px):** Vertical stack — summary → map (50vw height) → tab bar switching between Networks, Charts, Timeline.

---

## 6. Component Specifications

### 6.1 DetectionMap

The map is the primary feature. Use Leaflet with `react-leaflet`. Must be loaded with `next/dynamic` and `{ ssr: false }` — Leaflet accesses `window` and breaks SSR.

```typescript
// src/components/map/DetectionMap.tsx (simplified)
'use client';

import { MapContainer, TileLayer, useMap } from 'react-leaflet';
import MarkerClusterGroup from 'react-leaflet-cluster';

interface DetectionMapProps {
  detections: Detection[];
  selectedMac: string | null;
  onSelectNetwork: (mac: string) => void;
  showHeatmap: boolean;
  showCone: boolean;
}
```

**Map layers (toggle-able from UI):**

| Layer | Default | Description |
|---|---|---|
| Base tiles | ON | OpenStreetMap (online) or local tiles (offline) |
| Detection markers | ON | Clustered pins; color = encryption type |
| RSSI heatmap | OFF | Leaflet.heat; weight = signal strength |
| Path trace | OFF | Polyline connecting detections in time order |
| Cone overlay | ON if cone data | Filled arc showing the 60° locked heading |
| Node color coding | ON if multi-node | Each node_id gets a distinct marker color |

**Marker color by encryption:**

```typescript
const AUTH_COLORS: Record<AuthMode, string> = {
  OPEN:       '#ef4444',  // red — open networks stand out
  WEP:        '#f97316',  // orange — weak encryption
  WPA:        '#eab308',  // yellow
  WPA2:       '#22c55e',  // green
  WPA3:       '#3b82f6',  // blue
  'WPA2/WPA3': '#6366f1', // indigo
  UNKNOWN:    '#6b7280',  // gray
};
```

**On marker click:** Select that network — highlight its row in the NetworkTable, zoom map to it, show a popup with SSID, BSSID, RSSI, manufacturer, first seen time.

**Map popup content:**

```
┌─────────────────────────────────┐
│ HomeNetwork_2G          [WPA2]  │
│ AA:BB:CC:DD:EE:FF               │
│ Cisco Systems                   │
│ ch 6  ·  -62 dBm  ·  2437 MHz  │
│ 14:23:01 UTC                    │
│ [View in table →]               │
└─────────────────────────────────┘
```

**ConeOverlay:** Renders when `hasConeData = true`. Draws a filled SVG arc from the session's `hLockDeg` ± 30° centered on the average detection position. Use a semi-transparent fill. Show the bearing in degrees as a label at the arc's apex.

```typescript
// Cone geometry: center on average lat/lon of all cone-mode detections
// Radius: use max(rangeM) from detections, or 100m default
// Arc: hLockDeg - 30° to hLockDeg + 30°
// Render as Leaflet Polygon with lat/lon vertices computed from bearing + radius
```

### 6.2 NetworkTable

A virtualized table (use `@tanstack/react-table` for column logic) of unique networks — one row per unique MAC address.

**Columns:**

| Column | Width | Sortable | Notes |
|---|---|---|---|
| BSSID | 140px | No | Monospace font; first 3 bytes bolded (OUI) |
| SSID | flex | Yes | `(hidden)` in muted italic if empty |
| Auth | 80px | Yes | Colored badge matching marker color |
| RSSI | 70px | Yes | Show best (strongest) RSSI seen for this MAC |
| Ch | 50px | Yes | |
| Manufacturer | 160px | Yes | Truncate with tooltip |
| First Seen | 90px | Yes | Time only (HH:MM:SS); full timestamp in tooltip |
| Node | 50px | Yes | Only shown if multi-node session |

**Row interactions:**
- Click → select network, fly map to its position
- Selected row is highlighted and stays visible if table is scrolled
- Right-click → context menu: "Copy MAC", "Copy SSID", "Filter to this manufacturer"

**Filtering:**
- Text search across SSID + BSSID + Manufacturer (debounced 150ms)
- Auth mode multi-select checkboxes
- RSSI range slider (−30 to −90 dBm)
- Channel multi-select (1–13)
- Node ID filter (multi-node sessions)
- "In cone only" toggle (visible only if cone data present)
- "Hide unknown manufacturer" toggle

Active filters display as removable chips in the FilterBar above the table.

### 6.3 SummaryBar

A horizontal strip of stat cards at the top of the dashboard. All numbers update reactively when filters are applied.

```
[ 142 networks ]  [ 8 manufacturers ]  [ 00:47 runtime ]  [ 2 nodes ]  [ avg -64 dBm ]  [ GPS 1.2 HDOP ]
```

Stats:
- **Networks:** Unique MAC count after filters
- **Manufacturers:** Unique manufacturer strings after filters
- **Runtime:** `lastSeen - firstSeen` formatted as `HH:MM:SS`
- **Nodes:** Count of unique `nodeId` values (hidden if only 1)
- **Avg RSSI:** Mean RSSI across all filtered detections
- **GPS Quality:** Average HDOP formatted as `1.2 HDOP`; color-coded (green < 2, yellow 2–5, red > 5); hidden if null

### 6.4 RSSIChart

Recharts `BarChart`. Buckets detections into 5-dBm ranges from −30 to −95+.

```typescript
const buckets = [
  '−30 to −40', '−40 to −50', '−50 to −60',
  '−60 to −70', '−70 to −80', '−80 to −90', '< −90'
];
```

X-axis: RSSI bucket label. Y-axis: count. Bar fill uses a color gradient from bright (strong signal) to muted (weak signal). Clicking a bar filters the NetworkTable to that RSSI range.

### 6.5 ChannelChart

Recharts `BarChart`. X-axis: channel (1–13). Y-axis: unique network count on that channel. 

Annotate channels 1, 6, and 11 with a subtle top label "non-overlap" — these are the three non-overlapping 2.4GHz channels and the ones where legitimate APs usually operate. Heavy usage on non-standard channels (2–5, 7–10, 12–13) can indicate interference or unusual configurations.

### 6.6 TimelineChart

Recharts `AreaChart`. X-axis: time (from session start to end). Y-axis: detections per minute bucket.

If multi-node session, render one area per node, stacked. Use Recharts `Brush` component to let the user zoom into a time range — zooming updates the map and table to show only detections in that window.

### 6.7 ConeAnalysis

Only rendered if `session.summary.hasConeData === true`.

Shows:
- **Polar chart** (use Recharts `RadarChart` or a custom SVG) — plots detection count by bearing bucket (12 × 30° slices). Highlights the ±30° cone window.
- **Range histogram** — distribution of `rangeM` values for in-cone detections.
- **Lock heading:** "Cone locked to 127° (SE)" with a compass rose SVG.
- **In-cone vs out-of-cone:** Small stat: "94 in cone / 12 outside"

### 6.8 NodePanel

Only rendered if more than one unique `nodeId` is present.

A card per node showing:
- Node ID badge
- Detection count
- Unique networks
- RSSI range (min/max)
- Time range (first/last detection from this node)

---

## 7. State Management (Zustand)

```typescript
// src/store/session.ts
import { create } from 'zustand';
import { Detection, Session, SessionSummary } from '@/types/detection';

export interface FilterState {
  searchText: string;
  authModes: AuthMode[];            // Empty = all
  rssiRange: [number, number];      // [min, max] dBm
  channels: number[];               // Empty = all
  nodeIds: number[];                // Empty = all
  inConeOnly: boolean;
  hideUnknownManufacturer: boolean;
  timeRange: [Date, Date] | null;   // null = full session range
}

const DEFAULT_FILTERS: FilterState = {
  searchText: '',
  authModes: [],
  rssiRange: [-100, 0],
  channels: [],
  nodeIds: [],
  inConeOnly: false,
  hideUnknownManufacturer: false,
  timeRange: null,
};

interface SessionStore {
  // Data
  session: Session | null;
  setSession: (s: Session) => void;
  clearSession: () => void;

  // Filters
  filters: FilterState;
  setFilter: <K extends keyof FilterState>(key: K, value: FilterState[K]) => void;
  resetFilters: () => void;

  // Selection
  selectedMac: string | null;
  setSelectedMac: (mac: string | null) => void;

  // Map UI state
  showHeatmap: boolean;
  showPathTrace: boolean;
  showCone: boolean;
  toggleLayer: (layer: 'heatmap' | 'pathTrace' | 'cone') => void;

  // Derived (computed via selector, not stored)
  // Use: useFilteredDetections() hook — see Section 7.1
}

export const useSessionStore = create<SessionStore>((set) => ({
  session: null,
  setSession: (session) => set({ session }),
  clearSession: () => set({ session: null, selectedMac: null, filters: DEFAULT_FILTERS }),

  filters: DEFAULT_FILTERS,
  setFilter: (key, value) =>
    set((state) => ({ filters: { ...state.filters, [key]: value } })),
  resetFilters: () => set({ filters: DEFAULT_FILTERS }),

  selectedMac: null,
  setSelectedMac: (mac) => set({ selectedMac: mac }),

  showHeatmap: false,
  showPathTrace: false,
  showCone: true,
  toggleLayer: (layer) =>
    set((state) => ({
      showHeatmap:   layer === 'heatmap'    ? !state.showHeatmap   : state.showHeatmap,
      showPathTrace: layer === 'pathTrace'  ? !state.showPathTrace : state.showPathTrace,
      showCone:      layer === 'cone'       ? !state.showCone      : state.showCone,
    })),
}));
```

### 7.1 `useFilteredDetections` Hook

Computing filtered detections on every render would be expensive for large sessions. Memoize:

```typescript
// src/lib/filters.ts
import { useMemo } from 'react';
import { useSessionStore } from '@/store/session';

export function useFilteredDetections(): Detection[] {
  const session  = useSessionStore((s) => s.session);
  const filters  = useSessionStore((s) => s.filters);

  return useMemo(() => {
    if (!session) return [];
    let d = session.detections;

    if (filters.searchText) {
      const q = filters.searchText.toLowerCase();
      d = d.filter(x =>
        x.mac.toLowerCase().includes(q) ||
        x.ssid.toLowerCase().includes(q) ||
        (x.manufacturer ?? '').toLowerCase().includes(q)
      );
    }

    if (filters.authModes.length > 0)
      d = d.filter(x => filters.authModes.includes(x.authMode));

    d = d.filter(x => x.rssi >= filters.rssiRange[0] && x.rssi <= filters.rssiRange[1]);

    if (filters.channels.length > 0)
      d = d.filter(x => filters.channels.includes(x.channel));

    if (filters.nodeIds.length > 0)
      d = d.filter(x => filters.nodeIds.includes(x.nodeId ?? 0));

    if (filters.inConeOnly)
      d = d.filter(x => x.inCone === true);

    if (filters.hideUnknownManufacturer)
      d = d.filter(x => x.manufacturer && x.manufacturer !== 'Unknown');

    if (filters.timeRange)
      d = d.filter(x =>
        x.firstSeen >= filters.timeRange![0] && x.firstSeen <= filters.timeRange![1]
      );

    return d;
  }, [session, filters]);
}

// Deduplicated by MAC (for table) — best RSSI per BSSID
export function useUniqueNetworks(): Detection[] {
  const filtered = useFilteredDetections();
  return useMemo(() => {
    const map = new Map<string, Detection>();
    for (const d of filtered) {
      const existing = map.get(d.mac);
      if (!existing || d.rssi > existing.rssi) {
        map.set(d.mac, d);
      }
    }
    return Array.from(map.values());
  }, [filtered]);
}
```

---

## 8. CSV Parser Implementation

```typescript
// src/lib/parser.ts
import Papa from 'papaparse';
import { Detection, Session, AuthMode } from '@/types/detection';
import { computeSummary } from './summarize';

function parseAuthMode(raw: string): AuthMode {
  if (raw.includes('WPA3') && raw.includes('WPA2')) return 'WPA2/WPA3';
  if (raw.includes('WPA3')) return 'WPA3';
  if (raw.includes('WPA2')) return 'WPA2';
  if (raw.includes('WPA'))  return 'WPA';
  if (raw.includes('WEP'))  return 'WEP';
  if (raw.includes('ESS'))  return 'OPEN';
  return 'UNKNOWN';
}

function rowToDetection(row: string[], isExtended: boolean): Detection | null {
  if (row.length < 12) return null;

  const lat = parseFloat(row[7]);
  const lon = parseFloat(row[8]);

  // Skip rows with no GPS fix
  if (lat === 0 && lon === 0) return null;

  const bearingRaw = isExtended ? parseFloat(row[14]) : -1;
  const rangeRaw   = isExtended ? parseFloat(row[15]) : -1;
  const hLockRaw   = isExtended ? parseFloat(row[17]) : -1;

  return {
    mac:          row[0].trim().toUpperCase(),
    ssid:         row[1].trim(),
    authMode:     parseAuthMode(row[2]),
    firstSeen:    new Date(row[3].trim() + 'Z'),  // Force UTC
    channel:      parseInt(row[4], 10),
    frequencyMhz: parseInt(row[5], 10),
    rssi:         parseInt(row[6], 10),
    lat,
    lon,
    altitudeM:    parseFloat(row[9]),
    accuracyM:    parseFloat(row[10]),
    nodeId:       isExtended ? parseInt(row[12], 10) : null,
    manufacturer: isExtended ? row[13].trim() || null : null,
    bearingDeg:   bearingRaw  >= 0 ? bearingRaw  : null,
    rangeM:       rangeRaw    >= 0 ? rangeRaw    : null,
    inCone:       isExtended  ? row[16] === '1' : null,
    hLockDeg:     hLockRaw    >= 0 ? hLockRaw   : null,
    hdop:         isExtended  ? parseFloat(row[18]) : null,
    satellites:   isExtended  ? parseInt(row[19], 10) : null,
    oui:          row[0].trim().substring(0, 8).toUpperCase(),
    scanMode:     (bearingRaw >= 0) ? 'cone' : 'radius',
  };
}

export async function parseCSV(
  file: File,
  onProgress?: (pct: number) => void
): Promise<Session> {
  return new Promise((resolve, reject) => {
    const detections: Detection[] = [];
    let rowCount = 0;
    let isExtended = false;
    let skipRows = 2;  // WiGLE has 2 header rows

    Papa.parse<string[]>(file, {
      worker: true,  // Parse off main thread
      step: (result) => {
        rowCount++;
        if (rowCount <= skipRows) {
          // Detect format from header row
          if (rowCount === 1) {
            const firstCell = result.data[0] ?? '';
            if (firstCell.startsWith('WigleWifi')) {
              skipRows = 2;
              isExtended = false;
            } else if (firstCell === 'MAC') {
              // Extended CSV has just one header row
              skipRows = 1;
              isExtended = result.data.length > 12;
            }
          }
          return;
        }

        const d = rowToDetection(result.data, isExtended);
        if (d) detections.push(d);

        if (onProgress && rowCount % 500 === 0) {
          onProgress(Math.min(90, (rowCount / 5000) * 90));
        }
      },
      complete: () => {
        const session: Session = {
          id: `${file.name}-${Date.now()}`,
          filename: file.name,
          detections,
          loadedAt: new Date(),
          summary: computeSummary(detections),
        };
        resolve(session);
      },
      error: reject,
    });
  });
}
```

---

## 9. Export Features

Accessible via the `[Export ▾]` dropdown in the navbar.

### 9.1 WiGLE CSV Re-export

Writes the currently filtered detections back to WiGLE-compatible CSV. Useful for uploading a filtered subset to WiGLE.

### 9.2 GeoJSON Export

Exports each unique network as a GeoJSON `Feature` with a `Point` geometry and all detection fields as properties. Compatible with QGIS, Mapbox, and any GIS tool.

```typescript
// src/lib/export.ts
export function toGeoJSON(detections: Detection[]): string {
  const features = detections.map(d => ({
    type: 'Feature',
    geometry: { type: 'Point', coordinates: [d.lon, d.lat] },
    properties: {
      mac: d.mac, ssid: d.ssid, authMode: d.authMode,
      rssi: d.rssi, channel: d.channel, manufacturer: d.manufacturer,
      firstSeen: d.firstSeen.toISOString(),
      bearingDeg: d.bearingDeg, rangeM: d.rangeM,
      inCone: d.inCone, nodeId: d.nodeId,
    },
  }));
  return JSON.stringify({ type: 'FeatureCollection', features }, null, 2);
}
```

### 9.3 KML Export

Exports markers for Google Earth / Maps import. Each marker uses an icon color matching the encryption type. Generates a folder structure grouping by auth mode.

### 9.4 PNG Map Snapshot

Uses `leaflet-image` library to rasterize the current map view (with all visible layers) to a PNG. Opens in a new tab for save-as.

### 9.5 Session JSON

Exports the complete parsed `Session` object as JSON. Can be re-imported to skip re-parsing the CSV.

---

## 10. Optional: Local Ingest Server

A minimal Express server in `server/ingest.ts` that watches the SD card mount point and POSTs new CSV rows to the frontend via Server-Sent Events. Enables near-real-time updates during an active session.

```
wardriving-dashboard/
└── server/
    ├── ingest.ts     — Express + chokidar file watcher
    └── package.json  — separate package, run independently
```

**How it works:**
1. The SD card is mounted at a known path (e.g., `/media/user/WARDRIVE` on Linux, `D:\` on Windows)
2. `chokidar` watches `wardrive_ext.csv` for changes
3. On each change, tail the new rows appended since last read
4. Parse and broadcast via `/api/stream` (SSE endpoint)
5. Next.js frontend connects to `EventSource('/api/stream')` when live mode is enabled

This is opt-in and not required for the base feature set.

```typescript
// server/ingest.ts (sketch)
import express from 'express';
import chokidar from 'chokidar';
import { createReadStream } from 'fs';

const app = express();
const clients = new Set<express.Response>();

app.get('/api/stream', (req, res) => {
  res.setHeader('Content-Type', 'text/event-stream');
  res.setHeader('Cache-Control', 'no-cache');
  res.setHeader('Connection', 'keep-alive');
  clients.add(res);
  req.on('close', () => clients.delete(res));
});

const SD_PATH = process.env.SD_PATH ?? '/media/user/WARDRIVE/wardrive_ext.csv';
let lastSize = 0;

chokidar.watch(SD_PATH).on('change', (path, stats) => {
  if (!stats || stats.size <= lastSize) return;
  const stream = createReadStream(path, { start: lastSize, encoding: 'utf8' });
  let buf = '';
  stream.on('data', (chunk) => { buf += chunk; });
  stream.on('end', () => {
    lastSize = stats.size;
    const rows = buf.split('\n').filter(Boolean);
    for (const row of rows) {
      const payload = JSON.stringify({ row });
      clients.forEach(c => c.write(`data: ${payload}\n\n`));
    }
  });
});

app.listen(3001, () => console.log('Ingest server on :3001'));
```

---

## 11. `next.config.js`

```javascript
/** @type {import('next').NextConfig} */
const nextConfig = {
  // Static export — uncomment for offline/USB deployment
  // output: 'export',

  // Required for Leaflet tile images from external domains
  images: {
    remotePatterns: [
      {
        protocol: 'https',
        hostname: '*.tile.openstreetmap.org',
      },
    ],
    unoptimized: true,  // Required for static export
  },
};

module.exports = nextConfig;
```

---

## 12. `package.json` Dependencies

```json
{
  "dependencies": {
    "next": "^14.2.0",
    "react": "^18.3.0",
    "react-dom": "^18.3.0",
    "typescript": "^5.4.0",
    "tailwindcss": "^3.4.0",
    "zustand": "^4.5.0",
    "papaparse": "^5.4.1",
    "@types/papaparse": "^5.3.14",
    "leaflet": "^1.9.4",
    "react-leaflet": "^4.2.1",
    "@types/leaflet": "^1.9.12",
    "react-leaflet-cluster": "^2.1.0",
    "leaflet.heat": "^0.2.0",
    "recharts": "^2.12.0",
    "@tanstack/react-table": "^8.17.0",
    "date-fns": "^3.6.0",
    "lucide-react": "^0.400.0",
    "clsx": "^2.1.1",
    "tailwind-merge": "^2.3.0"
  },
  "devDependencies": {
    "@types/react": "^18.3.0",
    "@types/node": "^20.0.0",
    "autoprefixer": "^10.4.0",
    "postcss": "^8.4.0"
  }
}
```

Install shadcn/ui components individually as needed:

```bash
npx shadcn-ui@latest init
npx shadcn-ui@latest add button badge card dialog drawer input label select slider switch table tabs tooltip
```

---

## 13. Visual Design Direction

The UI should feel like **field-grade tactical software** — dense, high-contrast, information-first. Not a consumer app, not a marketing page. Think SDR# or Wireshark with a dark terminal aesthetic: every pixel earns its place, no whitespace used decoratively.

**Color palette:**

```
Background (base):   #0d1117   — near-black, GitHub Dark-inspired
Surface (cards):     #161b22   — elevated panels
Border:              #30363d   — subtle separator
Text primary:        #e6edf3   — off-white
Text secondary:      #7d8590   — muted labels

Accent (scan active):#00d4aa   — electric teal — the signature color
Accent dim:          #1a3a35   — teal at 15% opacity for backgrounds

Signal strong:       #22c55e   — green (−40 to −60 dBm)
Signal mid:          #eab308   — amber (−60 to −75 dBm)
Signal weak:         #ef4444   — red (< −75 dBm)

Auth open:           #ef4444   — red
Auth WEP:            #f97316   — orange
Auth WPA:            #eab308   — yellow
Auth WPA2:           #22c55e   — green
Auth WPA3:           #3b82f6   — blue
Auth WPA2/3:         #6366f1   — indigo
```

**Typography:**

```
Display / headings:  JetBrains Mono (monospace) — reinforces the terminal feel;
                     used for stat numbers, MAC addresses, headings
Body / labels:       Inter — readable, neutral
Data / tables:       JetBrains Mono — all tabular data, MAC addresses, timestamps
```

**Signature element:** The detection count on the SummaryBar ticks up in real-time as the CSV parses (like a network sniffer counter), using JetBrains Mono with a subtle green glow. It's the one animated moment in an otherwise static UI — it communicates that data is flowing in, and it's memorable.

**Tailwind config extensions:**

```typescript
// tailwind.config.ts
import type { Config } from 'tailwindcss';

export default {
  darkMode: 'class',
  content: ['./src/**/*.{ts,tsx}'],
  theme: {
    extend: {
      colors: {
        base:    '#0d1117',
        surface: '#161b22',
        border:  '#30363d',
        accent:  '#00d4aa',
        'accent-dim': '#1a3a35',
      },
      fontFamily: {
        mono: ['JetBrains Mono', 'monospace'],
        sans: ['Inter', 'sans-serif'],
      },
    },
  },
} satisfies Config;
```

Load fonts in `app/layout.tsx`:

```typescript
import { JetBrains_Mono, Inter } from 'next/font/google';

const mono = JetBrains_Mono({ subsets: ['latin'], variable: '--font-mono' });
const inter = Inter({ subsets: ['latin'], variable: '--font-sans' });
```

---

## 14. Offline Tile Support

For use in the field without internet, pre-cache OpenStreetMap tiles for the session area. A helper script downloads tiles for a bounding box at zoom levels 12–17:

```bash
# tools/cache_tiles.py
# Usage: python cache_tiles.py --bbox "32.7,-117.2,32.72,-117.15" --zoom 12-17
```

Place downloaded tiles at `public/tiles/{z}/{x}/{y}.png`. Configure Leaflet to use local tiles when network is unavailable:

```typescript
const tileUrl = isOnline
  ? 'https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png'
  : '/tiles/{z}/{x}/{y}.png';
```

Detect online status with `navigator.onLine` and a fallback fetch to the OSM tile server.

---

## 15. Testing Without Real Hardware

The repo includes a bundled sample dataset at `public/sample/`:

```
public/sample/
├── wardrive_wigle.csv    — 200 synthetic radius-mode detections
└── wardrive_ext.csv      — same data with full extended fields + 40 cone-mode detections
```

The sample data covers a plausible route through a residential neighborhood (San Diego area lat/lon), includes a mix of encryption types, 3 simulated sensor nodes, one cone-mode segment with `hLockDeg = 127°`, and intentional edge cases:
- 3 hidden networks (empty SSID)
- 2 open networks
- 1 WEP network
- 4 detections with no GPS fix (lat/lon = 0) — should be filtered from map but shown in table
- MAC addresses from common consumer OUIs (Apple, Cisco, Raspberry Pi, ASUS)

The "Load sample data" button on the landing page loads this dataset directly without a file upload.

---

## 16. Async Workstream Split

This frontend can be developed across two people cleanly:

| Workstream | Owner | Deliverable |
|---|---|---|
| Data layer | Person A | `parser.ts`, `summarize.ts`, `filters.ts`, `export.ts`, Zustand store, all TypeScript types |
| Map + visualization | Person B | `DetectionMap`, `HeatmapLayer`, `ConeOverlay`, all Recharts panels |
| Both | Either | Upload UX, NetworkTable, FilterBar, SummaryBar, layout |

**Interface between workstreams:** The TypeScript types in `src/types/detection.ts` and the Zustand store API in `src/store/session.ts` are the contracts. Person B should stub `useFilteredDetections()` returning the sample dataset during development and wire in the real hook once Person A's data layer is done.

Person B can develop against the sample CSV data independently at any time using the "Load sample data" button — no coordination with firmware developers required.