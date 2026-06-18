'use client';

import { useSessionStore } from '@/store/session';
import { useFilteredDetections } from '@/lib/filters';
import { useMemo } from 'react';

export function NodePanel() {
  const session = useSessionStore((s) => s.session);
  const filtered = useFilteredDetections();

  const nodeStats = useMemo(() => {
    if (!session) return [];

    interface NodeStat {
      nodeId: number;
      detections: number;
      networks: Set<string>;
      rssiMin: number;
      rssiMax: number;
      firstUptimeMs: number | null;
      lastUptimeMs: number | null;
    }

    const stats = new Map<number, NodeStat>();

    for (const nodeId of session.summary.nodeIds) {
      stats.set(nodeId, {
        nodeId,
        detections: 0,
        networks: new Set<string>(),
        rssiMin: 0,
        rssiMax: 0,
        firstUptimeMs: null,
        lastUptimeMs: null,
      });
    }

    for (const d of filtered) {
      const stat = stats.get(d.nodeId);
      if (!stat) continue;

      stat.detections++;
      stat.networks.add(d.mac);

      if (stat.rssiMin === 0 || d.rssi < stat.rssiMin) stat.rssiMin = d.rssi;
      if (stat.rssiMax === 0 || d.rssi > stat.rssiMax) stat.rssiMax = d.rssi;

      if (stat.firstUptimeMs === null || d.uptimeMs < stat.firstUptimeMs)
        stat.firstUptimeMs = d.uptimeMs;
      if (stat.lastUptimeMs === null || d.uptimeMs > stat.lastUptimeMs)
        stat.lastUptimeMs = d.uptimeMs;
    }

    return Array.from(stats.values());
  }, [session, filtered]);

  if (nodeStats.length === 0) return null;

  return (
    <div className="bg-surface border border-border rounded-lg p-4 shadow">
      <h3 className="font-mono text-sm font-semibold text-accent mb-3 uppercase tracking-wider">
        Nodes
      </h3>
      <div className="space-y-2">
        {nodeStats.map((stat) => (
          <div key={stat.nodeId} className="p-2 bg-base border border-border rounded text-xs">
            <div className="flex justify-between mb-1">
              <span className="font-mono font-bold text-accent">Node {stat.nodeId}</span>
              <span className="text-text-secondary">{stat.detections} detections</span>
            </div>
            <div className="flex justify-between text-text-secondary">
              <span>{stat.networks.size} networks</span>
              <span>RSSI {stat.rssiMin}–{stat.rssiMax} dBm</span>
            </div>
          </div>
        ))}
      </div>
    </div>
  );
}
