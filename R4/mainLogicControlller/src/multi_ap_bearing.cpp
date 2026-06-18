// multi_ap_bearing.cpp
// Multi-AP differential RSSI bearing estimator implementation.

#include "multi_ap_bearing.h"
#include <string.h>
#include <math.h>

MultiApBearing::MultiApBearing() : _count(0) {
    memset(_table, 0, sizeof(_table));
}

// ── Table management ───────────────────────────────────────────────────────

int MultiApBearing::_findOrAlloc(const char* bssid) {
    // Search for existing entry
    for (int i = 0; i < _count; i++) {
        if (strncmp(_table[i].bssid, bssid, 17) == 0) return i;
    }

    // Allocate new slot
    if (_count < MAX_TRACKED_APS) {
        int i = _count++;
        memset(&_table[i], 0, sizeof(ApRecord));
        strncpy(_table[i].bssid, bssid, 17);
        _table[i].bssid[17] = '\0';
        _table[i].is_anchor = (strncmp(bssid, ANCHOR_BSSID, 17) == 0);
        return i;
    }

    // Table full — evict the oldest entry with only one node seen
    // (least useful for correlation)
    uint32_t oldest_time = UINT32_MAX;
    int oldest_idx = 0;
    for (int i = 0; i < _count; i++) {
        if (!_table[i].has_node1 || !_table[i].has_node2) {
            uint32_t t = max(_table[i].uptime_node1, _table[i].uptime_node2);
            if (t < oldest_time) {
                oldest_time = t;
                oldest_idx = i;
            }
        }
    }

    memset(&_table[oldest_idx], 0, sizeof(ApRecord));
    strncpy(_table[oldest_idx].bssid, bssid, 17);
    _table[oldest_idx].bssid[17] = '\0';
    _table[oldest_idx].is_anchor = (strncmp(bssid, ANCHOR_BSSID, 17) == 0);
    return oldest_idx;
}

// ── Update ─────────────────────────────────────────────────────────────────

void MultiApBearing::update(const char* bssid, uint8_t node_id,
                             int8_t rssi, uint32_t uptime_ms) {
    if (node_id != 1 && node_id != 2) return;

    int i = _findOrAlloc(bssid);
    if (i < 0) return;

    if (node_id == 1) {
        _table[i].rssi_node1  = rssi;
        _table[i].uptime_node1 = uptime_ms;
        _table[i].has_node1   = true;
    } else {
        _table[i].rssi_node2  = rssi;
        _table[i].uptime_node2 = uptime_ms;
        _table[i].has_node2   = true;
    }
}

// ── Bearing math ───────────────────────────────────────────────────────────
// delta_dbm = rssi_node1 - rssi_node2
//   delta > 0 → node 1 is closer → AP is toward the node-1 side → bearing < 90°
//   delta < 0 → node 2 is closer → AP is toward the node-2 side → bearing > 90°
//   delta = 0 → AP is equidistant → bearing = 90° (directly ahead)
//
// We map delta to bearing via arcsin of a normalized value.
// Clamp to [-1, 1] to avoid domain errors.
// Scale factor 20.0 dBm is the maximum expected delta at close range.

float MultiApBearing::_bearingFromDelta(float delta_dbm) const {
    float normalized = delta_dbm / 20.0f;
    normalized = fmaxf(-1.0f, fminf(1.0f, normalized));
    // asin gives [-pi/2, pi/2]; shift to [0, pi] then convert to degrees
    float rad = asinf(normalized);
    float deg = 90.0f - (rad * 180.0f / (float)M_PI);
    return deg;
}

// Confidence based on how far the delta is above the noise floor.
// Anchor gets a multiplier since its TX power is known.
float MultiApBearing::_confidenceFromDelta(float delta_dbm,
                                            bool is_anchor) const {
    float noise = 1.1f;  // dBm noise floor after 20-sample averaging
    float snr   = fabsf(delta_dbm) / noise;
    float conf  = fminf(1.0f, snr / 10.0f);  // SNR of 10 = full confidence
    if (is_anchor) conf = fminf(1.0f, conf * ANCHOR_CONFIDENCE_MUL);
    return conf;
}

// ── Compute bearing ────────────────────────────────────────────────────────
// Weighted circular mean of bearing estimates from all correlated AP pairs.
// Weights are confidence scores.
// Returns invalid estimate if fewer than MIN_AP_COUNT pairs correlate.

BearingEstimate MultiApBearing::compute() {
    BearingEstimate result = {};
    result.valid = false;

    // Collect weighted bearing estimates
    float sum_sin     = 0.0f;
    float sum_cos     = 0.0f;
    float weight_sum  = 0.0f;
    float delta_sum   = 0.0f;
    uint8_t count     = 0;

    for (int i = 0; i < _count; i++) {
        ApRecord& r = _table[i];

        // Skip if we haven't seen this AP from both nodes
        if (!r.has_node1 || !r.has_node2) continue;

        // Skip if the observations are too far apart in time
        uint32_t t1 = r.uptime_node1;
        uint32_t t2 = r.uptime_node2;
        uint32_t gap = (t1 > t2) ? (t1 - t2) : (t2 - t1);
        if (gap > CORRELATION_WINDOW_MS) continue;

        float delta = (float)r.rssi_node1 - (float)r.rssi_node2;

        // Skip if delta is within noise floor
        if (fabsf(delta) < MIN_RSSI_DELTA_DBM) continue;

        float bearing_rad = _bearingFromDelta(delta) * (float)M_PI / 180.0f;
        float conf        = _confidenceFromDelta(delta, r.is_anchor);

        // Weighted circular mean accumulation
        // Use sin/cos to handle angle wrap-around correctly
        sum_sin    += conf * sinf(bearing_rad);
        sum_cos    += conf * cosf(bearing_rad);
        weight_sum += conf;
        delta_sum  += fabsf(delta);
        count++;
    }

    if (count < MIN_AP_COUNT || weight_sum < 0.001f) {
        return result;  // Not enough data
    }

    // Circular mean
    float mean_rad = atan2f(sum_sin / weight_sum,
                            sum_cos / weight_sum);
    float mean_deg = mean_rad * 180.0f / (float)M_PI;
    if (mean_deg < 0.0f) mean_deg += 360.0f;

    result.bearing_deg    = mean_deg;
    result.confidence     = fminf(1.0f, weight_sum / (float)count);
    result.ap_count       = count;
    result.rssi_delta_avg = delta_sum / (float)count;
    result.valid          = true;

    return result;
}

// ── Expire stale records ───────────────────────────────────────────────────

void MultiApBearing::expire(uint32_t current_uptime_ms) {
    for (int i = 0; i < _count; ) {
        uint32_t last_seen = max(_table[i].uptime_node1,
                                 _table[i].uptime_node2);
        uint32_t age = (current_uptime_ms > last_seen)
                     ? (current_uptime_ms - last_seen) : 0;

        if (age > RECORD_MAX_AGE_MS) {
            // Remove by swapping with last entry
            if (i < _count - 1) {
                _table[i] = _table[_count - 1];
            }
            _count--;
            // Don't increment i — recheck this slot
        } else {
            i++;
        }
    }
}

uint8_t MultiApBearing::correlatedCount() const {
    uint8_t n = 0;
    for (int i = 0; i < _count; i++) {
        if (_table[i].has_node1 && _table[i].has_node2) n++;
    }
    return n;
}