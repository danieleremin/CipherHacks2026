'use client';

import { useSessionStore } from '@/store/session';
import { useFilteredDetections } from '@/lib/filters';
import { useMemo } from 'react';

export function ConeAnalysis() {
  const session = useSessionStore((s) => s.session);
  const filtered = useFilteredDetections();

  const stats = useMemo(() => {
    if (!session) return { inCone: 0, outCone: 0, avgRange: 0 };

    let inCone = 0;
    let outCone = 0;
    let totalRange = 0;
    let rangeCount = 0;

    for (const d of filtered) {
      if (d.inCone === true) inCone++;
      else if (d.inCone === false) outCone++;

      if (d.rangeM !== null && d.rangeM > 0) {
        totalRange += d.rangeM;
        rangeCount++;
      }
    }

    return {
      inCone,
      outCone,
      avgRange: rangeCount > 0 ? (totalRange / rangeCount).toFixed(1) : '0',
    };
  }, [session, filtered]);

  if (!session || !session.summary.hasConeData) return null;

  return (
    <div className="bg-surface border border-border rounded-lg p-4 shadow">
      <h3 className="font-mono text-sm font-semibold text-accent mb-3 uppercase tracking-wider">
        Cone Analysis
      </h3>
      <div className="space-y-2 text-sm">
        <div className="flex justify-between">
          <span className="text-text-secondary">In-cone:</span>
          <span className="font-mono font-bold text-signal-strong">
            {stats.inCone}
          </span>
        </div>
        <div className="flex justify-between">
          <span className="text-text-secondary">Out-of-cone:</span>
          <span className="font-mono font-bold text-signal-weak">
            {stats.outCone}
          </span>
        </div>
        <div className="flex justify-between">
          <span className="text-text-secondary">Avg range:</span>
          <span className="font-mono font-bold text-accent">
            {stats.avgRange} m
          </span>
        </div>
      </div>
    </div>
  );
}
