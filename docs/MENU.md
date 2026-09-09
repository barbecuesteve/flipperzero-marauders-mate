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
│
├─ WiFi
│   ├─ Live Scan (Mate) ............... clearlist+scanall, live deduped AP list
│   │     └─ [select AP] → AP Detail (Mate)
│   │           • CH / RSSI / BSSID  (info rows)
│   │           • * Connected (joined)   (if this is the joined AP)
│   │           ├─ Disconnect ......... stopscan -f  (only when connected here)
│   │           ├─ Stations (N) → station list (Mate)
│   │           │     └─ [select client] → Station Detail  (targeted, -c)
│   │           │           ├─ Deauth ............ attack -t deauth -c
│   │           │           ├─ Bad Msg ........... attack -t badmsg -c
│   │           │           ├─ Sleep ............. attack -t sleep -c
│   │           │           └─ Fox Hunt .......... foxhunt -s (RSSI meter, Mate)
│   │           ├─ Fox Hunt ........... foxhunt -w (RSSI meter, Mate)
│   │           ├─ Join (L3) ......... auto-join networks.txt OR keyboard
│   │           │     └─ "Save for later?" → Join result (Connecting/Connected/Failed) (Mate)
│   │           ├─ Host Scan (L3) .... arpscan, live host IPs (Mate)
│   │           │     └─ [select host] → Port Scan (Mate)
│   │           │           • open ports + service names, live progress
│   │           ├─ Deauth ............ attack -t deauth   (this AP)
│   │           ├─ CSA ............... attack -t csa      (impersonate this AP)
│   │           ├─ Quiet ............. attack -t quiet    (impersonate this AP)
│   │           ├─ Sniff ............. sniffraw
│   │           └─ PMKID ............. sniffpmkid
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
│   │     ├─ Karma ............ karma -p   [keyboard]
│   │     └─ Manual Deauth .... attack -t deauth -s   [keyboard: src/dst MAC]
│   │
│   ├─ Detect  [WiFi → sub-menu; defensive: spot attacks / attack gear nearby]
│   │     ├─ Deauth Frames .... sniffdeauth     (someone deauthing — parsed screen: mm-0jh)
│   │     ├─ Rogue APs ........ sniffmultissid  (karma/mana multi-SSID beaconing)
│   │     ├─ Pineapple ........ sniffpinescan   (WiFi Pineapple / evil-AP fingerprints)
│   │     └─ Pwnagotchi ....... sniffpwn        (nearby Pwnagotchi units)
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
    ├─ LED ................. led -s / led -p   [keyboard]
    ├─ Settings ............ settings / -r / -s <toggle×6>
    ├─ List SD ............. ls /   [keyboard]
    ├─ Update .............. update -s
    ├─ Reboot .............. reboot
    ├─ Help ................ help
    ├─ Scripts ............. → script select/edit
    └─ Save to flipper sdcard → settings-init
```

## Parsed (Mate) scenes

Device Info, Live Scan, AP Detail, Station List, Station Detail, Fox Hunt,
Join result, Host Scan, Port Scan, Beacon Mon, Probe Mon — plus the category
submenu and the shared per-category / AP-Spoofing renderer.

The L3 chain hangs entirely off **Live Scan → AP**: Join → Host Scan → Port Scan.
The four WiFi sub-menus (AP Spoofing, Air Attacks, Detect, Capture) share one
renderer (`wifi_marauder_render_category` on `app->sub_category`), each opened by
a sentinel row (`spoofmenu` / `airmenu` / `detectmenu` / `capturemenu`).

Sniffing is split by intent: **Detect** (defensive — deauth frames, rogue APs,
Pineapple, Pwnagotchi) vs **Capture** (offensive handshake/frame grab — PMKID,
SAE, raw). `mactrack` and `packetcount` were dropped: both render only on the
ESP's TFT and emit nothing over serial, so they show a dead console on the
screenless module (verified in WiFiScan.cpp — no `Serial.print` in either path).

## Attack placement (by target level)

Each attack lives where its target is chosen (none are L3/host — those are Port
Scan). Confirmed against the ESP32 Marauder source:

- **Selected AP** (needs a chosen AP → **AP Detail**): `deauth` (requires
  `select`, `filterActive()`), `csa`, `quiet` (impersonate that AP's BSSID).
- **Selected station** (needs a chosen client → **Station Detail**, all `-c`):
  `deauth -c`, `badmsg -c`, `sleep -c`.
- **Untargeted / broadcast** (launch-and-go → **Air Attacks**): `probe`,
  `rickroll`, `funny`, `sae`, `karma`, and manual (`deauth -s <mac>`).

## Retired items

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

## Hardware notes

- ESP32-S2 (current board): WiFi-only, no BT radio, 2.4 GHz only. The Bluetooth
  section auto-greys ("no HW") after a one-shot `sniffbt` probe.
- ESP32-C5 (incoming): dual-band + BLE. Planned parsed BLE/AirTag screens
  (`mm-4yo`/`mm-2ki`) will replace the raw AirTags/BT Devices items; 5 GHz
  (`mm-cfp`).
