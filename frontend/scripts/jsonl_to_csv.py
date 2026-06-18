#!/usr/bin/env python3
"""
Convert node3/R4 JSON detection lines into the extended-CSV format the
frontend ingests (the same 20-column layout gen_sample.py produces).

The R4 broadcasts one JSON object per detection over its WebSocket, e.g.

  {"node":1,"schema":1,"mode":0,"uptime":9750,"bssid":"2E:7B:C8:EA:A6:EA",
   "ssid":"SD Library Events","rssi":-85,"ch":6,"enc":3,
   "bearing":-1.0,"range":-1.00,"in_cone":1,"h_lock":-1.0}

Usage
-----
  # From a captured file:
  websocat ws://192.168.4.1:8080 > capture.jsonl
  python scripts/jsonl_to_csv.py capture.jsonl -o public/sample/live.csv

  # Or stream straight from the device:
  websocat ws://192.168.4.1:8080 | python scripts/jsonl_to_csv.py -o public/sample/live.csv

Non-JSON lines, the "hello" handshake frame, and any object without a
"bssid" field are skipped, so you can point it at a raw capture safely.
"""

import argparse
import csv
import json
import sys

# enc_type (shared/packet_schema.h) -> AuthMode token the frontend parser
# (src/lib/parser.ts:parseAuthMode) keys on. WPA2E is the mixed/enterprise
# code; "[WPA2][WPA3]" makes the parser report WPA2/WPA3.
ENC_TO_AUTH = {
    0: "[ESS]",          # ENC_OPEN  -> parsed as OPEN
    1: "[WEP]",
    2: "[WPA]",
    3: "[WPA2]",
    4: "[WPA3]",
    5: "[WPA2][WPA3]",   # ENC_WPA2E -> parsed as WPA2/WPA3
}

# Must match EXT_HEADER in gen_sample.py exactly.
EXT_HEADER = [
    "MAC", "SSID", "AuthMode", "FirstSeen", "Channel", "Frequency", "RSSI",
    "CurrentLatitude", "CurrentLongitude", "AltitudeMeters", "AccuracyMeters",
    "Type", "NodeID", "Manufacturer", "BearingDeg", "RangeM", "InCone",
    "HLockDeg", "UptimeMs", "SchemaVersion",
]


def channel_to_freq(ch):
    """2.4 GHz channel -> centre frequency in MHz (matches gen_sample.py)."""
    if isinstance(ch, int) and 1 <= ch <= 14:
        return 2412 + (ch - 1) * 5
    return 0


def fmt_uptime(ms):
    """uptime in ms -> mm:ss, the FirstSeen format gen_sample.py uses."""
    try:
        s = int(ms) // 1000
    except (TypeError, ValueError):
        return "00:00"
    return f"{s // 60:02d}:{s % 60:02d}"


def record_to_row(rec):
    """Map one parsed JSON detection to an extended-CSV row, or None to skip."""
    bssid = rec.get("bssid")
    if not bssid:
        return None  # hello frame or anything that isn't a detection

    ch = rec.get("ch", 0)
    uptime = rec.get("uptime", 0)
    enc = rec.get("enc", 0)

    return [
        bssid,                              # MAC
        rec.get("ssid", ""),                # SSID
        ENC_TO_AUTH.get(enc, "[ESS]"),      # AuthMode (unknown enc -> OPEN)
        fmt_uptime(uptime),                 # FirstSeen
        ch,                                 # Channel
        channel_to_freq(ch),                # Frequency
        rec.get("rssi", 0),                 # RSSI
        0, 0, 0, -1,                        # Lat, Lon, Alt, Accuracy (GPS gone)
        "WIFI",                             # Type
        rec.get("node", 1),                 # NodeID
        "Unknown",                          # Manufacturer (no OUI lookup here)
        rec.get("bearing", -1),             # BearingDeg (<0 => null in UI)
        rec.get("range", -1),               # RangeM     (<0 => null in UI)
        rec.get("in_cone", 0),              # InCone
        rec.get("h_lock", -1),              # HLockDeg   (<0 => null in UI)
        uptime,                             # UptimeMs
        rec.get("schema", 1),               # SchemaVersion
    ]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("infile", nargs="?", help="JSONL capture file (default: stdin)")
    ap.add_argument("-o", "--out", help="output CSV path (default: stdout)")
    args = ap.parse_args()

    src = open(args.infile) if args.infile else sys.stdin
    dst = open(args.out, "w", newline="") if args.out else sys.stdout

    writer = csv.writer(dst)
    writer.writerow(EXT_HEADER)

    written = skipped = 0
    try:
        for line in src:
            line = line.strip()
            if not line:
                continue
            try:
                rec = json.loads(line)
            except json.JSONDecodeError:
                skipped += 1
                continue
            row = record_to_row(rec)
            if row is None:
                skipped += 1
                continue
            writer.writerow(row)
            written += 1
    finally:
        if args.infile:
            src.close()
        if args.out:
            dst.close()

    print(f"wrote {written} rows; skipped {skipped} non-detection lines",
          file=sys.stderr)


if __name__ == "__main__":
    main()
