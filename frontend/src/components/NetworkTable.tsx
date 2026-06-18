'use client';

import { useUniqueNetworks } from '@/lib/filters';
import { useSessionStore } from '@/store/session';
import { useMemo } from 'react';

export function NetworkTable() {
  const unique = useUniqueNetworks();
  const selectedMac = useSessionStore((s) => s.selectedMac);
  const setSelectedMac = useSessionStore((s) => s.setSelectedMac);
  const hideAnchor = useSessionStore((s) => s.filters.hideAnchor);
  const setFilter = useSessionStore((s) => s.setFilter);

  // Anchor rows sort to the top (most important reference), then by RSSI.
  const sorted = useMemo(() => {
    return [...unique].sort((a, b) => {
      if (a.isAnchor !== b.isAnchor) return a.isAnchor ? -1 : 1;
      return b.rssi - a.rssi;
    });
  }, [unique]);

  const AUTH_COLORS: Record<string, string> = {
    OPEN: 'bg-auth-open/20 text-auth-open',
    WEP: 'bg-auth-wep/20 text-auth-wep',
    WPA: 'bg-auth-wpa/20 text-auth-wpa',
    WPA2: 'bg-auth-wpa2/20 text-auth-wpa2',
    WPA3: 'bg-auth-wpa3/20 text-auth-wpa3',
    'WPA2/WPA3': 'bg-auth-wpa23/20 text-auth-wpa23',
    UNKNOWN: 'bg-border text-text-secondary',
  };

  return (
    <div>
      {/* Hide anchor toggle */}
      <div className="flex justify-end mb-2">
        <label className="flex items-center gap-2 text-xs font-mono text-text-secondary cursor-pointer select-none">
          <input
            type="checkbox"
            checked={hideAnchor}
            onChange={(e) => setFilter('hideAnchor', e.target.checked)}
            className="accent-accent"
          />
          Hide anchor
        </label>
      </div>

      <div className="overflow-x-auto max-h-96 overflow-y-auto border border-border rounded">
        <table className="w-full text-xs font-mono">
          <thead className="sticky top-0 bg-base border-b border-border">
            <tr>
              <th className="px-2 py-2 text-left text-text-secondary">BSSID</th>
              <th className="px-2 py-2 text-left text-text-secondary">SSID</th>
              <th className="px-2 py-2 text-right text-text-secondary">RSSI</th>
              <th className="px-2 py-2 text-center text-text-secondary">Auth</th>
              <th className="px-2 py-2 text-center text-text-secondary">Ch</th>
            </tr>
          </thead>
          <tbody>
            {sorted.map((detection) => {
              const isSelected = detection.mac === selectedMac;
              return (
                <tr
                  key={detection.mac}
                  onClick={() => setSelectedMac(detection.mac)}
                  className={`border-b border-border cursor-pointer transition-colors ${
                    isSelected
                      ? 'bg-accent-dim'
                      : detection.isAnchor
                        ? 'bg-accent/5 hover:bg-accent/10'
                        : 'hover:bg-base'
                  }`}
                >
                  <td className="px-2 py-2 truncate max-w-120px">
                    {detection.isAnchor && (
                      <span className="inline-block mr-1 px-1.5 py-0.5 rounded bg-accent/20 text-accent text-[10px] font-bold uppercase tracking-wider">
                        Anchor
                      </span>
                    )}
                    <span className="text-accent font-bold">
                      {detection.mac.substring(0, 8)}
                    </span>
                    <span className="text-text-secondary">
                      {detection.mac.substring(8)}
                    </span>
                  </td>
                  <td className="px-2 py-2 truncate max-w-100px text-text-secondary">
                    {detection.ssid || <em className="italic">(hidden)</em>}
                  </td>
                  <td className="px-2 py-2 text-right text-text-primary">
                    {detection.rssi}
                  </td>
                  <td className="px-2 py-2 text-center">
                    <span
                      className={`inline-block px-2 py-0.5 rounded text-xs font-bold ${
                        AUTH_COLORS[detection.authMode] || AUTH_COLORS.UNKNOWN
                      }`}
                    >
                      {detection.authMode === 'OPEN'
                        ? 'O'
                        : detection.authMode.charAt(0)}
                    </span>
                  </td>
                  <td className="px-2 py-2 text-center text-text-primary">
                    {detection.channel}
                  </td>
                </tr>
              );
            })}
          </tbody>
        </table>
        {sorted.length === 0 && (
          <div className="p-4 text-center text-text-secondary text-xs">
            No networks match current filters
          </div>
        )}
      </div>
    </div>
  );
}
