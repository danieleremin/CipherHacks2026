/**
 * CSV parsing logic for both WiGLE and extended format CSV files
 * Handles format auto-detection, validation, and row-by-row conversion
 */

import Papa from 'papaparse';
import { Detection, Session, AuthMode } from '@/types/detection';
import { computeSummary } from './summarize';

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
 * Convert a CSV row to a Detection object
 * Returns null if the row is invalid (e.g., no GPS fix, insufficient columns)
 */
function rowToDetection(row: string[], isExtended: boolean): Detection | null {
  // Minimum columns required for WiGLE format
  if (row.length < 12) return null;

  const lat = parseFloat(row[7]);
  const lon = parseFloat(row[8]);

  // Skip rows with no GPS fix (0,0 indicates pre-fix)
  if (lat === 0 && lon === 0) return null;

  // Extended format has additional fields starting at index 12
  const bearingRaw = isExtended ? parseFloat(row[14]) : -1;
  const rangeRaw = isExtended ? parseFloat(row[15]) : -1;
  const hLockRaw = isExtended ? parseFloat(row[17]) : -1;

  return {
    // Core identity
    mac: row[0].trim().toUpperCase(),
    ssid: row[1].trim(),
    authMode: parseAuthMode(row[2]),

    // Temporal
    firstSeen: new Date(row[3].trim() + 'Z'), // Force UTC parsing

    // RF
    channel: parseInt(row[4], 10),
    frequencyMhz: parseInt(row[5], 10),
    rssi: parseInt(row[6], 10),

    // Position
    lat,
    lon,
    altitudeM: parseFloat(row[9]),
    accuracyM: parseFloat(row[10]),

    // Extended fields
    nodeId: isExtended ? parseInt(row[12], 10) : null,
    manufacturer: isExtended ? (row[13].trim() || null) : null,
    bearingDeg: bearingRaw >= 0 ? bearingRaw : null,
    rangeM: rangeRaw >= 0 ? rangeRaw : null,
    inCone: isExtended ? (row[16] === '1' ? true : false) : null,
    hLockDeg: hLockRaw >= 0 ? hLockRaw : null,
    hdop: isExtended ? parseFloat(row[18]) : null,
    satellites: isExtended ? parseInt(row[19], 10) : null,

    // Computed
    oui: row[0].trim().substring(0, 8).toUpperCase(),
    scanMode: bearingRaw >= 0 ? 'cone' : 'radius',
  };
}

/**
 * Parse a CSV file (either WiGLE or extended format)
 * Returns a Session object with parsed detections and summary
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

        const session: Session = {
          id: `${file.name}-${Date.now()}`,
          filename: file.name,
          detections,
          loadedAt: new Date(),
          summary: computeSummary(detections),
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
