/**
 * Computes session summary statistics from a set of detections
 * Used during CSV parsing to pre-compute aggregate data.
 *
 * Phase 2: GPS-derived stats (bounds, HDOP) are gone. Time is expressed
 * as uptimeMs ranges. Anchor detections are excluded from network counts
 * and summarized separately via AnchorObservation[].
 */

import {
  Detection,
  SessionSummary,
  AuthMode,
  NodeId,
  AnchorObservation,
} from '@/types/detection';

/**
 * Compute RSSI histogram buckets (10-dBm ranges)
 */
function computeRssiHistogram(
  detections: Detection[]
): { bucket: string; count: number }[] {
  const buckets: Record<string, number> = {
    '-30 to -40': 0,
    '-40 to -50': 0,
    '-50 to -60': 0,
    '-60 to -70': 0,
    '-70 to -80': 0,
    '-80 to -90': 0,
    '< -90': 0,
  };

  for (const d of detections) {
    const rssi = d.rssi;
    if (rssi > -40) buckets['-30 to -40']++;
    else if (rssi > -50) buckets['-40 to -50']++;
    else if (rssi > -60) buckets['-50 to -60']++;
    else if (rssi > -70) buckets['-60 to -70']++;
    else if (rssi > -80) buckets['-70 to -80']++;
    else if (rssi > -90) buckets['-80 to -90']++;
    else buckets['< -90']++;
  }

  return Object.entries(buckets).map(([bucket, count]) => ({ bucket, count }));
}

/**
 * Compute all summary statistics for a detection set
 */
export function computeSummary(
  detections: Detection[],
  anchorObservations: AnchorObservation[]
): SessionSummary {
  // Real AP detections exclude anchor observations
  const realDetections = detections.filter((d) => !d.isAnchor);

  // Basic counts (exclude anchor from unique network count)
  const uniqueMacs = new Set(realDetections.map((d) => d.mac));
  const uniqueManufacturers = new Set(
    realDetections.map((d) => d.manufacturer).filter(Boolean)
  );
  const nodeIds = Array.from(
    new Set(detections.map((d) => d.nodeId))
  ).sort((a, b) => a - b) as NodeId[];

  // Auth mode distribution
  const authModeDistribution: Record<AuthMode, number> = {
    OPEN: 0,
    WEP: 0,
    WPA: 0,
    WPA2: 0,
    WPA3: 0,
    'WPA2/WPA3': 0,
    UNKNOWN: 0,
  };
  for (const d of realDetections) {
    authModeDistribution[d.authMode]++;
  }

  // Channel distribution
  const channelDistribution: Record<number, number> = {};
  for (const d of realDetections) {
    channelDistribution[d.channel] = (channelDistribution[d.channel] ?? 0) + 1;
  }

  // Uptime range (ms, monotonic and relative)
  const uptimes = detections.map((d) => d.uptimeMs);
  const start = uptimes.length > 0 ? Math.min(...uptimes) : 0;
  const end = uptimes.length > 0 ? Math.max(...uptimes) : 0;

  const hasConeData = detections.some((d) => d.bearingDeg !== null);
  const hasAnchorData = anchorObservations.length > 0;

  return {
    totalDetections: detections.length,
    uniqueNetworks: uniqueMacs.size,
    uniqueManufacturers: uniqueManufacturers.size,
    uptimeRange: { start, end },
    durationMs: end - start,
    channelDistribution,
    authModeDistribution,
    rssiHistogram: computeRssiHistogram(realDetections),
    nodeIds,
    hasConeData,
    hasAnchorData,
    bearingEstimates: anchorObservations,
  };
}
