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

// Parse a streamed host-discovery line: a bare "<IPv4>" (tolerating a leading
// "> " prompt), as arpscan/pingscan print each active host. Rejects lines that
// carry an IP after other text (e.g. pingscan's "IP address:/Gateway:/MAC:"
// header). Writes the IP into ip_out (>= 16 bytes).
bool mm_hostscan_parse_ip(const char* line, char* ip_out);

// Parse a `portscan` open-port line "<IPv4>: <port>" (e.g. "192.168.0.101: 22").
// Sets *port. Returns false for progress lines ("Checking IP: ... Port: N") and
// the IP/Gateway/MAC header. Tolerates a leading "> " prompt.
bool mm_portscan_parse_open(const char* line, int* port);

// Well-known service name for a port, or "" if unknown (for display next to the
// port number). Not exhaustive -- the common ones plus a few NAS/IoT services.
const char* mm_port_service_name(int port);

// Parse a `sniffbeacon` line: "<rssi> Ch: <ch> <bssid> ESSID: <name>". Like the
// scanall AP line but with NO trailing metadata tokens -- the ESSID runs to end
// of line. Fills *out (hidden if the ESSID is empty). Tolerates a "> " prompt.
bool mm_beacon_parse_line(const char* line, MMScanAp* out);

// Parse a `sniffprobe` line: "<rssi> Ch: <ch> Client: <mac> Requesting: <ssid>".
// Stores the client MAC in out->bssid and the requested SSID in out->ssid
// (empty request -> hidden=true, ssid=""). Tolerates a "> " prompt.
bool mm_probe_parse_line(const char* line, MMScanAp* out);

// --- `join` output support --------------------------------------------------

// True if a join-output line is settings-dump or password-bearing and must NOT
// be shown: the "Name:/Type:/Value:" settings block, its "Settings"/"----"
// separators, or any line carrying the password ("Password:" / "Value:"). The
// Join screen shows only lines this rejects-as-noise returns false for.
bool mm_join_line_is_noise(const char* line);

// Extract an assigned IPv4 (dotted quad, not 0.0.0.0) from a line into ip_out
// (>= 16 bytes). Returns true only when a real address is found.
bool mm_join_parse_ip(const char* line, char* ip_out);

// True if a line is Marauder's "Bluetooth not supported" reply (WiFi-only board).
bool mm_bt_line_unsupported(const char* line);

// Capability tri-state parsed from an `info` reply (launch-time detection).
typedef enum {
    MMCapUnknown = 0, // line said nothing conclusive
    MMCapYes, // present / supported / connected
    MMCapNo, // absent / not supported / not connected
} MMCap;

// Presence: true if a line marks a live Marauder (the `info` reply, which opens
// with "Firmware: Marauder"). Any matching line proves a board is answering.
bool mm_info_line_is_marauder(const char* line);

// SD card, from info's "SD Card: Connected" / "SD Card: Not Connected".
MMCap mm_info_line_sd(const char* line);

// Bluetooth, from info's "Bluetooth: Supported" / "Bluetooth: Not Supported"
// (custom firmware). Also treats the stock `sniffbt` "Bluetooth not supported"
// reply as MMCapNo, so detection still works before the firmware is reflashed.
MMCap mm_info_line_bt(const char* line);

// GPS, from info's "GPS: Connected" (module present) vs "GPS: Not Connected" /
// "GPS: Not Supported" (both -> MMCapNo: no usable GPS).
MMCap mm_info_line_gps(const char* line);

// One deauth/disassoc frame from a `sniffdeauth` line. The firmware prints
// (WiFiScan.cpp, WIFI_SCAN_DEAUTH): "<rssi> Ch: <channel> <src> -> <dst>", with
// an optional leading "> " prompt and an optional trailing space (screen boards).
typedef struct {
    char src[18]; // sender MAC "xx:xx:xx:xx:xx:xx"
    char dst[18]; // target MAC (often the AP's clients or ff:ff:ff:ff:ff:ff)
    int channel;
    int rssi; // signed dBm
} MMDeauthFrame;

// Parse one sniffdeauth line into *out. Returns false if the line isn't a
// deauth frame report (no " Ch: " / " -> " / valid MACs).
bool mm_deauth_parse_line(const char* line, MMDeauthFrame* out);

// True if mac is the broadcast address ff:ff:ff:ff:ff:ff (case-insensitive).
bool mm_mac_is_broadcast(const char* mac);

// One PineScan (rogue-AP / WiFi Pineapple) detection. Firmware line
// (WiFiScan.cpp): "MAC: <mac> CH: <ch> RSSI: <rssi> DET: <type> SSID: <essid>".
// DET is one of SUSP_OUI / TAG+SUSP_CAP / OTHER. Distinguished from the very
// similar multissid line by the " DET: " field.
typedef struct {
    char mac[18];
    char ssid[33]; // essid or "[hidden]"
    char det[16]; // detection type
    int channel;
    int rssi;
} MMPineScan;

// Parse one pinescan detection line. Returns false if it isn't one.
bool mm_pinescan_parse_line(const char* line, MMPineScan* out);

// Pwnagotchi beacons print (at least) "Name: <name>" then "Pwnd #: <n>".
// Custom firmware (see firmware/) also emits "MAC: <mac>" (and Ver/Uptime/
// Deauth) for a real identity. The parser handles both: the MAC line is simply
// absent on stock firmware. These extract each line.
bool mm_pwn_line_name(const char* line, char* out, size_t out_sz);
bool mm_pwn_line_pwnd(const char* line, int* out);
bool mm_pwn_line_mac(const char* line, char* out); // out >= 18; validates a MAC

// Direct upload (wardrive -> WiGLE), from info's "Direct Upload: Supported" /
// "Not Supported". Gates the Upload Wardrive action.
MMCap mm_info_line_direct_upload(const char* line);

// Dual-band (5 GHz) radio, from info's "Dual Band: Supported" / "Not Supported".
// Stored for the incoming C5; not yet gating any UI.
MMCap mm_info_line_dual_band(const char* line);

typedef enum {
    MMJoinLineNone, // nothing conclusive
    MMJoinLineConnecting, // "Connecting to WiFi"
    MMJoinLineConnected, // success marker
    MMJoinLineFailed, // failure/disconnect/error marker
} MMJoinLineType;

// Classify a (non-noise) join-output line as progress/success/failure.
MMJoinLineType mm_join_classify(const char* line);
