'use client';

import { useEffect } from 'react';
import { useRouter } from 'next/navigation';
import { useSessionStore } from '@/store/session';
import { SummaryBar } from '@/components/SummaryBar';
import { DetectionMap } from '@/components/map/DetectionMap';
import { NetworkTable } from '@/components/NetworkTable';
import { RSSIChart } from '@/components/RSSIChart';
import { ChannelChart } from '@/components/ChannelChart';
import { TimelineChart } from '@/components/TimelineChart';
import { AuthPieChart } from '@/components/AuthPieChart';
import { ConeAnalysis } from '@/components/ConeAnalysis';
import { NodePanel } from '@/components/NodePanel';
import { BearingPanel } from '@/components/BearingPanel';
import { FilterBar } from '@/components/FilterBar';
import { Navbar } from '@/components/Navbar';

// GPS was removed from the hardware, so there are no coordinates to plot.
// The map panel is hidden rather than deleted — flip this to true to restore
// it if/when GPS data returns. (DetectionMap is currently a no-GPS placeholder.)
const SHOW_MAP = false;

export default function DashboardPage() {
  const router = useRouter();
  const session = useSessionStore((s) => s.session);

  // Redirect to home if no session loaded
  useEffect(() => {
    if (!session) {
      router.push('/');
    }
  }, [session, router]);

  if (!session) {
    return (
      <div className="min-h-screen bg-base flex items-center justify-center">
        <div className="text-text-secondary">Loading...</div>
      </div>
    );
  }

  return (
    <div className="min-h-screen bg-base">
      {/* Navbar */}
      <Navbar />

      {/* Summary Bar */}
      <SummaryBar />

      {/* Main Layout */}
      <div className="flex flex-col lg:flex-row gap-4 p-4 max-w-full">
        {/* Left: Map (60% on desktop, full width on mobile) — hidden, no GPS */}
        {SHOW_MAP && (
          <div className="flex-1 lg:w-3/5">
            <div className="bg-surface border border-border rounded-lg overflow-hidden shadow-lg" style={{ height: '600px' }}>
              <DetectionMap />
            </div>
          </div>
        )}

        {/* Right: Panels — full width when the map is hidden */}
        <div
          className={`flex-1 space-y-4 ${
            SHOW_MAP ? 'lg:w-2/5 overflow-y-auto' : 'lg:w-full'
          }`}
          style={SHOW_MAP ? { maxHeight: '600px' } : undefined}
        >
          {/* Networks Table */}
          <div
            id="network-table"
            className="bg-surface border border-border rounded-lg p-4 shadow"
          >
            <h3 className="font-mono text-sm font-semibold text-accent mb-3 uppercase tracking-wider">
              Networks
            </h3>
            <NetworkTable />
          </div>

          {/* Node Panel (if multi-node) */}
          {session.summary.nodeIds.length > 1 && (
            <NodePanel />
          )}

          {/* Cone Analysis (if cone mode data exists) */}
          {session.summary.hasConeData && (
            <ConeAnalysis />
          )}
        </div>
      </div>

      {/* Bearing estimation (only when anchor data is present) */}
      {session.summary.hasAnchorData && (
        <div className="px-4">
          <BearingPanel />
        </div>
      )}

      {/* Charts Section */}
      <div className="p-4 space-y-4">
        {/* Filter Bar */}
        <FilterBar />

        {/* Charts Grid */}
        <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4">
          {/* RSSI Histogram */}
          <div className="bg-surface border border-border rounded-lg p-4 shadow">
            <h3 className="font-mono text-sm font-semibold text-accent mb-3 uppercase tracking-wider">
              RSSI Distribution
            </h3>
            <RSSIChart />
          </div>

          {/* Channel Distribution */}
          <div className="bg-surface border border-border rounded-lg p-4 shadow">
            <h3 className="font-mono text-sm font-semibold text-accent mb-3 uppercase tracking-wider">
              Channel Usage
            </h3>
            <ChannelChart />
          </div>

          {/* Auth Mode Breakdown */}
          <div className="bg-surface border border-border rounded-lg p-4 shadow">
            <h3 className="font-mono text-sm font-semibold text-accent mb-3 uppercase tracking-wider">
              Auth Types
            </h3>
            <AuthPieChart />
          </div>
        </div>

        {/* Timeline */}
        <div className="bg-surface border border-border rounded-lg p-4 shadow">
          <h3 className="font-mono text-sm font-semibold text-accent mb-3 uppercase tracking-wider">
            Detection Timeline
          </h3>
          <TimelineChart />
        </div>
      </div>
    </div>
  );
}
