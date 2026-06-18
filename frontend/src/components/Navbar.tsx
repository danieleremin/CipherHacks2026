'use client';

import Link from 'next/link';
import { useSessionStore } from '@/store/session';
import { stopLiveFeed } from '@/lib/liveFeed';

const LIVE_DOT: Record<string, string> = {
  connecting: 'bg-yellow-400 animate-pulse',
  connected: 'bg-signal-strong animate-pulse',
  disconnected: 'bg-signal-weak',
  error: 'bg-signal-weak',
};

export function Navbar() {
  const session = useSessionStore((s) => s.session);
  const clearSession = useSessionStore((s) => s.clearSession);
  const liveStatus = useSessionStore((s) => s.liveStatus);
  const setLiveStatus = useSessionStore((s) => s.setLiveStatus);
  const isLive = liveStatus !== 'off';

  return (
    <nav className="bg-surface border-b border-border px-4 py-3 shadow">
      <div className="flex items-center justify-between max-w-full">
        {/* Logo */}
        <Link href="/" className="font-mono text-lg font-bold text-accent">
          Hyperlocal Access Monitoring & Observation Network
        </Link>

        {/* Center: Session name */}
        {session && (
          <div className="flex-1 text-center">
            <span className="font-mono text-sm text-text-secondary truncate">
              {session.filename}
            </span>
          </div>
        )}

        {/* Right: Actions */}
        <div className="flex items-center gap-3">
          {isLive && (
            <div className="flex items-center gap-2">
              <span
                className={`inline-block w-2 h-2 rounded-full ${
                  LIVE_DOT[liveStatus] ?? 'bg-text-secondary'
                }`}
              />
              <span className="text-xs font-mono uppercase tracking-wider text-text-secondary">
                {liveStatus}
              </span>
              <button
                onClick={() => {
                  stopLiveFeed();
                  setLiveStatus('off');
                }}
                className="px-3 py-1 text-sm font-mono bg-base border border-border rounded hover:border-signal-weak transition-colors text-text-secondary hover:text-signal-weak"
              >
                Disconnect
              </button>
            </div>
          )}
          {session && (
            <button
              onClick={() => {
                clearSession();
              }}
              className="px-3 py-1 text-sm font-mono bg-base border border-border rounded hover:border-accent transition-colors text-text-secondary hover:text-accent"
            >
              + Upload
            </button>
          )}
        </div>
      </div>
    </nav>
  );
}
