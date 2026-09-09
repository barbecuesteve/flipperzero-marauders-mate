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
│     • Firmware, Version, Hardware, ESP-IDF, Station MAC, AP MAC, SD…
│
├─ WiFi
│   ├─ Live Scan (Mate) ............... clearlist+scanall, live deduped AP list
│   │     └─ [select AP] → AP Detail (Mate)
│   │           • CH / RSSI / BSSID  (info rows)
│   │           • * Connected (joined)   (if this is the joined AP)
│   │           ├─ Stations (N) → station list (Mate)
│   │           │     └─ [select client] → Station Detail
│   │           │           ├─ Deauth ............ select AP+sta, attack -t deauth -c
│   │           │           └─ Fox Hunt .......... foxhunt -s (RSSI meter, Mate)
│   │           ├─ Fox Hunt ........... foxhunt -w (RSSI meter, Mate)
│   │           ├─ Join (L3) ......... auto-join networks.txt OR keyboard
│   │           │     └─ "Save for later?" → Join result (Connecting/Connected/Failed) (Mate)
│   │           ├─ Host Scan (L3) .... arpscan, live host IPs (Mate)
│   │           │     └─ [select host] → Port Scan (Mate)
│   │           │           • open ports + service names, live progress
│   │           ├─ Deauth ............ attack -t deauth
│   │           ├─ Sniff ............. sniffraw
│   │           └─ PMKID ............. sniffpmkid
│   ├─ Beacon Mon (Mate) ............. sniffbeacon → APs by beacon count
│   ├─ Probe Mon (Mate) .............. sniffprobe → probe requests
│   ├─ Select .............. select -a/-s/-c   [keyboard]
│   ├─ Set STA MAC ......... randstamac / clonestamac -s
│   ├─ AP Spoofing ▸  → sub-menu (see below)
│   ├─ Join WiFi ........... join -s  (saved creds)   [keyboard]
│   ├─ Clear List .......... clearlist -a/-s/-c
│   ├─ WiFi Attack ......... deauth/probe/rickroll/funny/badmsg/sleep/sae/csa/quiet
│   ├─ Targeted Attacks .... attack -t deauth -s / karma -p / badmsg -c / sleep -c  [keyboard]
│   ├─ WiFi Sniff .......... deauth/pmkid/pwn/raw/mactrack/packetcount/pineapple/multissid/sae
│   ├─ Channel ............. channel (get) / channel -s (set)
│   └─ Shutdown WiFi ....... stopscan -f
│
│   └─ AP Spoofing  [WiFi → sub-menu]
│         ├─ Spoof SSIDs ...... ssid -a -g / -a -n / -r   [keyboard]  (the broadcast list)
│         ├─ View SSIDs ....... list -s
│         ├─ Set AP MAC ....... randapmac / cloneapmac -a
│         ├─ Evil Portal ...... evilportal -c start / sethtml / setap
│         ├─ Load Evil Portal HTML file … evilportal -c sethtmlstr
│         └─ Beacon Spam ...... attack -t beacon -a / -l / -r
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
    ├─ Info ................ info   (raw; superseded by top-level Device Info)
    ├─ View Log from ....... start/end → log viewer / console
    ├─ Scripts ............. → script select/edit
    └─ Save to flipper sdcard → settings-init
```

## Parsed (Mate) scenes

Device Info, Live Scan, AP Detail, Station List, Station Detail, Fox Hunt,
Join result, Host Scan, Port Scan, Beacon Mon, Probe Mon — plus the category
submenu and the shared per-category / AP-Spoofing renderer.

The L3 chain hangs entirely off **Live Scan → AP**: Join → Host Scan → Port Scan.

## Known redundancies (tracked)

- **System → Info** duplicates **Device Info** — remove the raw item. `mm-1d7`
- **View Log from** can display saved logs that contain the plaintext WiFi
  password (Marauder echoes it on join) — security risk; remove the viewer. `mm-8cu`
- **Join WiFi (saved)** (`join -s`, quick reconnect) overlaps AP-detail **Join (L3)**
  — consolidate/relabel. `mm-pw2`
- **WiFi Sniff → pmkid** (untargeted) vs AP-detail **PMKID** (targeted) — dedup. `mm-hda`

## Hardware notes

- ESP32-S2 (current board): WiFi-only, no BT radio, 2.4 GHz only. The Bluetooth
  section auto-greys ("no HW") after a one-shot `sniffbt` probe.
- ESP32-C5 (incoming): dual-band + BLE. Planned parsed BLE/AirTag screens
  (`mm-4yo`/`mm-2ki`) will replace the raw AirTags/BT Devices items; 5 GHz
  (`mm-cfp`).
