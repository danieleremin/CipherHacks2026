'use client';

import { useSessionStore } from '@/store/session';
import { X } from 'lucide-react';

export function FilterBar() {
  const filters = useSessionStore((s) => s.filters);
  const setFilter = useSessionStore((s) => s.setFilter);
  const resetFilters = useSessionStore((s) => s.resetFilters);

  const activeFilterCount =
    (filters.searchText ? 1 : 0) +
    (filters.authModes.length > 0 ? 1 : 0) +
    (filters.channels.length > 0 ? 1 : 0) +
    (filters.nodeIds.length > 0 ? 1 : 0) +
    (filters.inConeOnly ? 1 : 0) +
    (filters.hideUnknownManufacturer ? 1 : 0) +
    (filters.hideAnchor ? 1 : 0);

  if (activeFilterCount === 0) return null;

  return (
    <div className="bg-surface border border-border rounded-lg p-3 flex flex-wrap gap-2 items-center justify-between">
      <div className="flex flex-wrap gap-2">
        {filters.searchText && (
          <div className="flex items-center gap-2 px-3 py-1 bg-base border border-border rounded text-sm">
            <span className="text-text-secondary">Search:</span>
            <span className="font-mono text-accent">{filters.searchText}</span>
            <button
              onClick={() => setFilter('searchText', '')}
              className="ml-1 hover:text-signal-weak transition-colors"
            >
              <X className="w-4 h-4" />
            </button>
          </div>
        )}

        {filters.authModes.length > 0 && (
          <div className="flex items-center gap-2 px-3 py-1 bg-base border border-border rounded text-sm">
            <span className="text-text-secondary">Auth:</span>
            <span className="font-mono text-accent">
              {filters.authModes.join(', ')}
            </span>
            <button
              onClick={() => setFilter('authModes', [])}
              className="ml-1 hover:text-signal-weak transition-colors"
            >
              <X className="w-4 h-4" />
            </button>
          </div>
        )}

        {filters.channels.length > 0 && (
          <div className="flex items-center gap-2 px-3 py-1 bg-base border border-border rounded text-sm">
            <span className="text-text-secondary">Channels:</span>
            <span className="font-mono text-accent">
              {filters.channels.join(', ')}
            </span>
            <button
              onClick={() => setFilter('channels', [])}
              className="ml-1 hover:text-signal-weak transition-colors"
            >
              <X className="w-4 h-4" />
            </button>
          </div>
        )}

        {filters.inConeOnly && (
          <div className="flex items-center gap-2 px-3 py-1 bg-base border border-border rounded text-sm">
            <span className="font-mono text-accent">In-cone only</span>
            <button
              onClick={() => setFilter('inConeOnly', false)}
              className="ml-1 hover:text-signal-weak transition-colors"
            >
              <X className="w-4 h-4" />
            </button>
          </div>
        )}

        {filters.hideAnchor && (
          <div className="flex items-center gap-2 px-3 py-1 bg-base border border-border rounded text-sm">
            <span className="font-mono text-accent">Anchor hidden</span>
            <button
              onClick={() => setFilter('hideAnchor', false)}
              className="ml-1 hover:text-signal-weak transition-colors"
            >
              <X className="w-4 h-4" />
            </button>
          </div>
        )}
      </div>

      <button
        onClick={resetFilters}
        className="px-3 py-1 text-xs font-mono bg-base border border-border rounded hover:border-accent transition-colors text-text-secondary hover:text-accent"
      >
        Clear all
      </button>
    </div>
  );
}
