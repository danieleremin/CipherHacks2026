/**
 * Live-feed adapters: turn the JSON detection frames the R4 broadcasts over
 * its WebSocket into the same Detection / Session shapes the CSV path produces,
 * so the entire dashboard works unchanged on a live stream.
 *
 * Frame shape (node3 record_to_json -> R4 verbatim):
 *   {"node":1,"schema":1,"mode":0,"uptime":9750,"bssid":"..","ssid":"..",
 *    "rssi":-85,"ch":6,"enc":3,"bearing":-1.0,"range":-1.0,"in_cone":1,
 *    "h_lock":-1.0}
 * The "hello" handshake frame has no "bssid" and is ignored.
 */

import { Detection, Session, AuthMode, NodeId } from '@/types/detection';
import { computeSummary } from './summarize';
import { buildAnchorObservations } from './anchor';
import { ANCHOR_BSSID } from './constants';

// enc_type (shared/packet_schema.h) -> AuthMode. Mirrors parser.ts semantics.
const ENC_TO_AUTH: Record<number, AuthMode> = {
  0: 'OPEN',
  1: 'WEP',
  2: 'WPA',
  3: 'WPA2',
  4: 'WPA3',
  5: 'WPA2/WPA3', // ENC_WPA2E (mixed / enterprise)
};

function channelToFreq(ch: number): number {
  return ch >= 1 && ch <= 14 ? 2412 + (ch - 1) * 5 : 0;
}

/**
 * Convert one parsed JSON frame to a Detection, or null if it isn't a
 * detection record (e.g. the hello frame, or malformed input).
 */
export function jsonToDetection(frame: unknown): Detection | null {
  if (typeof frame !== 'object' || frame === null) return null;
  const f = frame as Record<string, unknown>;

  const bssid = typeof f.bssid === 'string' ? f.bssid.trim().toUpperCase() : '';
  if (!bssid) return null; // hello frame or anything without a MAC

  const num = (v: unknown, fallback = 0) =>
    typeof v === 'number' && Number.isFinite(v) ? v : fallback;

  const nodeRaw = num(f.node, 1);
  const nodeId = (nodeRaw === 2 || nodeRaw === 3 ? nodeRaw : 1) as NodeId;

  const bearing = num(f.bearing, -1);
  const range = num(f.range, -1);
  const hLock = num(f.h_lock, -1);
  const ch = num(f.ch, 0);

  return {
    mac: bssid,
    ssid: typeof f.ssid === 'string' ? f.ssid : '',
    authMode: ENC_TO_AUTH[num(f.enc, 0)] ?? 'UNKNOWN',
    uptimeMs: num(f.uptime, 0),
    channel: ch,
    frequencyMhz: channelToFreq(ch),
    rssi: num(f.rssi, 0),
    lat: null,
    lon: null,
    nodeId,
    schemaVersion: num(f.schema, 1),
    manufacturer: null,
    bearingDeg: bearing >= 0 ? bearing : null,
    rangeM: range >= 0 ? range : null,
    inCone: num(f.in_cone, 0) === 1,
    hLockDeg: hLock >= 0 ? hLock : null,
    oui: bssid.substring(0, 8),
    scanMode: bearing >= 0 ? 'cone' : 'radius',
    isAnchor: bssid === ANCHOR_BSSID,
  };
}

/**
 * Build a Session from the running list of live detections. Recomputes anchor
 * observations and summary the same way parseCSV does, so every downstream
 * panel behaves identically.
 */
export function buildLiveSession(detections: Detection[], url: string): Session {
  const anchorObservations = buildAnchorObservations(detections);
  return {
    id: `live-${url}`,
    filename: `LIVE · ${url}`,
    detections,
    anchorObservations,
    loadedAt: new Date(),
    summary: computeSummary(detections, anchorObservations),
  };
}
