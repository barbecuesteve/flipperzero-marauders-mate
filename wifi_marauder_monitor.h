// Marauder's Mate: shared live-monitor engine.
//
// The Beacon / Probe / Deauth / Pineapple / Pwnagotchi screens are all the same
// shape: run a sniff command (usually channel-hopping), parse each serial line
// into a keyed record, dedup + count, sort, and rebuild a submenu on a throttle.
// This engine owns all of that; each monitor scene just supplies an MMMonDef
// (command, headers, a per-line parser, and a row formatter) and forwards its
// three scene entry points here.
#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct WifiMarauderApp WifiMarauderApp;

// One deduped row. `parse` fills the fields it needs; the engine keeps `hits`.
typedef struct {
    char key[40]; // dedup key (BSSID, "src>dst", name, ...)
    char t1[34]; // primary text (SSID / name / MAC tail)
    char t2[34]; // secondary (detection / dst / requested SSID / MAC)
    int channel;
    int rssi;
    int metric; // sort value when sort_by_metric (RSSI, pwnd count, ...)
    uint32_t hits; // times seen
} MMMonRec;

typedef struct {
    const char* cmd; // sniff command, e.g. "sniffbeacon"
    const char* waiting; // header shown until first record
    const char* header_fmt; // header with one %d (record count)
    bool chanhop; // enable ChanHop while running
    bool sort_by_metric; // true: sort by metric desc; false: by hits desc
    // Parse one line into *out; return true if *out is a complete record.
    bool (*parse)(const char* line, MMMonRec* out);
    // Format a row from a record.
    void (*label)(const MMMonRec* r, char* buf, size_t sz);
} MMMonDef;

void mm_monitor_start(WifiMarauderApp* app, const MMMonDef* def);
void mm_monitor_tick(WifiMarauderApp* app, const MMMonDef* def);
void mm_monitor_stop(WifiMarauderApp* app, const MMMonDef* def);
