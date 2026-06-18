/**
 * Zustand store for application state
 * Manages: session data, filters, UI selections, map layer visibility,
 * and anchor / bearing selection.
 */

import { create } from 'zustand';
import { useMemo } from 'react';
import { Session, AnchorObservation } from '@/types/detection';
import { FilterState, DEFAULT_FILTERS } from '@/lib/filters';
import { ANCHOR_BSSID } from '@/lib/constants';

export interface SessionStore {
  // Session data
  session: Session | null;
  setSession: (session: Session) => void;
  clearSession: () => void;

  // Filter state
  filters: FilterState;
  setFilter: <K extends keyof FilterState>(
    key: K,
    value: FilterState[K]
  ) => void;
  resetFilters: () => void;

  // Selection state
  selectedMac: string | null;
  setSelectedMac: (mac: string | null) => void;

  // Anchor
  anchorBssid: string; // From constants — not user-configurable
  selectedObservationIdx: number | null; // Which bearing observation is highlighted
  setSelectedObservation: (idx: number | null) => void;

  // Map layer visibility
  showHeatmap: boolean;
  showPathTrace: boolean;
  showCone: boolean;
  toggleLayer: (layer: 'heatmap' | 'pathTrace' | 'cone') => void;
}

export const useSessionStore = create<SessionStore>((set) => ({
  // Session data
  session: null,
  setSession: (session) => set({ session }),
  clearSession: () =>
    set({
      session: null,
      selectedMac: null,
      selectedObservationIdx: null,
      filters: DEFAULT_FILTERS,
    }),

  // Filter state
  filters: DEFAULT_FILTERS,
  setFilter: (key, value) =>
    set((state) => ({
      filters: { ...state.filters, [key]: value },
    })),
  resetFilters: () => set({ filters: DEFAULT_FILTERS }),

  // Selection state
  selectedMac: null,
  setSelectedMac: (mac) => set({ selectedMac: mac }),

  // Anchor
  anchorBssid: ANCHOR_BSSID,
  selectedObservationIdx: null,
  setSelectedObservation: (idx) => set({ selectedObservationIdx: idx }),

  // Map layer visibility
  showHeatmap: false,
  showPathTrace: false,
  showCone: true,
  toggleLayer: (layer) =>
    set((state) => ({
      showHeatmap:
        layer === 'heatmap' ? !state.showHeatmap : state.showHeatmap,
      showPathTrace:
        layer === 'pathTrace' ? !state.showPathTrace : state.showPathTrace,
      showCone: layer === 'cone' ? !state.showCone : state.showCone,
    })),
}));

/**
 * Derived hook — all correlated anchor observations for the current session.
 */
export function useAnchorObservations(): AnchorObservation[] {
  const session = useSessionStore((s) => s.session);
  return session?.anchorObservations ?? [];
}

/**
 * Derived hook — the most recent anchor observation that produced a bearing
 * estimate (by uptimeMs), or null if there is none.
 */
export function useLatestBearing(): AnchorObservation | null {
  const observations = useAnchorObservations();
  return useMemo(() => {
    let latest: AnchorObservation | null = null;
    for (const o of observations) {
      if (o.bearingEstimateDeg === null) continue;
      if (!latest || o.uptimeMs > latest.uptimeMs) latest = o;
    }
    return latest;
  }, [observations]);
}
