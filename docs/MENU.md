# Marauder's Mate — function map

The app's full menu/scene tree. Parsed custom scenes are marked **(Mate)**;
everything else sends a raw ESP32 Marauder command to the console.

Menu structure: a top-level **category submenu** (`scenes/wifi_marauder_scene_categories.c`)
→ a per-category renderer (`scenes/wifi_marauder_scene_start.c`, filtered by each
item's `MMMenuCategory`). Items live in the `items[]` table in `scene_start.c`.

```
Marauder's Mate  [top-level: category submenu]
│
├─ Device Info ......................... info → parsed fields  (Mate)
│     • Connected: <ssid> / Not connected   (from join state)
│     • Disconnect ....... stopscan -f  (only when connected)
│     • Firmware, Version, Hardware, ESP-IDF, Station MAC, AP MAC, SD…
│     • Re-detect ........ re-run the capability probe (updates greying)
│     • "No Marauder detected" + Re-detect when nothing answers
│
├─ WiFi
│   ├─ Live Scan (Mate) ............... clearlist+scanall, live deduped AP list
│   │     └─ [select AP] → AP Detail (Mate)
│   │           • * Connected (joined) + Disconnect (stopscan -f)  when on this AP
│   │           • Join ............... in lieu of Disconnect when not on this AP
│   │           │     └─ auto-join networks.txt OR keyboard
│   │           │           └─ "Save for later?" → Join result (Mate)
│   │           • CH / RSSI / BSSID  (info rows)
│   │           ├─ Stations (N) → station list (Mate)
│   │           │     └─ [select client] → Station Detail  (targeted, -c)
│   │           │           ├─ Deauth ............ attack -t deauth -c
│   │           │           ├─ Bad Msg ........... attack -t badmsg -c
│   │           │           ├─ Sleep ............. attack -t sleep -c
│   │           │           └─ Fox Hunt .......... foxhunt -s (RSSI meter, Mate)
│   │           ├─ Fox Hunt ........... foxhunt -w (RSSI meter, Mate)
│   │           ├─ ARP Scan .......... arpscan, live host IPs (Mate)
│   │           │     └─ [select host] → Port Scan (Mate)
│   │           │           • open ports + service names, live progress
│   │           └─ Attacks ▸ → AP attack sub-menu (Mate)  [all target this AP]
│   │                 ├─ Deauth ...... attack -t deauth
│   │                 ├─ CSA ......... attack -t csa      (impersonate this AP)
│   │                 ├─ Quiet ....... attack -t quiet    (impersonate this AP)
│   │                 ├─ Sniff ....... sniffraw
│   │                 └─ PMKID ....... sniffpmkid
│   ├─ Beacon Mon (Mate) ............. sniffbeacon → APs by beacon count
│   ├─ Probe Mon (Mate) .............. sniffprobe → probe requests
│   ├─ Set STA MAC ......... randstamac / clonestamac -s
│   ├─ AP Spoofing ▸  → sub-menu (see below)
│   ├─ Air Attacks ▸  → sub-menu (see below)
│   ├─ Detect ▸  → sub-menu (see below)
│   ├─ Capture ▸  → sub-menu (see below)
│   └─ Stop ................ stopscan   (ends the scan/attack; keeps connection)
│
│   ├─ AP Spoofing  [WiFi → sub-menu]
│   │     ├─ Spoof SSIDs ...... ssid -a -g / -a -n / -r   [keyboard]  (the broadcast list)
│   │     ├─ View SSIDs ....... list -s
│   │     ├─ Clear SSIDs ...... clearlist -s
│   │     ├─ Set AP MAC ....... randapmac / cloneapmac -a
│   │     ├─ Evil Portal ...... evilportal -c start / sethtml / setap
│   │     ├─ Load Evil Portal HTML file … evilportal -c sethtmlstr
│   │     └─ Beacon Spam ...... attack -t beacon -a / -l / -r
│   │
│   ├─ Air Attacks  [WiFi → sub-menu; untargeted / broadcast]
│   │     ├─ Probe Spam ....... attack -t probe
│   │     ├─ Rickroll Beacons . attack -t rickroll
│   │     ├─ Funny SSIDs ...... attack -t funny
│   │     ├─ SAE Flood ........ attack -t sae
│   │     ├─ Karma ............ karma -p   [keyboard; SD-gated: serves an Evil Portal page]
│   │     └─ Manual Deauth .... attack -t deauth -s   [keyboard: src/dst MAC]
│   │
│   ├─ Detect  [WiFi → sub-menu; defensive: spot attacks / attack gear nearby]
│   │     ├─ Deauth Frames (Mate) . sniffdeauth → live flows by src→dst (who's deauthing whom)
│   │     ├─ Rogue APs ........ sniffmultissid  (karma/mana multi-SSID beaconing)
│   │     ├─ Pineapple (Mate) . sniffpinescan → list by MAC (SSID / DET / RSSI)
│   │     └─ Pwnagotchi (Mate) sniffpwn → list by name (Pwnd count; no MAC over serial)
│   │
│   └─ Capture  [WiFi → sub-menu; offensive: handshake / frame capture]
│         ├─ PMKID ........... sniffpmkid  (→ channel/deauth options)
│         ├─ SAE (WPA3) ...... sniffsae
│         └─ Raw ............. sniffraw
│
├─ Bluetooth   [greyed "(no HW)" on ESP32-S2; probed via sniffbt]
│   ├─ AirTags ............. list -t          ← (C5: → parsed AirTag list, mm-2ki)
│   ├─ BT Devices .......... list -b          ← (C5: → parsed BLE list, mm-4yo)
│   ├─ BT Spam ............. sourapple/applejuice/swiftpair/samsung/google/flipper/all
│   ├─ Airtag .............. spoofat -t / findmy -t   [keyboard]
│   └─ BT Sniff ............ bt/skim/airtag/flipper/flock/meta
│
├─ GPS
│   ├─ Wardrive ............ wardrive
│   ├─ Upload Wardrive ..... upload -d wdg/wigle/both
│   ├─ GPS Data ............ gps -t / gpsdata / gps -g <field×9>
│   ├─ NMEA Stream ......... nmea
│   └─ GPS POI ............. gpspoi -s/-m/-e
│
└─ System
    ├─ LED (Mate) ......... colour picker → led -s #RRGGBB / led -p rainbow
    ├─ Settings (Mate) .... live toggle screen (see below)
    ├─ SD Card ▸  → sub-menu (see below)
    ├─ Reboot (Mate) ...... Cancel / Reboot confirm → reboot
    └─ Help ............... help
    └─ SD Card  [System → sub-menu; greyed when no card]
          ├─ List SD ...... ls /   [keyboard]
          └─ Update ....... update -s

Settings (Mate): reads the ESP `settings` on entry and shows the six booleans
(ForcePMKID, ForceProbe, SavePCAP, EnableLED, EPDeauth, ChanHop) as live On/Off
toggles (flip → `settings -s <name> enable|disable`), plus the two Flipper-side
capture prefs (save pcaps / logs to the Flipper SD). Actions: Save profile→Flipper
(writes the six booleans to MM_SETTINGS_PROFILE_FILEPATH), Load profile←Flipper
(re-applies them to the ESP), Restore defaults (`settings -r`). The old
"Save to flipper sdcard" (settings-init) is retired; its capture prefs live here.

LED colours are 8-solid on the Flipper dev board's 3-GPIO RGB (needs a firmware
change to drive it — mm-82m); works natively on neopixel boards / the C5.
```

## Parsed (Mate) scenes

Device Info, Live Scan, AP Detail, AP Attacks, Station List, Station Detail,
Fox Hunt, Join result, ARP Scan, Port Scan, Beacon Mon, Probe Mon, Deauth Mon,
Pineapple Mon, Pwnagotchi Mon, LED picker, Settings, Reboot confirm — plus the
category submenu and the shared per-category / AP-Spoofing renderer (now also
used by the SD Card sub-menu).

The L3 chain hangs entirely off **Live Scan → AP**: Join → ARP Scan → Port Scan.
Five sub-menus (WiFi's AP Spoofing, Air Attacks, Detect, Capture; System's SD
Card) share one renderer (`wifi_marauder_render_category` on `app->sub_category`),
each opened by a sentinel row (`spoofmenu` / `airmenu` / `detectmenu` /
`capturemenu` / `sdmenu`). LED / Settings / Reboot open their own scenes via
`ledpicker` / `settingsui` / `rebootconfirm` sentinels.

Sniffing is split by intent: **Detect** (defensive — deauth frames, rogue APs,
Pineapple, Pwnagotchi) vs **Capture** (offensive handshake/frame grab — PMKID,
SAE, raw). `mactrack` and `packetcount` were dropped: both render only on the
ESP's TFT and emit nothing over serial, so they show a dead console on the
screenless module (verified in WiFiScan.cpp — no `Serial.print` in either path).

The five live monitors (Beacon, Probe, Deauth, Pineapple, Pwnagotchi) share one
engine (`wifi_marauder_monitor.{h,c}`): each scene is a thin wrapper supplying an
`MMMonDef` (command, headers, a per-line parser, a row formatter). The engine
owns the RX drain, dedup+count store, sort, and throttled rebuild. Pwnagotchi
shows a MAC when the firmware patch is applied (see `firmware/`); otherwise
name + pwnd count.

## Attack placement (by target level)

Each attack lives where its target is chosen (none are L3/host — those are Port
Scan). Confirmed against the ESP32 Marauder source:

- **Selected AP** (needs a chosen AP → **AP Detail → Attacks**): `deauth`
  (requires `select`, `filterActive()`), `csa`, `quiet` (impersonate that AP's
  BSSID), plus `sniffraw` / `sniffpmkid` capture on that AP.
- **Selected station** (needs a chosen client → **Station Detail**, all `-c`):
  `deauth -c`, `badmsg -c`, `sleep -c`.
- **Untargeted / broadcast** (launch-and-go → **Air Attacks**): `probe`,
  `rickroll`, `funny`, `sae`, `karma`, and manual (`deauth -s <mac>`).

## Retired items

- **Scripts** (the inherited Marauder automation-scripts subsystem: cJSON, the
  script core/executor/worker, and 8 script scenes) removed. It was the single
  largest part of the binary (~30 KB of code) and pushed the loaded app to ~99 KB,
  past what the Flipper heap could hand out contiguously — the app failed to load
  / relaunch (out of memory). Removing it dropped the loaded size to ~69 KB.
- **Select** (`select -a/-s/-c`) removed. Selection is now implicit: AP Detail
  and Station Detail call `select` before each attack (`mm_select_target`).
  `select -s` (SSID) was vestigial — the beacon-list attack broadcasts the whole
  SSID list regardless of the `.selected` flag (verified in WiFiScan.cpp). The
  `-f` filter power move ("select every AP matching X, then broadcast-deauth all
  of them") is the one lost capability — tracked as a future targeted action in
  `mm-yiy`, not a raw index picker.
- **Channel** (`channel` / `channel -s`) removed. `set_channel` only matters for
  channel-locked modes, and the one Mate flow that needs it — PMKID — sets it
  inline (`sniffpmkid -c` in the PMKID options). Live Scan / scanall sweep all
  channels and ignore it. Dropped rather than kept as a raw knob.
- **System → Info** (`info`, raw) removed — the top-level **Device Info** (Mate)
  is the parsed superset (same fields plus a Connected line). `mm-1d7`
- **View Log from** (the on-device `.log` viewer) removed — a saved log can
  contain the plaintext WiFi password (Marauder echoes it on join), and the
  viewer's file browser could open any `.log`, so it surfaced credentials on the
  device. The whole scene is gone. Log *saving* is untouched; whether to stop
  logging join output is a separate follow-up. `mm-8cu`

## Stop vs. Disconnect

`stopscan` and `stopscan -f` are different (confirmed in CommandLine.cpp):

- **Stop** (`stopscan`, WiFi menu) — ends the current scan/attack, **keeps** any
  joined network.
- **Disconnect** (`stopscan -f`) — also runs `WiFi.disconnect(true)`, dropping the
  joined AP and powering down station mode. Surfaced next to the Connected
  indicator on **AP Detail** and **Device Info** (only shown when connected); the
  app clears its connection state when firing it.

## Known redundancies (tracked)

- **WiFi Sniff → pmkid** (untargeted) vs AP-detail **PMKID** (targeted) — dedup. `mm-hda`

## Capability detection (`mm-xdy`)

On launch the category menu probes the board and greys what isn't usable
(assume-absent, prove-present). The probe runs from `scene_categories.c`:

1. **Settle** — send `stopscan` and let the reply drain (~0.4s). This idles the
   ESP: scan/attack commands are gated behind `!scanning()`, and it also flushes
   any dangling boot-noise line so the next command parses cleanly.
2. **`info`** — collect ~1.5s, parse via `mm_apply_info_caps`:
   - presence (`Firmware: Marauder`) → grey everything but Device Info if absent
   - `SD Card: Connected/Not Connected` → grey SD items when absent
   - `Bluetooth:`, `GPS:`, `Direct Upload:`, `Dual Band:` — **custom firmware
     only** (added to `RunInfo`); stock firmware omits them.
3. **BT fallback** — when `info` carries no `Bluetooth:` line (stock firmware),
   send `stopscan` (clears the `SHOW_INFO` scan mode `info` leaves set) then
   `sniffbt -serial`. `-serial` is required for the reply to reach the UART;
   "Bluetooth not supported" → no BT. Accumulation is capped (a BT scan on a
   capable board streams forever) so it can't exhaust the heap.

**Greyed** rows carry a suffix and are inert: categories `(no device)` / `(no
HW)` / `(none)`; SD-gated items `(no SD)`; Upload Wardrive `(no upload)`.

SD-gated items: List SD, Save to flipper sdcard, Update, Load Evil
Portal HTML, Wardrive, Upload Wardrive. Upload Wardrive is *also* gated on
Direct Upload. Dual-band is parsed and stored for the C5 but gates nothing yet.

Detection is session-scoped (not persisted); **Device Info → Re-detect** re-runs
the probe after a hotplug (board or SD card).

## Hardware notes

- ESP32-S2 (current board): WiFi-only, no BT radio, 2.4 GHz only. The Bluetooth
  section auto-greys ("no HW") from the launch probe.
- ESP32-C5 (incoming): dual-band + BLE. Planned parsed BLE/AirTag screens
  (`mm-4yo`/`mm-2ki`) will replace the raw AirTags/BT Devices items; 5 GHz
  (`mm-cfp`). The `Dual Band:` info line already feeds `dual_band_state`.
