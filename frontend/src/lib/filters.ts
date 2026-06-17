/**
 * Filter state management and filtering operations
 * Memoized hooks for efficient filtered detection computation
 */

import { useMemo } from 'react';
import { useSessionStore } from '@/store/session';
import { Detection, AuthMode } from '@/types/detection';

/**
 * Filter state type definition
 */
export interface FilterState {
  searchText: string;              // Search across SSID, BSSID, Manufacturer
  authModes: AuthMode[];           // Empty = all
  rssiRange: [number, number];     // [min, max] dBm
  channels: number[];              // Empty = all
  nodeIds: number[];               // Empty = all
  inConeOnly: boolean;
  hideUnknownManufacturer: boolean;
  timeRange: [Date, Date] | null;  // null = full session range
}

/**
 * Default filter state: shows everything
 */
export const DEFAULT_FILTERS: FilterState = {
  searchText: '',
  authModes: [],
  rssiRange: [-100, 0],
  channels: [],
  nodeIds: [],
  inConeOnly: false,
  hideUnknownManufacturer: false,
  timeRange: null,
};

/**
 * Hook to get all detections after applying filters
 * Memoized for performance
 */
export function useFilteredDetections(): Detection[] {
  const session = useSessionStore((s) => s.session);
  const filters = useSessionStore((s) => s.filters);

  return useMemo(() => {
    if (!session) return [];

    let d = session.detections;

    // Search filter
    if (filters.searchText) {
      const q = filters.searchText.toLowerCase();
      d = d.filter(
        (x) =>
          x.mac.toLowerCase().includes(q) ||
          x.ssid.toLowerCase().includes(q) ||
          (x.manufacturer ?? '').toLowerCase().includes(q)
      );
    }

    // Auth mode filter
    if (filters.authModes.length > 0) {
      d = d.filter((x) => filters.authModes.includes(x.authMode));
    }

    // RSSI range filter
    d = d.filter(
      (x) =>
        x.rssi >= filters.rssiRange[0] && x.rssi <= filters.rssiRange[1]
    );

    // Channel filter
    if (filters.channels.length > 0) {
      d = d.filter((x) => filters.channels.includes(x.channel));
    }

    // Node ID filter
    if (filters.nodeIds.length > 0) {
      d = d.filter((x) => filters.nodeIds.includes(x.nodeId ?? 0));
    }

    // Cone filter
    if (filters.inConeOnly) {
      d = d.filter((x) => x.inCone === true);
    }

    // Unknown manufacturer filter
    if (filters.hideUnknownManufacturer) {
      d = d.filter((x) => x.manufacturer && x.manufacturer !== 'Unknown');
    }

    // Time range filter
    if (filters.timeRange) {
      d = d.filter(
        (x) =>
          x.firstSeen >= filters.timeRange![0] &&
          x.firstSeen <= filters.timeRange![1]
      );
    }

    return d;
  }, [session, filters]);
}

/**
 * Hook to get deduplicated networks (one row per unique MAC)
 * Returns the detection with the strongest RSSI for each MAC
 */
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
