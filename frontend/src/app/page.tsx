'use client';

import { useState } from 'react';
import { useRouter } from 'next/navigation';
import { useSessionStore } from '@/store/session';
import { parseCSV } from '@/lib/parser';
import { startLiveFeed, stopLiveFeed } from '@/lib/liveFeed';
import { Upload, AlertCircle, CheckCircle, Radio } from 'lucide-react';

export default function HomePage() {
  const router = useRouter();
  const setSession = useSessionStore((s) => s.setSession);
  const setLiveStatus = useSessionStore((s) => s.setLiveStatus);
  const setLiveBearing = useSessionStore((s) => s.setLiveBearing);

  // A CSV session is not a live session — tear down any live feed and clear
  // its status so the dashboard doesn't show a stale "Disconnect" control.
  const resetLiveState = () => {
    stopLiveFeed();
    setLiveStatus('off');
    setLiveBearing(null);
  };

  const [files, setFiles] = useState<File[]>([]);
  const [loading, setLoading] = useState(false);
  const [progress, setProgress] = useState(0);
  const [error, setError] = useState<string | null>(null);
  const [liveUrl, setLiveUrl] = useState('ws://192.168.4.1:8080');
  const [connecting, setConnecting] = useState(false);

  // Handle file selection from input or drop
  const handleFiles = (newFiles: File[]) => {
    const csvFiles = newFiles.filter((f) => f.name.endsWith('.csv'));
    if (csvFiles.length === 0) {
      setError('Please select CSV files');
      return;
    }
    setFiles(csvFiles);
    setError(null);
  };

  // Drag and drop handlers
  const handleDrop = (e: React.DragEvent<HTMLDivElement>) => {
    e.preventDefault();
    handleFiles(Array.from(e.dataTransfer.files));
  };

  const handleDragOver = (e: React.DragEvent<HTMLDivElement>) => {
    e.preventDefault();
  };

  // Parse CSV files
  const handleLoad = async () => {
    if (files.length === 0) {
      setError('Please select a CSV file');
      return;
    }

    setLoading(true);
    setError(null);
    setProgress(0);
    resetLiveState();

    try {
      // Parse the first CSV file (extended format preferred)
      const csvFile = files[0];
      const session = await parseCSV(csvFile, (pct) => setProgress(pct));

      setSession(session);
      router.push('/dashboard');
    } catch (err) {
      setError(
        err instanceof Error ? err.message : 'Failed to parse CSV file'
      );
      setLoading(false);
    }
  };

  // Connect to a live R4 WebSocket feed. We only navigate to the dashboard
  // once the socket actually opens — if it errors, closes, or never responds
  // within the timeout, we stay on this page and show an error instead of
  // displaying an empty dashboard.
  const handleConnectLive = () => {
    const url = liveUrl.trim();
    if (!/^wss?:\/\//.test(url)) {
      setError('Enter a WebSocket URL, e.g. ws://192.168.4.1:8080');
      return;
    }
    setError(null);
    setLiveBearing(null);
    setConnecting(true);

    let settled = false;
    const fail = (msg: string) => {
      if (settled) return;
      settled = true;
      stopLiveFeed();
      setLiveStatus('off');
      setConnecting(false);
      setError(msg);
    };
    const timer = setTimeout(
      () =>
        fail(
          `Could not reach ${url}. Join the R4's WiFi AP and check the address/port.`
        ),
      6000
    );

    startLiveFeed(url, {
      onSession: setSession,
      onBearing: setLiveBearing,
      onStatus: (status) => {
        setLiveStatus(status);
        if (settled) return;
        if (status === 'connected') {
          settled = true;
          clearTimeout(timer);
          setConnecting(false);
          router.push('/dashboard');
        } else if (status === 'error' || status === 'disconnected') {
          clearTimeout(timer);
          fail(
            "Couldn't connect to the R4. Is it powered, and are you on its WiFi AP?"
          );
        }
      },
    });
  };

  // Load sample data
  const handleSampleData = async () => {
    setLoading(true);
    setError(null);
    setProgress(0);
    resetLiveState();

    try {
      // Fetch sample CSV from public folder
      const response = await fetch('/sample/wardrive_ext.csv');
      const csv = await response.text();
      const blob = new Blob([csv], { type: 'text/csv' });
      const file = new File([blob], 'sample_wardrive.csv', {
        type: 'text/csv',
      });

      const session = await parseCSV(file, (pct) => setProgress(pct));
      setSession(session);
      router.push('/dashboard');
    } catch (err) {
      setError('Failed to load sample data');
      setLoading(false);
    }
  };

  return (
    <div className="min-h-screen bg-base flex items-center justify-center p-4">
      <div className="w-full max-w-md">
        {/* Header */}
        <div className="text-center mb-8">
          <div className="text-4xl font-mono font-bold text-accent mb-2">
            Hyperlocal Access Monitoring & Observation Network
          </div>
          <div className="text-text-secondary">
            Post-mission WiFi detection analysis
          </div>
        </div>

        {/* Upload Card */}
        <div className="bg-surface border border-border rounded-lg p-8 shadow-lg">
          {/* Drop Zone */}
          <div
            onDrop={handleDrop}
            onDragOver={handleDragOver}
            className="border-2 border-dashed border-border rounded-lg p-8 text-center mb-6 hover:border-accent transition-colors cursor-pointer bg-accent-dim"
          >
            <Upload className="w-12 h-12 mx-auto mb-3 text-accent opacity-50" />
            <p className="text-text-primary mb-1">Drop CSV files here</p>
            <p className="text-sm text-text-secondary mb-4">
              or click to browse
            </p>
            <input
              type="file"
              multiple
              accept=".csv"
              onChange={(e) => handleFiles(Array.from(e.target.files || []))}
              className="hidden"
              id="file-input"
            />
            <label
              htmlFor="file-input"
              className="inline-block px-4 py-2 bg-accent text-base font-mono text-sm font-semibold rounded hover:opacity-90 transition-opacity cursor-pointer"
            >
              Select Files
            </label>
          </div>

          {/* File List */}
          {files.length > 0 && (
            <div className="mb-6 space-y-2">
              {files.map((file) => (
                <div
                  key={file.name}
                  className="flex items-center gap-3 p-3 bg-base border border-border rounded"
                >
                  <CheckCircle className="w-4 h-4 text-signal-strong flex-shrink-0" />
                  <div className="flex-1 min-w-0">
                    <div className="font-mono text-sm truncate">
                      {file.name}
                    </div>
                    <div className="text-xs text-text-secondary">
                      {(file.size / 1024).toFixed(1)} KB
                    </div>
                  </div>
                </div>
              ))}
            </div>
          )}

          {/* Progress Bar */}
          {loading && progress > 0 && (
            <div className="mb-6">
              <div className="flex justify-between mb-2">
                <span className="text-sm font-mono text-text-secondary">
                  Parsing...
                </span>
                <span className="text-sm font-mono text-accent">
                  {Math.round(progress)}%
                </span>
              </div>
              <div className="h-2 bg-base border border-border rounded overflow-hidden">
                <div
                  className="h-full bg-accent transition-all duration-300"
                  style={{ width: `${progress}%` }}
                />
              </div>
            </div>
          )}

          {/* Error Message */}
          {error && (
            <div className="mb-6 flex gap-3 p-3 bg-signal-weak/10 border border-signal-weak/30 rounded">
              <AlertCircle className="w-5 h-5 text-signal-weak flex-shrink-0 mt-0.5" />
              <div className="text-sm text-signal-weak">{error}</div>
            </div>
          )}

          {/* Load Button */}
          <button
            onClick={handleLoad}
            disabled={loading || files.length === 0}
            className="w-full py-3 bg-accent text-base font-mono font-semibold rounded hover:opacity-90 disabled:opacity-50 disabled:cursor-not-allowed transition-opacity mb-4"
          >
            {loading ? 'Parsing CSV...' : 'Load Session →'}
          </button>

          {/* Divider */}
          <div className="relative mb-6">
            <div className="absolute inset-0 flex items-center">
              <div className="w-full border-t border-border" />
            </div>
            <div className="relative flex justify-center">
              <span className="px-2 bg-surface text-xs text-text-secondary uppercase tracking-wider">
                or
              </span>
            </div>
          </div>

          {/* Sample Data Button */}
          <button
            onClick={handleSampleData}
            disabled={loading}
            className="w-full py-3 bg-surface border border-accent text-accent font-mono font-semibold rounded hover:bg-accent hover:text-base transition-colors disabled:opacity-50 disabled:cursor-not-allowed"
          >
            Load Sample Data
          </button>

          {/* Live Device Connection */}
          <div className="mt-6 pt-6 border-t border-border">
            <label
              htmlFor="live-url"
              className="block text-xs text-text-secondary uppercase tracking-wider mb-2"
            >
              Connect to live device
            </label>
            <div className="flex gap-2">
              <input
                id="live-url"
                type="text"
                value={liveUrl}
                onChange={(e) => setLiveUrl(e.target.value)}
                placeholder="ws://192.168.4.1:8080"
                spellCheck={false}
                disabled={connecting}
                className="flex-1 min-w-0 px-3 py-2 bg-base border border-border rounded font-mono text-sm text-text-primary focus:border-accent focus:outline-none disabled:opacity-50"
              />
              <button
                onClick={handleConnectLive}
                disabled={loading || connecting}
                className="flex items-center gap-2 px-4 py-2 bg-accent text-base font-mono text-sm font-semibold rounded hover:opacity-90 transition-opacity disabled:opacity-50 disabled:cursor-not-allowed"
              >
                <Radio className={`w-4 h-4 ${connecting ? 'animate-pulse' : ''}`} />
                {connecting ? 'Connecting…' : 'Connect'}
              </button>
            </div>
            <p className="text-xs text-text-secondary mt-2">
              Join the R4&apos;s WiFi AP first, then connect to its WebSocket.
            </p>
          </div>
        </div>

        {/* Footer */}
        <div className="text-center mt-8 text-sm text-text-secondary">
          <p>Supports WiGLE and extended CSV formats</p>
          <p>All data processed locally in your browser</p>
        </div>
      </div>
    </div>
  );
}
