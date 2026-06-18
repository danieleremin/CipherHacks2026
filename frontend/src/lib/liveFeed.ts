/**
 * Module-level WebSocket controller for the live R4 feed.
 *
 * It lives outside React's component tree on purpose: the connection must
 * survive the navigation from the home page to /dashboard (which unmounts the
 * page that started it). Components interact with it through startLiveFeed /
 * stopLiveFeed and receive updates via the callbacks they pass in.
 *
 * Incoming frames are buffered and the Session is rebuilt on a trailing timer
 * (FLUSH_MS) rather than on every frame, so a burst of detections doesn't
 * recompute the summary dozens of times per second.
 */

import { Session, Detection } from '@/types/detection';
import { jsonToDetection, buildLiveSession } from './live';

export type LiveStatus = 'connecting' | 'connected' | 'disconnected' | 'error';

interface LiveCallbacks {
  onSession: (session: Session) => void;
  onStatus: (status: LiveStatus) => void;
}

const FLUSH_MS = 400;

let socket: WebSocket | null = null;
let buffer: Detection[] = [];
let flushTimer: ReturnType<typeof setTimeout> | null = null;
let currentUrl = '';
let onSession: ((session: Session) => void) | null = null;

function flush() {
  flushTimer = null;
  if (onSession) onSession(buildLiveSession(buffer.slice(), currentUrl));
}

function scheduleFlush() {
  if (flushTimer === null) flushTimer = setTimeout(flush, FLUSH_MS);
}

/**
 * Open a live connection. Replaces any existing one. Emits an initial empty
 * Session immediately so the dashboard has something to render before the
 * first frame arrives.
 */
export function startLiveFeed(url: string, cbs: LiveCallbacks): void {
  stopLiveFeed();

  currentUrl = url;
  buffer = [];
  onSession = cbs.onSession;
  cbs.onSession(buildLiveSession([], url));

  let sock: WebSocket;
  try {
    sock = new WebSocket(url);
  } catch {
    cbs.onStatus('error');
    return;
  }
  socket = sock;
  cbs.onStatus('connecting');

  sock.onopen = () => cbs.onStatus('connected');
  sock.onerror = () => cbs.onStatus('error');
  sock.onclose = () => {
    if (socket === sock) {
      socket = null;
      cbs.onStatus('disconnected');
    }
  };
  sock.onmessage = (ev) => {
    if (typeof ev.data !== 'string') return;
    let frame: unknown;
    try {
      frame = JSON.parse(ev.data);
    } catch {
      return; // ignore non-JSON noise
    }
    const d = jsonToDetection(frame);
    if (d) {
      buffer.push(d);
      scheduleFlush();
    }
  };
}

/** Close the connection and reset state. Safe to call when already stopped. */
export function stopLiveFeed(): void {
  if (flushTimer !== null) {
    clearTimeout(flushTimer);
    flushTimer = null;
  }
  if (socket) {
    socket.onopen = socket.onclose = socket.onerror = socket.onmessage = null;
    try {
      socket.close();
    } catch {
      /* already closing */
    }
    socket = null;
  }
  buffer = [];
  onSession = null;
}
