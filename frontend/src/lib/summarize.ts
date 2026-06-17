/**
 * Computes session summary statistics from a set of detections
 * Used during CSV parsing to pre-compute aggregate data
 */

import { Detection, SessionSummary, AuthMode } from '@/types/detection';

/**
 * Compute RSSI histogram buckets (5-dBm ranges)
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
 * Compute bounding box for map
 */
function computeBounds(detections: Detection[]) {
  const validDetections = detections.filter(d => d.lat !== 0 && d.lon !== 0);

  if (validDetections.length === 0) {
    return { north: 0, south: 0, east: 0, west: 0 };
  }

  const lats = validDetections.map(d => d.lat);
  const lons = validDetections.map(d => d.lon);

  return {
    north: Math.max(...lats),
    south: Math.min(...lats),
    east: Math.max(...lons),
    west: Math.min(...lons),
  };
}

/**
 * Compute all summary statistics for a detection set
 */
export function computeSummary(detections: Detection[]): SessionSummary {
  // Basic counts
  const uniqueMacs = new Set(detections.map(d => d.mac));
  const uniqueManufacturers = new Set(
    detections.map(d => d.manufacturer).filter(Boolean)
  );
  const nodeIds = Array.from(
    new Set(detections.map(d => d.nodeId).filter(n => n !== null))
  ) as number[];

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
  for (const d of detections) {
    authModeDistribution[d.authMode]++;
  }

  // Channel distribution
  const channelDistribution: Record<number, number> = {};
  for (const d of detections) {
    channelDistribution[d.channel] = (channelDistribution[d.channel] ?? 0) + 1;
  }

  // Time range
  const times = detections.map(d => d.firstSeen.getTime());
  const timeRange = {
    start: new Date(Math.min(...times)),
    end: new Date(Math.max(...times)),
  };

  // GPS quality
  const hdopValues = detections
    .map(d => d.hdop)
    .filter((h): h is number => h !== null);
  const avgHdop =
    hdopValues.length > 0
      ? hdopValues.reduce((a, b) => a + b, 0) / hdopValues.length
      : null;

  // Check if there's cone data
  const hasConeData = detections.some(d => d.bearingDeg !== null);

  return {
    totalDetections: detections.length,
    uniqueNetworks: uniqueMacs.size,
    uniqueManufacturers: uniqueManufacturers.size,
    timeRange,
    bounds: computeBounds(detections),
    channelDistribution,
    authModeDistribution,
    rssiHistogram: computeRssiHistogram(detections),
    nodeIds,
    hasConeData,
    avgHdop,
  };
}
