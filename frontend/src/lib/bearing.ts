// src/lib/bearing.ts
// Multi-AP differential RSSI bearing estimator — frontend implementation.
// Mirrors the logic in multi_ap_bearing.cpp exactly so post-session
// analysis in the frontend matches real-time R4 output.

export const ANCHOR_BSSID          = '30:76:F5:06:28:C5';
export const NODE_BASELINE_M       = 0.20;
export const CORRELATION_WINDOW_MS = 2000;
export const MIN_RSSI_DELTA_DBM    = 2.0;
export const RECORD_MAX_AGE_MS     = 10000;
export const MIN_AP_COUNT          = 2;
export const ANCHOR_CONFIDENCE_MUL = 2.0;
export const RSSI_NOISE_FLOOR      = 1.1;  // dBm after 20-sample averaging

export interface ApRecord {
    bssid:       string;
    rssiNode1:   number;
    rssiNode2:   number;
    uptimeNode1: number;
    uptimeNode2: number;
    hasNode1:    boolean;
    hasNode2:    boolean;
    isAnchor:    boolean;
}

export interface BearingEstimate {
    bearingDeg:   number;
    confidence:   number;
    apCount:      number;
    rssiDeltaAvg: number;
    valid:        boolean;
}

function bearingFromDelta(deltaDpm: number): number {
    const normalized = Math.max(-1, Math.min(1, deltaDpm / 20));
    const rad = Math.asin(normalized);
    return 90 - (rad * 180 / Math.PI);
}

function confidenceFromDelta(deltaDpm: number, isAnchor: boolean): number {
    const snr  = Math.abs(deltaDpm) / RSSI_NOISE_FLOOR;
    const conf = Math.min(1, snr / 10);
    return isAnchor ? Math.min(1, conf * ANCHOR_CONFIDENCE_MUL) : conf;
}

export class MultiApBearing {
    private table = new Map<string, ApRecord>();

    update(bssid: string, nodeId: 1 | 2, rssi: number, uptimeMs: number) {
        let rec = this.table.get(bssid);
        if (!rec) {
            rec = {
                bssid,
                rssiNode1: 0,   rssiNode2: 0,
                uptimeNode1: 0, uptimeNode2: 0,
                hasNode1: false, hasNode2: false,
                isAnchor: bssid.toUpperCase() === ANCHOR_BSSID.toUpperCase(),
            };
            this.table.set(bssid, rec);
        }

        if (nodeId === 1) {
            rec.rssiNode1   = rssi;
            rec.uptimeNode1 = uptimeMs;
            rec.hasNode1    = true;
        } else {
            rec.rssiNode2   = rssi;
            rec.uptimeNode2 = uptimeMs;
            rec.hasNode2    = true;
        }
    }

    compute(): BearingEstimate {
        let sumSin = 0, sumCos = 0, weightSum = 0, deltaSum = 0, count = 0;

        for (const rec of this.table.values()) {
            if (!rec.hasNode1 || !rec.hasNode2) continue;

            const gap = Math.abs(rec.uptimeNode1 - rec.uptimeNode2);
            if (gap > CORRELATION_WINDOW_MS) continue;

            const delta = rec.rssiNode1 - rec.rssiNode2;
            if (Math.abs(delta) < MIN_RSSI_DELTA_DBM) continue;

            const bearingRad = bearingFromDelta(delta) * Math.PI / 180;
            const conf       = confidenceFromDelta(delta, rec.isAnchor);

            sumSin    += conf * Math.sin(bearingRad);
            sumCos    += conf * Math.cos(bearingRad);
            weightSum += conf;
            deltaSum  += Math.abs(delta);
            count++;
        }

        if (count < MIN_AP_COUNT || weightSum < 0.001) {
            return { bearingDeg: 0, confidence: 0, apCount: 0,
                     rssiDeltaAvg: 0, valid: false };
        }

        let meanRad = Math.atan2(sumSin / weightSum, sumCos / weightSum);
        let meanDeg = meanRad * 180 / Math.PI;
        if (meanDeg < 0) meanDeg += 360;

        return {
            bearingDeg:   Math.round(meanDeg * 10) / 10,
            confidence:   Math.round(Math.min(1, weightSum / count) * 100) / 100,
            apCount:      count,
            rssiDeltaAvg: Math.round(deltaSum / count * 10) / 10,
            valid:        true,
        };
    }

    expire(currentUptimeMs: number) {
        for (const [bssid, rec] of this.table.entries()) {
            const lastSeen = Math.max(rec.uptimeNode1, rec.uptimeNode2);
            if (currentUptimeMs - lastSeen > RECORD_MAX_AGE_MS) {
                this.table.delete(bssid);
            }
        }
    }

    get tableSize()      { return this.table.size; }
    get correlatedCount() {
        let n = 0;
        for (const r of this.table.values()) {
            if (r.hasNode1 && r.hasNode2) n++;
        }
        return n;
    }
}

// ── Batch processing for post-session analysis ────────────────────────────
// Feed a full Detection[] array and get bearing estimates over time.
// Groups by time bucket (BUCKET_MS) and runs the correlator per bucket.

export interface TimedBearingEstimate extends BearingEstimate {
    uptimeMs: number;
}

export function buildBearingTimeline(
    detections: { bssid: string; nodeId: 1|2; rssi: number; uptimeMs: number }[],
    bucketMs = 2000
): TimedBearingEstimate[] {
    if (detections.length === 0) return [];

    // Sort by time
    const sorted = [...detections].sort((a, b) => a.uptimeMs - b.uptimeMs);

    const results: TimedBearingEstimate[] = [];
    const estimator = new MultiApBearing();

    let bucketStart = sorted[0].uptimeMs;
    let bucketEnd   = bucketStart + bucketMs;

    for (const d of sorted) {
        if (d.uptimeMs > bucketEnd) {
            // End of bucket — compute estimate and advance
            const est = estimator.compute();
            if (est.valid) {
                results.push({ ...est, uptimeMs: bucketStart + bucketMs / 2 });
            }
            estimator.expire(d.uptimeMs);
            bucketStart = bucketEnd;
            bucketEnd   = bucketStart + bucketMs;
        }
        estimator.update(d.bssid, d.nodeId, d.rssi, d.uptimeMs);
    }

    // Final bucket
    const last = estimator.compute();
    if (last.valid) {
        results.push({ ...last, uptimeMs: bucketStart + bucketMs / 2 });
    }

    return results;
}