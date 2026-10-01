# Multi-radio awareness — design + plan

Make Marauder's Mate aware of **more than one ESP32 radio** on the same board,
instead of assuming a single Marauder on `FuriHalSerialIdUsart`. The near-term
goal is **detection**: prove the app can see the bench dual-radio unit's *second*
ESP and report it. Everything past detection (routing scans to a chosen radio,
merging results) is staged behind that.

Status: **M0 resolved 2026-09-28 — original premise disproved; pivoted to direct
GPS.** See the outcome box below. Confidence tags on every hardware claim.

> ## M0 OUTCOME (2026-09-28): no second Marauder on the header
>
> Tested functionally via the Flipper USB-UART bridge + an `info` probe on each
> header UART:
> - **13/14 (USART1) = C5 Marauder** — `ESP32-C5 DevKit`, v1.17.0, `Dual Band:
>   Supported`. [confirmed on device]
> - **15/16 (LPUART1) = GPS receiver** — continuous NMEA at 115200
>   (`$GNGGA`/`$GPGSV`, valid checksums), ignores CLI input. [confirmed on device]
>
> **Conclusion:** the header's two UARTs go to the C5 and the GPS. There is **no
> second Marauder command endpoint** — the v6 UI chip is not broken out (matches
> the inter-chip link in `FIRMWARE.md`). The multi-radio-**command** design below
> (old M1–M5) does not apply to this board and those beads are **closed as
> premise-invalidated**.
>
> **Pivot (discovery-first):** the Flipper can read **GPS NMEA directly** off
> LPUART 15/16 on a second UART, independent of the C5 link. New chain **G0–G3**:
> G0 `mm-zdr` (open LPUART, read NMEA — *ready*), G1 `mm-br5` (route NMEA into the
> existing `nmea`/`gpsdata` parsers), G2 `mm-dm1` (GPS-source toggle), G3 `mm-j77`
> (keep the C5 link free during GPS use), G4 `mm-d2i` (satellite icon + sat-count
> indicator on the GPS views). The second-UART machinery designed below is reused
> verbatim; only the payload is NMEA, not a Marauder CLI.
>
> **UI indicator (G4) — feasibility:** a 16×16 satellite icon + live count is
> doable on the **widget-based GPS views** (`gps_sats`/`gps_data`) via
> `widget_add_icon_element` + `assets/Satellite_16x16.png` (→ `&I_Satellite_16x16`).
> A *global* upper-right badge across all scenes is **not** natural: the stock
> `submenu`/`variable_item_list` modules own the whole 128×64 canvas and expose no
> overlay corner, so a persistent badge would mean custom-drawing every scene.
> Scope to GPS views. Icon should signal the *enhanced* (direct-LPUART) source is
> live — filled when active, tied to G2's source state. Top-right at ~x=108
> displaces the existing HDOP/speed text.
>
> Everything below this box is the **original** design, kept for context.

## Framing: discovery first, not attack

Steer for this feature (user intent, 2026-09-28): lean the second radio toward
**discovery / enumeration**, not more attack surface. The dual-band C5 makes the
compelling story a *more complete picture of the RF environment* — 5 GHz APs the
v6 can't see, richer station maps, GPS-tagged wardrive/mapping — rather than a
second attack transmitter. So:

- First user-visible payoff is a **Radios** view: "what chips are here, what can
  each do," a natural extension of today's Device Info.
- When routing lands, default discovery scans (`scanall`, `list`, GPS) to the
  most capable radio; do **not** auto-fan attacks across both.
- Naming in code/UI: "radio" / "discovery," not "attacker" / "second gun."

## The hardware question this rests on (verify before building)

Today `wifi_marauder_uart.c` hardcodes `#define UART_CH (FuriHalSerialIdUsart)`
— the Flipper's **USART1** on header **pins 13 (TX) / 14 (RX)**. [high]

The Flipper Zero exposes a **second** hardware serial, **LPUART1**, on header
**pins 15 (TX) / 16 (RX)**, addressable as `FuriHalSerialIdLpuart`. [high] So the
app *can* open a second independent UART with no extra hardware.

What is **NOT** established — and the whole feature depends on it:

- **Is the second ESP electrically reachable from the Flipper header at all?**
  `docs/FIRMWARE.md` says the bridge (13/14) is wired to the **C5**, and that the
  v6 UI chip is on an *inter-chip* link, "not on our UART path." `docs/BRIDGE.md`
  says the opposite is possible (each ESP has its own Flipper-facing GPIOs, v6 on
  13/14, C5 remapped to 15/16). **These two docs conflict.** [the conflict is the
  point]
- **Resolving test (BRIDGE.md P5):** meter continuity from each ESP's UART pair
  to the Flipper header pads. Three outcomes:
  1. Both ESPs land on the **same** two header pads (13/14) → only one talks at a
     time; a second `FuriHalSerialId` buys nothing. Feature is a select-line /
     mux problem, not a second-UART problem.
  2. The two ESPs land on **different** pads (one on 13/14, one on 15/16) → the
     clean case; open both UARTs, detection works as designed below.
  3. Second ESP not broken out to the header at all → software can't reach it;
     feature is dead until rewired.

**Milestone 0 is this measurement.** Writing the two-UART code before it is
writing for a link that may not exist. (Cheap parallel probe if a meter is slow:
flash the app with LPUART opened, send `info\n` down it, watch for any reply.)

## Data model change: one radio → a small fixed array

The single `WifiMarauderUart* uart` (`wifi_marauder_app_i.h`) becomes an array of
radio slots. Each slot owns its serial id, UART handle, and detected caps — the
caps that are today loose fields on the app (`bt_state`, `sd_state`, `gps_state`,
`dual_band_state`, ...) move *into* the slot, because they are per-radio.

```c
#define MM_RADIO_MAX (2)   // USART1 + LPUART1; bump only if a 3rd link is proven

typedef struct {
    FuriHalSerialId serial_id;   // FuriHalSerialIdUsart / ...Lpuart
    WifiMarauderUart* uart;      // NULL until opened; NULL if nothing answers
    MMDeviceState present;       // probed per radio
    MMCap sd, gps, direct_upload, dual_band;
    MMBtState bt;
    char hw[24];                 // "ESP32-C5 DevKit" etc. from info's Hardware:
    char version[16];            // firmware version from info
} MMRadio;
```

App gains `MMRadio radios[MM_RADIO_MAX]; int radio_count; int active_radio;`.
`active_radio` indexes the radio that command TX / RX callbacks currently target
— so the vast majority of existing scenes need **no change**: they keep calling
`wifi_marauder_uart_tx(app->uart, ...)`, where `app->uart` becomes a macro/inline
resolving to `app->radios[app->active_radio].uart`.

That indirection is the seam that keeps this from touching every scene.

## Detection (the milestone that matters)

1. **Open** each candidate serial id (USART first, LPUART second), each with its
   own RX thread. `wifi_marauder_uart_init` already takes a `FuriHalSerialId` and
   a thread name — it was written for exactly this; only the `usart_init` wrapper
   and the callers hardcode the channel. Give each RX thread a distinct name.
2. **Probe** each open radio the way the launch probe already works
   (`scenes/wifi_marauder_scene_categories.c`): `stopscan` to settle, then
   `info\n`, collect ~1.5 s, run `mm_apply_info_caps` **into that radio's slot**
   instead of onto the app. `mm_apply_info_caps` is refactored to take an
   `MMRadio*` (or a caps-struct) rather than writing app fields directly.
3. **Classify**: a slot with a Marauder `info` reply is `present`; its
   `Hardware:` and `Dual Band:` lines tell 5 GHz-capable (C5) from 2.4-only (v6).
4. **Report**: a **Radios** entry (extend Device Info) lists each present radio,
   its hardware string, version, and caps. On the bench unit the win condition is
   two rows: the C5 (dual-band) and the v6 (2.4 GHz) — or an honest "1 radio
   found on USART; nothing on LPUART" if P5 outcome 1/3.

Backward compat: if only one radio answers (any stock single-Marauder setup),
behavior is identical to today — `radio_count == 1`, `active_radio == 0`.

## Concurrency / memory

- **Per-radio RX thread + streams** (the existing model, instantiated twice). The
  IRQ→stream→worker path is already per-`WifiMarauderUart`; two instances don't
  share state. Distinct thread names for debugging. [high]
- **Cost:** ~7 KB heap per extra radio — two `RX_BUF_SIZE` (2048) stream buffers
  + a 1 KB RX thread stack + the struct. The `.fap` RAM budget (~66 KB, per
  CLAUDE.md) has room for one more; **measure** actual load after, since a
  too-large `.fap` fails to load. [moderate — needs measurement]
- **Probe them sequentially, not in parallel**, at launch: two ESPs answering
  `info` at once is fine electrically (separate UARTs) but the shared parse
  scratch (`ap_scan_buffer`, `scan_stream`) is single-use. Probe radio 0, then
  radio 1, into their own slots. [high]
- **Teardown order unchanged**, applied per radio: stop-IRQ → join-thread →
  free-streams (a stray callback with no MMU reboots the Flipper).

## Risks

- **Expansion service claims LPUART.** `wifi_marauder_app()` already calls
  `expansion_disable()` before opening the USART; confirm that also frees LPUART,
  or the acquire on radio 1 fails. [low, but check — it's a one-liner to verify]
- **`furi_hal_serial_control_acquire(FuriHalSerialIdLpuart)` returns NULL** if
  the platform/API build doesn't expose it. `wifi_marauder_uart_init` does
  `furi_check(serial_handle)` — a hard reboot. Detection code must **tolerate a
  failed acquire** (NULL slot, skip), not `furi_check`, since a second radio is
  optional. Refactor the check out of the shared init for optional radios. [high]
- **P5 outcome 1 (shared pads):** the entire two-UART approach is wrong and this
  becomes a mux/select-line design. Do not build past Milestone 1 until P5 says
  outcome 2. [high — this is the gate]
- **LPUART baud ceiling / level:** both ESPs are 3.3 V, same board, no shifter
  (BRIDGE.md). LPUART at 115200 is fine. [high]

## Milestones → beads

**Superseded — see the M0 OUTCOME box at the top.** M0 `mm-jbw` is closed
(resolved). M1–M5 (`mm-k9d`/`mm-mfr`/`mm-yeb`/`mm-dmz`/`mm-gpp`) are closed as
premise-invalidated. Active work is the G0–G3 direct-GPS chain in that box.

- **M0 — Verify topology (P5 continuity test).** Meter each ESP UART pair to the
  Flipper header pads. Decide outcome 1/2/3. *Blocks everything.* No code.
- **M1 — Radio slot data model.** Introduce `MMRadio`, `radios[]`, `active_radio`;
  make `app->uart` resolve through `active_radio`; move per-radio caps into the
  slot. Refactor `mm_apply_info_caps` to fill an `MMRadio*`. No behavior change
  yet (single radio). Host tests still green. *Depends: M0 = outcome 2.*
- **M2 — Optional second UART open.** Make `wifi_marauder_uart_init` tolerate a
  failed/absent acquire (no `furi_check` reboot) for optional radios; open
  LPUART as radio 1; confirm `expansion_disable` frees it. *Depends: M1.*
- **M3 — Per-radio detection probe.** Probe each open radio with `stopscan`+`info`
  into its slot; classify present / hardware / dual-band. *Depends: M2.*
- **M4 — Radios view.** Extend Device Info into a per-radio list: hardware,
  version, caps, or "nothing on LPUART." **This is the demo:** the bench unit
  shows both its ESPs. *Depends: M3.*
- **M5 (later, out of first scope) — Radio routing.** Let discovery scenes pick a
  radio (default: most capable / dual-band); merge AP/station results by BSSID
  across radios. Discovery-only by default; attacks stay single-radio and
  explicit. *Depends: M4.*

Start with `bd show mm-zdr` (direct-GPS G0).
