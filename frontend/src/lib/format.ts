/**
 * Formatting helpers.
 *
 * GPS (and therefore wall-clock time) was removed from the data pipeline.
 * All temporal display is derived from uptimeMs — milliseconds since the
 * detecting node booted — which is monotonic and relative only.
 */

/**
 * Format a millisecond uptime value as elapsed MM:SS.
 * Used for chart axes and relative timestamps.
 */
export function formatUptimeMMSS(ms: number): string {
  const totalSeconds = Math.floor(ms / 1000);
  const minutes = Math.floor(totalSeconds / 60);
  const seconds = totalSeconds % 60;
  return `${minutes.toString().padStart(2, '0')}:${seconds
    .toString()
    .padStart(2, '0')}`;
}

/**
 * Format a millisecond duration as HH:MM:SS runtime.
 */
export function formatRuntimeHHMMSS(ms: number): string {
  const totalSeconds = Math.floor(ms / 1000);
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const seconds = totalSeconds % 60;
  return [hours, minutes, seconds]
    .map((n) => n.toString().padStart(2, '0'))
    .join(':');
}
