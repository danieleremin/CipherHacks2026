# Wardriving / RF Direction-Finding System

A multi-node system that scans for WiFi access points, estimates their bearing and
range from the scanning rig, and streams the detections to a browser dashboard in
real time. Built for CipherHacks 2026.

The system is made of small, single-purpose firmware images that talk to each other
over **ESP-NOW** (radio) and **UART / WiFi WebSocket** (wired + downstream). Each
firmware lives in its own folder and builds with [PlatformIO](https://platformio.org/).

---

## 1. How it works

```
  ┌──────────────┐   ESP-NOW    ┌─────────────────────┐  serial (JSON) ┌──────────────┐   WiFi WS   ┌───────────┐
  │ Scanner node │── (record) ─▶│ node3               │── (lines) ────▶│ Arduino R4   │── ws://… ──▶│ Frontend  │
  │ node1, node2 │              │ anchor + base       │                │ WiFi bridge  │             │ dashboard │
  │ (ESP32)      │◀── (sync) ───│ receiver (ESP32)    │                │ (relay + AP) │             │ (browser) │
  └──────────────┘              └─────────────────────┘                └──────────────┘             └───────────┘
```

1. **Scanner nodes (node1, node2)** — ESP32 boards that hop WiFi channels, sniff
   beacons/probe responses, smooth the RSSI, tag each detection with an IMU heading
   and a rangefinder distance, pack it into a fixed `WardrivingRecord` struct
   (`shared/packet_schema.h`), and **ESP-NOW unicast it to the base node**.

2. **node3 — anchor + base receiver** (`sensors`, env `node3_anchor`) — an ESP32 that
   receives the ESP-NOW records from the scanners. It also broadcasts a fixed-channel
   reference beacon the scanners use for differential RSSI, and periodically tells the
   scanners to lock onto the anchor channel for correlated readings. It serializes each
   received record to a **JSON line** and prints it on its serial, which is wired into
   the R4.

3. **Arduino R4 WiFi (`R4/mainLogicControlller`)** — a **dumb relay**. It reads node3's
   JSON lines over a wired serial link, hosts its own WiFi access point, runs a WebSocket
   server, and forwards every JSON line to connected clients **verbatim**. It does not
   parse or re-encode records; it only filters out node3's non-JSON diagnostic lines. An
   onboard LED matrix flashes as records are forwarded.

4. **Frontend** — a browser dashboard that connects to the R4's WebSocket. *Out of
   scope for this README.*

The on-the-wire JSON the frontend consumes is shown in §5 below.

### Architecture note

node3 emits JSON text lines and the R4 forwards them verbatim, so the two connect
**directly over serial** — node3 TX → R4 RX1 (pin 0), through a level shifter (see §6).

An earlier design used **`old/espnow_bridge`**, a dedicated ESP32 that received ESP-NOW
records and emitted them as length-prefixed **binary** frames (`shared/uart_bridge_protocol.h`).
The current R4 firmware is a JSON line relay and **no longer parses those binary frames**,
so that bridge has been retired to `old/` (see §2) along with the previous LCD logger.

---

## 2. Repository structure

```
CipherHacks2026/
├── sensors/                 ESP32 firmware: scanner nodes + node3 anchor/base receiver
│   ├── platformio.ini       envs: node1, node2, node3_anchor
│   └── src/                 WiFi sniffer, IMU, rangefinder, RSSI smoothing, ESP-NOW, anchor
├── R4/
│   └── mainLogicControlller/  Arduino R4 WiFi base node (JSON relay + WiFi AP + WebSocket)
│       ├── platformio.ini     env: uno_r4_wifi
│       └── src/               main (serial line reader), web_export (WS), led_matrix
├── shared/                  Contracts shared by the ESP32 firmwares — DO NOT edit unilaterally
│   ├── packet_schema.h      WardrivingRecord struct + enc/mode/command constants (ESP-NOW)
│   └── uart_bridge_protocol.h  Binary UART framing — used only by the archived old/ firmware
├── old/                    Retired firmware, kept for reference only — not part of the pipeline
│   ├── espnow_bridge/       ESP32 ESP-NOW↔UART binary bridge (former R4 upstream)
│   └── lcd_driver/          Mega2560 LCD/SD logger
├── frontend/               Browser dashboard (out of scope here)
└── README.md               You are here
```

> Everything under `old/` is retired and not part of the current pipeline — skip it.

---

## 3. Prerequisites

- **PlatformIO Core** (`pip install platformio`, or the VS Code extension). All build
  and flash commands below use the `pio` CLI.
- USB cables for each board. Flash **one board at a time** and note which serial port
  each enumerates as.
- For WebSocket testing without the frontend: a CLI WebSocket client such as
  [`websocat`](https://github.com/vi/websocat) (`brew install websocat`) or
  `wscat` (`npm i -g wscat`).

All firmwares log at **115200 baud**.

---

## 4. Configure node identities and MAC addresses

ESP-NOW is addressed by MAC, so the nodes must know each other's addresses before they
can talk. These live in `sensors/src/config.h`:

```c
#define BASE_NODE_MAC  { 0x30, 0x76, 0xF5, 0x06, 0x28, 0xC5 }  // node3 anchor's AP MAC
#define SCANNER_MAC_1  { 0xE0, 0x8C, 0xFE, 0xE6, 0x49, 0xD0 }  // node1
#define SCANNER_MAC_2  { 0x8C, 0x94, 0xDF, 0x93, 0xC7, 0xA0 }  // node2
```

- Scanners **unicast** their records to `BASE_NODE_MAC`. Set this to the receiver's MAC:
  node3 prints it at boot as `[ANCHOR] BSSID : XX:XX:...`.
- node3 sends channel-sync commands to `SCANNER_MAC_1/2`. Read those from each scanner's
  boot log (or from the `esptool` MAC line during flashing).

> node3 is the receiver in the live pipeline, so `BASE_NODE_MAC` should be node3's AP MAC.
> (The old `espnow_bridge` upstream has been retired to `old/` — see §1–2.)

---

## 5. Build, flash, and test each part separately

### 5a. ESP32 scanner nodes (`sensors`, node1 / node2)

```bash
cd sensors
pio run -e node1 -t upload      # flash node 1
pio run -e node2 -t upload      # flash node 2 (swap the USB cable first)
pio device monitor -b 115200    # watch the boot + diag output
```

What to expect on the monitor:

```
[BOOT] Wardriving scanner node, NODE_ID=0x01
[STATE] SCANNING — node 1 online, ch hop active
[DIAG] node=1 uptime=5s ch=6 | sent=142 fail=0 | IMU: hdg=271.3° lock=-1.0° | range=1.84m | tracked BSSIDs=9
```

- `tracked BSSIDs` climbing ⇒ the WiFi sniffer sees networks.
- `sent` climbing with `fail=0` ⇒ ESP-NOW transmit to `BASE_NODE_MAC` is succeeding.
- `fail` climbing ⇒ usually a wrong/unreachable `BASE_NODE_MAC` or a channel mismatch.

**Test scanning without ESP-NOW (no receiver needed):** uncomment `-DDEBUG_SERIAL_ONLY`
in `sensors/platformio.ini`'s `[env:base]` build flags, re-flash, and the node prints each
detection straight to its own serial instead of transmitting:

```
[NODE 01] BSSID=2E:7B:C8:EA:A6:EA SSID='SD Library Events' RSSI=-85 ch=6 enc=3 uptime=9750ms bearing=271.3 range=1.84 in_cone=1
```

### 5b. ESP32 node3 — anchor + base receiver (`sensors`, node3_anchor)

```bash
cd sensors
pio run -e node3_anchor -t upload
pio device monitor -b 115200
```

What to expect:

```
[BOOT] Wardriving ANCHOR + BASE RECEIVER, NODE_ID=0x03
[ANCHOR] SSID    : WARDRIVE_ANCHOR
[ANCHOR] BSSID   : 30:76:F5:06:28:C5      ← copy this into BASE_NODE_MAC for the scanners
[ANCHOR] Channel : 6
[ANCHOR] Waiting for scanner records...
[DIAG] anchor uptime=5s rx=0 bssid=30:76:F5:06:28:C5
```

- Copy the printed `BSSID` into the scanners' `BASE_NODE_MAC` (§4) and re-flash them.
- Once scanners are running, `rx=` climbs and **JSON lines** appear on this serial — these
  are exactly what gets forwarded downstream:

```json
{"node":1,"schema":1,"mode":0,"uptime":9750,"bssid":"2E:7B:C8:EA:A6:EA","ssid":"SD Library Events","rssi":-85,"ch":6,"enc":3,"bearing":-1.0,"range":-1.00,"in_cone":1,"h_lock":-1.0}
```

> Standalone smoke test (no scanners): the anchor still boots, broadcasts its
> `WARDRIVE_ANCHOR` beacon (visible in any phone's WiFi list), and prints `[DIAG]` with
> `rx=0`. That confirms WiFi AP + ESP-NOW init succeeded even before any records arrive.

### 5c. Arduino R4 WiFi base node (`R4/mainLogicControlller`)

```bash
cd R4/mainLogicControlller
pio run -e uno_r4_wifi -t upload
pio device monitor -b 115200
```

What to expect:

```
Wardriver R4 ready — relaying node3 JSON to WebSocket.
[web] AP "wardriver-r4" up, ws://192.168.4.1:8080
```

Diagnostic lines from node3 (anything not starting with `{`) are echoed here prefixed
with `[NODE3]`; JSON detection lines are forwarded to WebSocket clients, not printed.

**Test the WebSocket relay without the frontend:**

1. On your laptop, join the WiFi network **`wardriver-r4`** (password `wardrive123`).
2. Connect a CLI WebSocket client to the R4 (IP printed on its serial, usually
   `192.168.4.1`, port `8080`):

   ```bash
   websocat ws://192.168.4.1:8080
   # or: wscat -c ws://192.168.4.1:8080
   ```

3. On connect you should immediately receive a **hello** frame:

   ```json
   {"type":"hello","ip":"192.168.4.1","forwarded":0}
   ```

4. With node3 wired in and feeding it records (§5b, §6), detection frames follow,
   one JSON object per WebSocket message — exactly node3's lines, forwarded verbatim:

   ```json
   {"node":1,"schema":1,"mode":0,"uptime":9750,"bssid":"2E:7B:C8:EA:A6:EA","ssid":"…","rssi":-85,"ch":6,"enc":3,"bearing":-1.0,"range":-1.00,"in_cone":1,"h_lock":-1.0}
   ```

Getting the `hello` but no detections means the R4 is healthy but nothing valid is
arriving on its node3 serial link — check §6.

> **Quick bench test without node3:** with the R4's RX1 (pin 0) wired to a USB-serial
> adapter at 115200, paste a `{...}` line + newline and it appears on your WebSocket
> client; paste a non-`{` line and it shows up as `[NODE3] …` on the R4's USB serial only.

---

## 6. Test the parts together (no frontend)

Full chain: `node1 + node2` → (ESP-NOW) → `node3` → (serial JSON) → `R4` → (WebSocket) → client

1. Flash node3 (§5b) and note its `[ANCHOR] BSSID`.
2. Flash node1 and node2 (§5a) with `BASE_NODE_MAC` set to node3's BSSID. Confirm node3's
   `rx=` climbs and JSON lines appear on node3's serial.
3. Wire node3 → R4 over serial (see wiring below) and flash the R4 (§5c).
4. Connect `websocat` to the R4 (§5c). You should see the `hello` frame, then live
   detection records — each line identical to what node3 prints — as the scanners pick up
   networks.

You can validate the upstream half (scan → record → JSON) **without the R4 at all** by
just watching node3's serial monitor; and the downstream half (serial → WebSocket) on the
bench with a USB-serial adapter (see the quick bench test in §5c). When both halves check
out, wiring them together is the only remaining step.

### Wiring (node3 → R4)

node3's serial TX wires into the R4's **RX1 (pin 0)**. The reverse line (R4 TX1, pin 1 →
node3 RX) is optional — the R4 only reads from node3 — but if you connect it, it **must**
go through a level shifter: the R4's GPIO is **5 V** and node3's RX is **not 5 V
tolerant**. A 10 kΩ/20 kΩ divider gives `5 V × 20/(10+20) = 3.33 V`. node3 TX → R4 RX1 is
fine direct (3.3 V reads as a valid high on the R4). Share a common ground between the
boards. Both sides run at **115200 baud**.

### Recommended power-on order

1. Power node3 first (so the ESP-NOW receiver and anchor beacon are up).
2. Power the scanner nodes.
3. Power the R4 last, then connect your WebSocket client once it prints its IP.

---

## 7. Troubleshooting

| Symptom | Likely cause |
|---|---|
| Scanner `fail` count climbing | Wrong `BASE_NODE_MAC`, or node3 not on the ESP-NOW channel |
| node3 `rx` stays 0 | Scanners pointed at the wrong base MAC, or not powered/scanning |
| R4 `hello` received but no detections | Nothing valid on node3's serial link — check the wiring, that node3 is emitting JSON, and the baud (115200) |
| R4 shows `[NODE3]` lines but no WebSocket records | node3 is wired correctly but only printing diagnostics — no scanner records reaching it (check node3 `rx`) |
| WebSocket connects then drops | `loop()` blocked too long; keep the R4 loop non-blocking |
| Sensor build can't find `../../shared/...` | Run `pio` from the correct project dir; the shared headers are referenced relatively |

More R4-specific detail lives in
[`R4/mainLogicControlller/SPEC_ARDUINO_R4_BASE_NODE.md`](R4/mainLogicControlller/SPEC_ARDUINO_R4_BASE_NODE.md).
