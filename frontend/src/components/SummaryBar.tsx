'use client';

import { useMemo } from 'react';
import { useSessionStore, useLatestBearing } from '@/store/session';
import { useFilteredDetections } from '@/lib/filters';
import { formatRuntimeHHMMSS } from '@/lib/format';

export function SummaryBar() {
  const session = useSessionStore((s) => s.session);
  const filtered = useFilteredDetections();
  const latestBearing = useLatestBearing();

  // Compute summary from filtered detections
  const summary = useMemo(() => {
    const real = filtered.filter((d) => !d.isAnchor);
    const uniqueMacs = new Set(real.map((d) => d.mac));
    const uniqueManufacturers = new Set(
      real.map((d) => d.manufacturer).filter(Boolean)
    );
    const nodeIds = new Set(filtered.map((d) => d.nodeId));
    const uptimes = filtered.map((d) => d.uptimeMs);
    const durationMs =
      uptimes.length > 0 ? Math.max(...uptimes) - Math.min(...uptimes) : 0;
    const avgRssi =
      real.length > 0
        ? real.reduce((sum, d) => sum + d.rssi, 0) / real.length
        : 0;

    return {
      networks: uniqueMacs.size,
      manufacturers: uniqueManufacturers.size,
      runtime: formatRuntimeHHMMSS(durationMs),
      nodes: nodeIds.size,
      avgRssi: avgRssi.toFixed(0),
    };
  }, [filtered]);

  if (!session) return null;

  return (
    <div className="bg-surface border-b border-border px-4 py-3">
      <div className="flex flex-wrap gap-4 items-center text-sm">
        {/* Networks */}
        <div className="flex items-center gap-2">
          <span className="text-text-secondary">Networks:</span>
          <span className="font-mono font-bold text-accent">
            {summary.networks}
          </span>
        </div>

        {/* Manufacturers */}
        <div className="flex items-center gap-2 border-l border-border pl-4">
          <span className="text-text-secondary">Manufacturers:</span>
          <span className="font-mono font-bold text-accent">
            {summary.manufacturers}
          </span>
        </div>

        {/* Runtime (uptime-derived) */}
        <div className="flex items-center gap-2 border-l border-border pl-4">
          <span className="text-text-secondary">Runtime:</span>
          <span className="font-mono font-bold text-accent">
            {summary.runtime}
          </span>
        </div>

        {/* Nodes */}
        <div className="flex items-center gap-2 border-l border-border pl-4">
          <span className="text-text-secondary">Nodes:</span>
          <span className="font-mono font-bold text-accent">
            {summary.nodes}
          </span>
        </div>

        {/* Avg RSSI */}
        <div className="flex items-center gap-2 border-l border-border pl-4">
          <span className="text-text-secondary">Avg RSSI:</span>
          <span className="font-mono font-bold text-accent">
            {summary.avgRssi} dBm
          </span>
        </div>

        {/* Bearing (only when anchor data is present) */}
        {session.summary.hasAnchorData &&
          latestBearing?.bearingEstimateDeg != null && (
            <div className="flex items-center gap-2 border-l border-border pl-4">
              <span className="text-text-secondary">Bearing:</span>
              <span className="font-mono font-bold text-accent">
                {Math.round(latestBearing.bearingEstimateDeg)}°
                <span className="text-text-secondary ml-1">
                  ({Math.round(latestBearing.confidenceScore * 100)}%)
                </span>
              </span>
            </div>
          )}
      </div>
    </div>
  );
}
