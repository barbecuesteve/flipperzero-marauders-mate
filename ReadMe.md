# Marauder's Mate

![Marauder's Mate — hack smarter, not harder](splash.png)

**A friendlier Flipper Zero front-end for the ESP32 Marauder firmware.**

The [ESP32 Marauder](https://github.com/justcallmekoko/ESP32Marauder) is a
powerful WiFi/Bluetooth pentest firmware, driven over a serial CLI. The existing
Flipper companion app is essentially a terminal for that CLI: it builds the
commands for you and dumps the raw ESP32 text into a scrolling console.

Marauder's Mate **parses that output into a real UI**. A scan becomes a
selectable list of access points; tap one and you get a detail screen that can
join it, scan its hosts, or attack it — no reading indices off a console and
typing them back. It's built to make the Marauder pleasant to *use*, not just to
*operate*.

> This is a GPLv3 fork of
> [0xchocolate/flipperzero-wifi-marauder](https://github.com/0xchocolate/flipperzero-wifi-marauder).

## Highlights

- **Tap-to-target, not type-an-index.** Pick an AP or a client from a live list
  and act on it directly; the app issues the underlying `select -a`/`-c` for you.
- **Live WiFi scan → AP detail.** Streaming, deduped access-point list; each AP
  opens to stations, Join, ARP host scan, and attacks — with a per-host Port Scan.
- **Attack/defence monitors.** Beacon, Probe, and Deauth monitors parse the raw
  firehose into live lists that dedup and rank by activity ("who's deauthing whom").
- **Fox Hunt RSSI meter.** A live signal bar with peak-hold to physically home in
  on a chosen AP or client.
- **Capability-aware menus.** The app detects your board at launch and greys out
  what it can't do — no Bluetooth radio, no SD card, no GPS.
- **Friendlier system controls.** An LED colour picker, a Settings screen with
  on/off toggles and save/load-to-Flipper profiles, and a reboot confirmation.
- **One-tap auto-join** for your own networks (optional; see below).

## Menu map & how it's organized

The full menu/scene tree is documented in **[docs/MENU.md](docs/MENU.md)**.

Upstream's app is a single flat menu that mirrors the Marauder CLI: every command
in one long scroll, each one dumping raw serial text to a console. Marauder's
Mate reorganizes that around how you actually work:

- **By protocol, not by command.** The top level is Device Info / WiFi /
  Bluetooth / GPS / System, so you stay in one radio's world instead of scrolling
  past everything at once.
- **Capability-aware.** At launch the app probes the board (`info`) and greys out
  what it can't do, so you never pick a dead option. (Reporting BT/GPS/etc. in
  `info` needs a small firmware addition — see below — but even without it the app
  detects the board and SD card and degrades gracefully.)
- **Parsed screens, not raw text.** A scan becomes a selectable AP list; tap an
  AP for a detail screen (stations, join, ARP scan, attacks). Device Info, Port
  Scan, the monitors, and the Fox Hunt meter are all parsed views.
- **Actions live where their target is chosen.** AP attacks on the AP; client
  attacks on the station; untargeted/broadcast in an Air Attacks group.
- **Grouped by intent.** WiFi sniffing splits into **Detect** (defensive — deauth
  flows, rogue APs, Pineapple / Pwnagotchi detection) and **Capture** (offensive
  — PMKID / handshake / raw). Spoofing, air attacks, and SD tools each get a
  sub-menu.
- **Safer defaults.** The saved-log viewer was removed — Marauder echoes the WiFi
  password on join, so a saved log can hold plaintext credentials — and
  screen-only commands that emit nothing over serial were dropped rather than
  shown as dead ends.

## Hardware

Requires a Flipper Zero and a dev board running ESP32 Marauder firmware.
Developed and verified against the **ESP32-S2 Flipper Zero WiFi Dev Board**,
which is WiFi-only (no Bluetooth, 2.4 GHz). Bluetooth, AirTag, and 5 GHz features
are stubbed for the incoming **ESP32-C5** (dual-band + BLE) and are greyed out on
boards that lack the hardware.

For flashing the Marauder firmware, see UberGuidoZ's
[install guide](https://github.com/UberGuidoZ/Flipper/tree/main/Wifi_DevBoard#marauder-install-information).

## Build & install

Uses [`ufbt`](https://pypi.org/project/ufbt/):

```sh
python3 -m venv .venv && . .venv/bin/activate
pip install ufbt
ufbt            # build -> dist/marauders_mate.fap
ufbt launch     # build, upload, and run on a connected Flipper
```

## Auto-join your networks (optional)

Tapping **Join** on a network normally prompts you to type the password on the
Flipper. To one-tap join your own networks instead, drop a file on the SD card at
`/ext/apps_data/marauder/networks.txt` listing SSID/password pairs; when a
tapped AP's SSID matches, the app joins without prompting. See
[`networks.txt.example`](networks.txt.example) for the format.

> ⚠️ **This file is plaintext on an unencrypted, removable SD card.** Anyone who
> gets the card can read every password in it. Put only your own networks here,
> and treat a lost Flipper as lost passwords. The real file is gitignored; only
> the placeholder example is tracked. (Marauder also echoes the password in its
> own join output, the same as a manually typed join.)

## Optional: fuller capability detection (firmware patch)

Stock Marauder firmware reports the board and SD card in `info` but not
Bluetooth, GPS, direct-upload, or dual-band. A small, additive firmware patch in
[`firmware/`](firmware/) adds those lines so a single probe can gate the whole
UI; a companion patch lets the LED colour picker drive the Flipper dev board's
status LED. Both are optional — the app works on stock firmware and simply leaves
the corresponding items ungreyed. See [`firmware/README.md`](firmware/README.md).

## Status

Functional and actively developed. The core flows are verified on the S2 dev
board: Live Scan → AP detail → stations / join / ARP scan / attacks, the Beacon /
Probe / Deauth monitors, Device Info, the parsed Port Scan, and the Fox Hunt
meter. It is a work in progress and moving toward a first release; feedback from
other boards is welcome.

**Next up:** ESP32-C5 support (5 GHz, BLE, parsed AirTag/BLE lists), resolving
ARP-scan host IPs to MACs, and de-duplicating the targeted vs. untargeted PMKID
paths.

## Credits & license

GPLv3, inherited from upstream — see [LICENSE](LICENSE). Flipper companion app
originally by [0xchocolate](https://github.com/0xchocolate/flipperzero-wifi-marauder)
and contributors; ESP32 Marauder firmware by
[JustCallMeKoko](https://github.com/justcallmekoko/ESP32Marauder).

> Use only on networks and devices you own or are authorized to test.
