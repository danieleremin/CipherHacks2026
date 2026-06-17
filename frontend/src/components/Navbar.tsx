'use client';

import Link from 'next/link';
import { useSessionStore } from '@/store/session';
import { Menu } from 'lucide-react';

export function Navbar() {
  const session = useSessionStore((s) => s.session);
  const clearSession = useSessionStore((s) => s.clearSession);

  return (
    <nav className="bg-surface border-b border-border px-4 py-3 shadow">
      <div className="flex items-center justify-between max-w-full">
        {/* Logo */}
        <Link href="/" className="font-mono text-lg font-bold text-accent">
          WARDRIVING
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
