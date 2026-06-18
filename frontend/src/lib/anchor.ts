/**
 * Anchor observation correlation and bearing estimation.
 *
 * Node 3 (the anchor) broadcasts a reference beacon. Nodes 1 and 2 both
 * detect it and log it as a regular AP, distinguishable by its known BSSID.
 * By correlating the smoothed RSSI each scanner node reads for the anchor
 * within a short time window, we estimate a bearing from the differential.
 */

import {
  CORRELATION_WINDOW_MS,
  MIN_RSSI_DELTA_DBM,
  RSSI_NOISE_FLOOR_DBM,
} from './constants';
import { Detection, AnchorObservation } from '@/types/detection';

export function buildAnchorObservations(
  detections: Detection[]
): AnchorObservation[] {
  // Separate anchor detections by node
  const node1 = detections.filter((d) => d.isAnchor && d.nodeId === 1);
  const node2 = detections.filter((d) => d.isAnchor && d.nodeId === 2);

  const observations: AnchorObservation[] = [];

  // For each node 1 anchor observation, find a matching node 2 observation
  // within the correlation window
  for (const a of node1) {
    const match = node2.find(
      (b) => Math.abs(b.uptimeMs - a.uptimeMs) <= CORRELATION_WINDOW_MS
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
      confidenceScore = Math.min(
        1.0,
        Math.abs(delta) / (RSSI_NOISE_FLOOR_DBM * 10)
      );
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
