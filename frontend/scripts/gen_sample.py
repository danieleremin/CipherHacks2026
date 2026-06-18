#!/usr/bin/env python3
"""
Regenerate synthetic sample wardriving data for the Phase 2 schema.

- No GPS: lat/lon/alt = 0, accuracy = -1
- uptime_ms (col 18) increasing from 0 to ~1,800,000 (30 minutes)
- schema_version = 1 (col 19)
- Records from node 1 and node 2 interleaved
- Anchor BSSID rows from both nodes, node1 ~6-10 dBm stronger than node2
- >= 40 anchor observation pairs within the 500ms correlation window
- A cone-mode segment: 30 rows with bearing populated, in_cone mixing 0/1
"""

import random

random.seed(7)

ANCHOR_BSSID = "30:76:F5:06:28:C5"

MANUFACTURERS = [
    "Cisco Systems", "Apple Inc", "Raspberry Pi Trading Ltd", "Netgear",
    "TP-Link", "Ubiquiti Networks", "Espressif", "Samsung Electronics",
    "Unknown",
]
AUTH = ["[WPA2]", "[WPA2][WPA3]", "[WPA3]", "[WEP]", "[ESS]", "[WPA]"]
CHANNELS = [1, 6, 11, 3, 9, 13, 4]


def freq(ch):
    return 2412 + (ch - 1) * 5


def fmt_uptime(ms):
    s = ms // 1000
    return f"{s // 60:02d}:{s % 60:02d}"


SSIDS = [
    "HomeNetwork_2G", "CoffeeShop", "(hidden)", "LibraryWiFi",
    "Office-Guest", "eduroam", "xfinitywifi", "ATT-WiFi", "Linksys",
    "", "NETGEAR47", "PrintServer", "IoT-Hub", "Garage", "Studio",
]

# (mac, ssid, auth, channel, manufacturer, base_rssi)
networks = []
for i in range(1, 16):
    networks.append((
        f"AA:BB:CC:DD:EE:{i:02X}",
        SSIDS[(i - 1) % len(SSIDS)],
        random.choice(AUTH),
        random.choice(CHANNELS),
        random.choice(MANUFACTURERS),
        random.randint(-82, -50),
    ))

# rows: list of dicts with all extended fields
rows = []


def add_row(mac, ssid, auth, ch, rssi, node, manuf, uptime,
            bearing=-1, rangem=-1, incone=0, hlock=-1):
    rows.append({
        "mac": mac, "ssid": ssid, "auth": auth, "ch": ch,
        "freq": freq(ch), "rssi": rssi, "node": node,
        "manuf": manuf, "bearing": bearing, "rangem": rangem,
        "incone": incone, "hlock": hlock, "uptime": uptime,
    })


# --- Regular AP detections, interleaved across both nodes over ~30 min ---
TOTAL_MS = 1_800_000
n_obs = 320
for k in range(n_obs):
    uptime = int(k / n_obs * TOTAL_MS) + random.randint(0, 400)
    net = random.choice(networks)
    mac, ssid, auth, ch, manuf, base = net
    node = 1 if random.random() < 0.5 else 2
    rssi = max(-95, min(-40, base + random.randint(-6, 6)))
    add_row(mac, ssid, auth, ch, rssi, node, manuf, uptime)

# --- Anchor observation pairs: node1 consistently 6-10 dBm stronger ---
N_PAIRS = 48
for p in range(N_PAIRS):
    uptime1 = int((p + 0.5) / N_PAIRS * TOTAL_MS)
    rssi1 = -52 + random.randint(-2, 2)            # node 1 stronger
    delta = random.randint(6, 10)
    rssi2 = rssi1 - delta                          # node 2 weaker
    uptime2 = uptime1 + random.randint(80, 300)    # within 500ms window
    add_row(ANCHOR_BSSID, "ANCHOR-REF", "[WPA2]", 6, rssi1, 1,
            "Espressif", uptime1)
    add_row(ANCHOR_BSSID, "ANCHOR-REF", "[WPA2]", 6, rssi2, 2,
            "Espressif", uptime2)

# --- Cone-mode segment: 30 rows with bearing populated, in_cone 0 and 1 ---
cone_start = 900_000
for j in range(30):
    uptime = cone_start + j * 1500
    net = random.choice(networks)
    mac, ssid, auth, ch, manuf, base = net
    node = 1 if j % 2 == 0 else 2
    bearing = random.randint(0, 359)
    incone = 1 if j % 3 != 0 else 0
    rangem = round(random.uniform(2.0, 18.0), 1)
    hlock = random.randint(0, 359)
    rssi = max(-95, min(-40, base + random.randint(-5, 5)))
    add_row(mac, ssid, auth, ch, rssi, node, manuf, uptime,
            bearing=bearing, rangem=rangem, incone=incone, hlock=hlock)

# Sort by uptime so the file reads chronologically
rows.sort(key=lambda r: r["uptime"])

# --- Write extended CSV ---
EXT_HEADER = ("MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,"
              "CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,"
              "Type,NodeID,Manufacturer,BearingDeg,RangeM,InCone,HLockDeg,"
              "UptimeMs,SchemaVersion")

with open("public/sample/wardrive_ext.csv", "w") as f:
    f.write(EXT_HEADER + "\n")
    for r in rows:
        f.write(",".join(str(x) for x in [
            r["mac"], r["ssid"], r["auth"], fmt_uptime(r["uptime"]),
            r["ch"], r["freq"], r["rssi"],
            0, 0, 0, -1, "WIFI",
            r["node"], r["manuf"], r["bearing"], r["rangem"],
            r["incone"], r["hlock"], r["uptime"], 1,
        ]) + "\n")

# --- Write WiGLE CSV (12 cols, GPS zeroed, no node/ext fields, no anchor) ---
WIGLE_PRE = ("WigleWifi-1.4,appRelease=2.0,model=WardriverR4,release=2.0,"
             "device=ESP32Mesh,display=LCD,board=UnoR4WiFi,brand=Custom")
WIGLE_HEADER = ("MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,"
                "CurrentLatitude,CurrentLongitude,AltitudeMeters,"
                "AccuracyMeters,Type")

with open("public/sample/wardrive_wigle.csv", "w") as f:
    f.write(WIGLE_PRE + "\n")
    f.write(WIGLE_HEADER + "\n")
    for r in rows:
        if r["mac"] == ANCHOR_BSSID:
            continue
        f.write(",".join(str(x) for x in [
            r["mac"], r["ssid"], r["auth"], fmt_uptime(r["uptime"]),
            r["ch"], r["freq"], r["rssi"],
            0, 0, 0, -1, "WIFI",
        ]) + "\n")

print("wrote", len(rows), "extended rows;",
      sum(1 for r in rows if r["mac"] == ANCHOR_BSSID), "anchor rows")
