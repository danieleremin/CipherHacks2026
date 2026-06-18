// firmware/lcd-driver/src/oui_lookup.cpp

#include <SD.h>
#include <string.h>
#include "oui_lookup.h"
#include "config.h"

#define OUI_ENTRY_SIZE 26
#define OUI_NAME_LEN   23

static File oui_file;
static uint32_t oui_count = 0;

// ── In-RAM LRU cache ─────────────────────────────────────────────────────
// Indexed by recency: index 0 is most recently used. On a hit, the entry
// is moved to the front; on a miss with a full cache, the last entry is
// evicted to make room.

struct CacheEntry {
    uint8_t oui[3];
    char    name[OUI_NAME_LEN + 1];
    bool    valid;
};

static CacheEntry cache[OUI_CACHE_SIZE];
static uint8_t cache_count = 0;

static int cache_find(const uint8_t* oui) {
    for (uint8_t i = 0; i < cache_count; i++) {
        if (memcmp(cache[i].oui, oui, 3) == 0) return i;
    }
    return -1;
}

static void cache_promote(int idx) {
    if (idx <= 0) return;
    CacheEntry tmp = cache[idx];
    memmove(&cache[1], &cache[0], idx * sizeof(CacheEntry));
    cache[0] = tmp;
}

static void cache_insert(const uint8_t* oui, const char* name) {
    if (cache_count < OUI_CACHE_SIZE) {
        memmove(&cache[1], &cache[0], cache_count * sizeof(CacheEntry));
        cache_count++;
    } else {
        memmove(&cache[1], &cache[0], (OUI_CACHE_SIZE - 1) * sizeof(CacheEntry));
    }
    memcpy(cache[0].oui, oui, 3);
    strncpy(cache[0].name, name, OUI_NAME_LEN);
    cache[0].name[OUI_NAME_LEN] = '\0';
    cache[0].valid = true;
}

// ── SD-backed binary search ─────────────────────────────────────────────

bool oui_lookup_init() {
    oui_file = SD.open(OUI_TABLE_FILENAME, FILE_READ);
    if (!oui_file) return false;
    oui_count = oui_file.size() / OUI_ENTRY_SIZE;
    return true;
}

bool oui_lookup(const uint8_t* mac, char* buf) {
    if (!oui_file || oui_count == 0) {
        strncpy(buf, "Unknown", 23);
        buf[23] = '\0';
        return false;
    }

    int cached = cache_find(mac);
    if (cached >= 0) {
        strncpy(buf, cache[cached].name, OUI_NAME_LEN);
        buf[OUI_NAME_LEN] = '\0';
        cache_promote(cached);
        return true;
    }

    uint32_t target = ((uint32_t)mac[0] << 16) |
                      ((uint32_t)mac[1] << 8)  |
                       (uint32_t)mac[2];

    uint32_t lo = 0, hi = oui_count - 1;

    while (lo <= hi) {
        uint32_t mid = (lo + hi) / 2;
        oui_file.seek(mid * OUI_ENTRY_SIZE);

        uint8_t entry[OUI_ENTRY_SIZE];
        oui_file.read(entry, OUI_ENTRY_SIZE);

        uint32_t oui_val = ((uint32_t)entry[0] << 16) |
                           ((uint32_t)entry[1] << 8)  |
                            (uint32_t)entry[2];

        if (oui_val == target) {
            memcpy(buf, entry + 3, OUI_NAME_LEN);
            buf[OUI_NAME_LEN] = '\0';
            cache_insert(mac, buf);
            return true;
        } else if (oui_val < target) {
            lo = mid + 1;
        } else {
            if (mid == 0) break;
            hi = mid - 1;
        }
    }

    strncpy(buf, "Unknown", 23);
    buf[23] = '\0';
    return false;
}
