// Marauder's Mate: pure parser for ESP32 Marauder `list -a` output.
//
// Dependency-free (no furi/GUI) so it compiles both into the Flipper app and
// into a host unit test. One `list -a` row looks like:
//
//     [<index>][CH:<ch>] <name> <rssi>
//
// where <name> is the SSID (may contain spaces) or, for a hidden/unnamed AP,
// a BSSID like aa:bb:cc:dd:ee:ff. <rssi> is the final whitespace-delimited
// token. <index> is the value used by Marauder's `select -a <index>`.
#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define MM_AP_NAME_MAX 33 // SSID max 32 chars + NUL; BSSID (17) fits too

typedef struct {
    int index; // bracket index -> select -a <index>
    int channel; // from [CH:<n>]
    int rssi; // signed dBm
    bool hidden; // true when name field is a BSSID (no SSID advertised)
    char name[MM_AP_NAME_MAX]; // SSID, or BSSID string when hidden
} MMAccessPoint;

// True if s is exactly a MAC/BSSID "xx:xx:xx:xx:xx:xx".
bool mm_ap_is_bssid(const char* s);

// Replace NUL bytes with spaces in place. Marauder pads hidden-AP ESSID fields
// with 0x00; callers must run this over received chunks before treating them as
// C strings, or strlen-based appends silently truncate at the first NUL.
void mm_sanitize_nuls(uint8_t* buf, size_t len);

// Parse a single line. Returns true and fills *out iff line is a valid AP row.
bool mm_ap_parse_line(const char* line, MMAccessPoint* out);

// Parse a whole text buffer (newline-separated) into out[0..max). Returns count.
size_t mm_ap_parse_buffer(const char* text, MMAccessPoint* out, size_t max);

// ---------------------------------------------------------------------------
// scanall support
//
// `scanall` streams two kinds of lines, interleaved:
//   AP:      "<rssi> Ch: <ch> <bssid> ESSID: <ssid> <m1> <m2>"
//   station: "<n>: ap: <bssid> -> sta: <mac>"  (direction may be reversed)
// Unlike `list -a`, every AP row carries the BSSID, which is the stable key
// for de-duplicating re-sightings across sweeps.
// ---------------------------------------------------------------------------

#define MM_BSSID_LEN 18 // "xx:xx:xx:xx:xx:xx" + NUL

typedef struct {
    char bssid[MM_BSSID_LEN]; // dedup key
    char ssid[MM_AP_NAME_MAX]; // "" when hidden
    int channel;
    int rssi;
    bool hidden;
} MMScanAp;

typedef enum {
    MMScanLineNone, // noise / prompt / status
    MMScanLineAp, // an AP beacon row
    MMScanLineStation, // a station/association row
} MMScanLineType;

// Classify a scanall line without fully parsing it.
MMScanLineType mm_scanall_classify(const char* line);

// Parse a scanall AP beacon line (handles a leading "> " prompt). Fills *out.
bool mm_scanall_parse_ap(const char* line, MMScanAp* out);

// Parse a scanall station line. Writes the AP's BSSID and the station MAC
// (each at least MM_BSSID_LEN). Returns true on a valid association line.
bool mm_scanall_parse_station(const char* line, char* ap_bssid_out, char* sta_mac_out);

// Upsert an AP into a BSSID-keyed store. On a hit, updates rssi/channel and
// (if the new sighting is named) the ssid/hidden fields. Returns the store
// index, or -1 if full; sets *is_new (may be NULL) to whether a slot was added.
int mm_scan_store_upsert(
    MMScanAp* store,
    int* count,
    int max,
    const MMScanAp* ap,
    bool* is_new);

// Resolve a `select -a <index>` index for a scanall target, bridging to a
// parsed `list -a` snapshot. scanall and list -a share the same internal AP
// vector, so `discovery_index` (the target's first-appearance rank in the
// scanall stream) should equal its list -a index; this is verified against
// list_a contents (hidden rows expose the BSSID, named rows the SSID). Falls
// back to a unique content match. Returns the index, or -1 if it cannot be
// resolved unambiguously (e.g. a named duplicate SSID whose order didn't
// verify -- list -a has no BSSID to disambiguate it).
int mm_resolve_select_index(
    const MMScanAp* target,
    int discovery_index,
    const MMAccessPoint* list_a,
    int list_a_count);

// ---------------------------------------------------------------------------
// `list -c` support (stations grouped under APs)
//
//   [<ap_index>] <name> <rssi>:      AP header (index matches list -a)
//     [<sel_index>] <mac>            station: sel_index is the select -c index
// ---------------------------------------------------------------------------

// Parse a `list -c` AP header line (no indent, trailing ':'). Sets *ap_index.
bool mm_listc_parse_ap_header(const char* line, int* ap_index);

// Parse an indented `list -c` station line. Sets *sel_index and mac_out
// (>= MM_BSSID_LEN). Returns true on a valid station row.
bool mm_listc_parse_station(const char* line, int* sel_index, char* mac_out);

// Parse a Fox Hunt stream line ("<name> RSSI: <n>"), tolerating a leading
// "> " prompt. Sets *rssi. Returns true only on a line carrying an RSSI value.
bool mm_foxhunt_parse_rssi(const char* line, int* rssi);

// Parse a `list -i` host line ("[<n>] <IPv4>"). Writes the dotted-quad IP into
// ip_out (>= 16 bytes). Returns true only on a valid host row.
bool mm_listi_parse_ip(const char* line, char* ip_out);

// Parse a `sniffbeacon` line: "<rssi> Ch: <ch> <bssid> ESSID: <name>". Like the
// scanall AP line but with NO trailing metadata tokens -- the ESSID runs to end
// of line. Fills *out (hidden if the ESSID is empty). Tolerates a "> " prompt.
bool mm_beacon_parse_line(const char* line, MMScanAp* out);
