/**
 * Zustand store for application state
 * Manages: session data, filters, UI selections, map layer visibility
 */

import { create } from 'zustand';
import { Detection, Session } from '@/types/detection';
import { FilterState, DEFAULT_FILTERS } from '@/lib/filters';

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
