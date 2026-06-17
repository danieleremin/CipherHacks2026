'use client';

import { useMemo } from 'react';
import { useSessionStore } from '@/store/session';
import { useFilteredDetections } from '@/lib/filters';
import { format } from 'date-fns';

export function SummaryBar() {
  const session = useSessionStore((s) => s.session);
  const filtered = useFilteredDetections();

  // Compute summary from filtered detections
  const summary = useMemo(() => {
    const uniqueMacs = new Set(filtered.map((d) => d.mac));
    const uniqueManufacturers = new Set(
      filtered.map((d) => d.manufacturer).filter(Boolean)
    );
    const nodeIds = Array.from(
      new Set(filtered.map((d) => d.nodeId).filter((n): n is number => n !== null))
    );
    const times = filtered.map((d) => d.firstSeen.getTime());
    const duration = times.length > 0 ? Math.max(...times) - Math.min(...times) : 0;
    const durationMs = new Date(duration);
    const avgRssi =
      filtered.length > 0
        ? filtered.reduce((sum, d) => sum + d.rssi, 0) / filtered.length
        : 0;
    const avgHdop =
      filtered.length > 0
        ? filtered
            .filter((d) => d.hdop !== null)
            .reduce((sum, d) => sum + (d.hdop ?? 0), 0) /
          filtered.filter((d) => d.hdop !== null).length
        : null;

    return {
      networks: uniqueMacs.size,
      manufacturers: uniqueManufacturers.size,
      duration:
        durationMs.getUTCHours().toString().padStart(2, '0') +
        ':' +
        durationMs.getUTCMinutes().toString().padStart(2, '0') +
        ':' +
        durationMs.getUTCSeconds().toString().padStart(2, '0'),
      nodes: nodeIds.length,
      avgRssi: avgRssi.toFixed(1),
      avgHdop: avgHdop?.toFixed(1) ?? null,
    };
  }, [filtered]);

  if (!session) return null;

  return (
    <div className="bg-surface border-b border-border px-4 py-3">
      <div className="flex flex-wrap gap-4 items-center text-sm">
        {/* Networks */}
        <div className="flex items-center gap-2">
          <span className="text-text-secondary">Networks:</span>
          <span className="font-mono font-bold text-accent">{summary.networks}</span>
        </div>

        {/* Manufacturers */}
        <div className="flex items-center gap-2 border-l border-border pl-4">
          <span className="text-text-secondary">Manufacturers:</span>
          <span className="font-mono font-bold text-accent">
            {summary.manufacturers}
          </span>
        </div>

        {/* Duration */}
        <div className="flex items-center gap-2 border-l border-border pl-4">
          <span className="text-text-secondary">Duration:</span>
          <span className="font-mono font-bold text-accent">{summary.duration}</span>
        </div>

        {/* Nodes (if multi-node) */}
        {summary.nodes > 1 && (
          <div className="flex items-center gap-2 border-l border-border pl-4">
            <span className="text-text-secondary">Nodes:</span>
            <span className="font-mono font-bold text-accent">{summary.nodes}</span>
          </div>
        )}

        {/* Avg RSSI */}
        <div className="flex items-center gap-2 border-l border-border pl-4">
          <span className="text-text-secondary">Avg RSSI:</span>
          <span className="font-mono font-bold text-accent">{summary.avgRssi} dBm</span>
        </div>

        {/* GPS Quality */}
        {summary.avgHdop !== null && (
          <div className="flex items-center gap-2 border-l border-border pl-4">
            <span className="text-text-secondary">GPS:</span>
            <span
              className={`font-mono font-bold ${
                parseFloat(summary.avgHdop) < 2
                  ? 'text-signal-strong'
                  : parseFloat(summary.avgHdop) < 5
                    ? 'text-signal-mid'
                    : 'text-signal-weak'
              }`}
            >
              {summary.avgHdop} HDOP
            </span>
          </div>
        )}
      </div>
    </div>
  );
}
