'use client';

import { MapPinOff } from 'lucide-react';

/**
 * No-GPS map state.
 *
 * Phase 2 removed the GPS module entirely — every detection record is
 * logged without coordinates (lat/lon are always null). Rather than render
 * an empty Leaflet map (which would fire tile requests against 0,0), we show
 * a clear empty state. The map remains here as a placeholder for a future
 * GPS integration pass; the Leaflet renderer can be restored from history.
 */
export function DetectionMap() {
  const goToTable = () => {
    document
      .getElementById('network-table')
      ?.scrollIntoView({ behavior: 'smooth', block: 'start' });
  };

  return (
    <div className="w-full h-full bg-base flex items-center justify-center p-8">
      <div className="max-w-sm text-center">
        <MapPinOff className="w-12 h-12 mx-auto mb-4 text-text-secondary opacity-50" />
        <h3 className="font-mono text-base font-semibold text-text-primary mb-2">
          No location data available
        </h3>
        <p className="text-sm text-text-secondary mb-6 leading-relaxed">
          GPS was not present in this session. Detection records are logged
          without coordinates.
        </p>
        <button
          onClick={goToTable}
          className="px-4 py-2 bg-surface border border-accent text-accent font-mono text-sm font-semibold rounded hover:bg-accent hover:text-base transition-colors"
        >
          View network table instead →
        </button>
      </div>
    </div>
  );
}
