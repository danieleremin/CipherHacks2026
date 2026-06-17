'use client';

import dynamic from 'next/dynamic';
import { useMemo } from 'react';
import { useSessionStore } from '@/store/session';
import { useFilteredDetections } from '@/lib/filters';
import type { Detection } from '@/types/detection';

// Dynamic import Leaflet (no SSR - accesses window)
const MapContent = dynamic(
  () => import('./MapContent').then((mod) => mod.MapContent),
  { ssr: false, loading: () => <div className="w-full h-full bg-base flex items-center justify-center text-text-secondary">Loading map...</div> }
);

export function DetectionMap() {
  const filtered = useFilteredDetections();
  const selectedMac = useSessionStore((s) => s.selectedMac);
  const setSelectedMac = useSessionStore((s) => s.setSelectedMac);

  // Group detections by MAC for clustering
  const networkGroups = useMemo(() => {
    const groups = new Map<string, Detection[]>();
    for (const d of filtered) {
      if (!groups.has(d.mac)) {
        groups.set(d.mac, []);
      }
      groups.get(d.mac)!.push(d);
    }
    return groups;
  }, [filtered]);

  return (
    <MapContent
      detections={filtered}
      networkGroups={networkGroups}
      selectedMac={selectedMac}
      onSelectNetwork={setSelectedMac}
    />
  );
}
