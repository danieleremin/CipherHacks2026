/**
 * CSV parsing logic for both WiGLE and extended format CSV files
 * Handles format auto-detection, validation, and row-by-row conversion.
 *
 * Phase 2: GPS columns are dead (always 0). uptime_ms is read from column 18
 * (replaces HDOP), schema_version from column 19. Anchor detections are
 * flagged by BSSID on load.
 */

import Papa from 'papaparse';
import { Detection, Session, AuthMode, NodeId } from '@/types/detection';
import { computeSummary } from './summarize';
import { buildAnchorObservations } from './anchor';
import { ANCHOR_BSSID } from './constants';

/**
 * Parse an AuthMode string from the CSV AuthMode column
 * Format examples: "[WPA2]", "[WEP]", "[ESS]", "[WPA2][WPA3]"
 */
function parseAuthMode(raw: string): AuthMode {
  // Check combinations first
  if (raw.includes('WPA3') && raw.includes('WPA2')) return 'WPA2/WPA3';
  if (raw.includes('WPA3')) return 'WPA3';
  if (raw.includes('WPA2')) return 'WPA2';
  if (raw.includes('WPA')) return 'WPA';
  if (raw.includes('WEP')) return 'WEP';
  if (raw.includes('ESS')) return 'OPEN';
  return 'UNKNOWN';
}

/**
 * Convert a CSV row to a Detection object.
 * Returns null only if the row has too few columns to be valid.
 * GPS is gone, so 0,0 coordinates are no longer treated as "no fix".
 */
function rowToDetection(row: string[], isExtended: boolean): Detection | null {
  if (row.length < 12) return null;

  const uptimeMs = isExtended ? parseFloat(row[18]) : 0;
  const mac = row[0].trim().toUpperCase();

  const nodeIdRaw = isExtended ? parseInt(row[12], 10) : 1;
  const nodeId = (nodeIdRaw === 2 || nodeIdRaw === 3 ? nodeIdRaw : 1) as NodeId;

  const bearingRaw = isExtended ? parseFloat(row[14]) : -1;

  return {
    mac,
    ssid: row[1].trim(),
    authMode: parseAuthMode(row[2]),
    uptimeMs: Number.isFinite(uptimeMs) ? uptimeMs : 0,
    channel: parseInt(row[4], 10),
    frequencyMhz: parseInt(row[5], 10),
    rssi: parseInt(row[6], 10),
    lat: null, // GPS removed
    lon: null,
    nodeId,
    schemaVersion: isExtended ? parseInt(row[19], 10) || 1 : 1,
    manufacturer: isExtended ? row[13].trim() || null : null,
    bearingDeg: bearingRaw >= 0 ? bearingRaw : null,
    rangeM: isExtended
      ? parseFloat(row[15]) >= 0
        ? parseFloat(row[15])
        : null
      : null,
    inCone: isExtended ? row[16] === '1' : null,
    hLockDeg: isExtended
      ? parseFloat(row[17]) >= 0
        ? parseFloat(row[17])
        : null
      : null,
    oui: mac.substring(0, 8),
    scanMode: bearingRaw >= 0 ? 'cone' : 'radius',
    isAnchor: mac === ANCHOR_BSSID,
  };
}

/**
 * Parse a CSV file (either WiGLE or extended format)
 * Returns a Session object with parsed detections, anchor observations
 * and summary.
 */
export async function parseCSV(
  file: File,
  onProgress?: (pct: number) => void
): Promise<Session> {
  return new Promise((resolve, reject) => {
    const detections: Detection[] = [];
    let rowCount = 0;
    let isExtended = false;
    let skipRows = 2; // WiGLE has 2 header rows by default

    Papa.parse<string[]>(file, {
      worker: true, // Parse off main thread for responsiveness
      step: (result) => {
        rowCount++;

        // Detect format from first row
        if (rowCount === 1) {
          const firstCell = result.data[0]?.[0] ?? '';
          if (firstCell.startsWith('WigleWifi')) {
            // WiGLE format: 2 header rows
            skipRows = 2;
            isExtended = false;
          } else if (firstCell === 'MAC') {
            // Might be extended format with 1 header row
            skipRows = 1;
            isExtended = (result.data[0]?.length ?? 0) > 12;
          }
          return;
        }

        // Skip header rows
        if (rowCount <= skipRows) {
          return;
        }

        // Convert row to Detection
        const d = rowToDetection(result.data, isExtended);
        if (d) {
          detections.push(d);
        }

        // Progress callback every 500 rows
        if (onProgress && rowCount % 500 === 0) {
          onProgress(Math.min(90, (rowCount / 5000) * 90));
        }
      },
      complete: () => {
        if (onProgress) onProgress(95);

        const anchorObservations = buildAnchorObservations(detections);

        const session: Session = {
          id: `${file.name}-${Date.now()}`,
          filename: file.name,
          detections,
          anchorObservations,
          loadedAt: new Date(),
          summary: computeSummary(detections, anchorObservations),
        };

        if (onProgress) onProgress(100);
        resolve(session);
      },
      error: (error) => {
        reject(new Error(`CSV parse error: ${error.message}`));
      },
    });
  });
}
