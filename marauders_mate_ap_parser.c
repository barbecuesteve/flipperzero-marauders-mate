#include "marauders_mate_ap_parser.h"

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

bool mm_ap_is_bssid(const char* s) {
    if(!s || strlen(s) != 17) return false;
    for(int i = 0; i < 17; i++) {
        if((i % 3) == 2) {
            if(s[i] != ':') return false;
        } else if(!isxdigit((unsigned char)s[i])) {
            return false;
        }
    }
    return true;
}

bool mm_ap_parse_line(const char* line, MMAccessPoint* out) {
    if(!line || !out) return false;

    const char* p = line;
    while(*p == ' ' || *p == '\t') p++;
    if(*p != '[') return false;
    p++;

    // index
    char* end;
    long idx = strtol(p, &end, 10);
    if(end == p || *end != ']') return false;
    p = end + 1;

    // [CH:<n>]
    if(strncmp(p, "[CH:", 4) != 0) return false;
    p += 4;
    long ch = strtol(p, &end, 10);
    if(end == p || *end != ']') return false;
    p = end + 1;

    // remainder: "<name> <rssi>"
    while(*p == ' ' || *p == '\t') p++;
    const char* rest = p;
    size_t len = strlen(rest);
    while(len > 0 && (rest[len - 1] == '\n' || rest[len - 1] == '\r' ||
                      rest[len - 1] == ' ' || rest[len - 1] == '\t'))
        len--;
    if(len == 0) return false;

    // rssi is the final whitespace-delimited token
    size_t last_sp = (size_t)-1;
    for(size_t i = 0; i < len; i++) {
        if(rest[i] == ' ' || rest[i] == '\t') last_sp = i;
    }
    if(last_sp == (size_t)-1) return false; // need both a name and an rssi

    const char* rssi_tok = rest + last_sp + 1;
    size_t rssi_len = len - (last_sp + 1);
    char rbuf[16];
    if(rssi_len == 0 || rssi_len >= sizeof(rbuf)) return false;
    memcpy(rbuf, rssi_tok, rssi_len);
    rbuf[rssi_len] = '\0';
    char* rend;
    long rssi = strtol(rbuf, &rend, 10);
    if(rend == rbuf || *rend != '\0') return false;

    // name is everything before that last whitespace, trimmed
    size_t name_len = last_sp;
    while(name_len > 0 && (rest[name_len - 1] == ' ' || rest[name_len - 1] == '\t'))
        name_len--;
    if(name_len == 0) return false;
    if(name_len >= MM_AP_NAME_MAX) name_len = MM_AP_NAME_MAX - 1;
    memcpy(out->name, rest, name_len);
    out->name[name_len] = '\0';

    out->index = (int)idx;
    out->channel = (int)ch;
    out->rssi = (int)rssi;
    out->hidden = mm_ap_is_bssid(out->name);
    return true;
}

size_t mm_ap_parse_buffer(const char* text, MMAccessPoint* out, size_t max) {
    if(!text || !out) return 0;
    size_t count = 0;
    const char* p = text;
    char line[128];
    while(*p && count < max) {
        const char* nl = strchr(p, '\n');
        size_t linelen = nl ? (size_t)(nl - p) : strlen(p);
        size_t cpy = linelen < sizeof(line) - 1 ? linelen : sizeof(line) - 1;
        memcpy(line, p, cpy);
        line[cpy] = '\0';
        if(mm_ap_parse_line(line, &out[count])) count++;
        if(!nl) break;
        p = nl + 1;
    }
    return count;
}

// --------------------------------------------------------------------------
// scanall support
// --------------------------------------------------------------------------

// Skip leading whitespace and a single "> " prompt.
static const char* mm_skip_prompt(const char* p) {
    while(*p == ' ' || *p == '\t') p++;
    if(p[0] == '>' && p[1] == ' ') {
        p += 2;
        while(*p == ' ' || *p == '\t') p++;
    }
    return p;
}

MMScanLineType mm_scanall_classify(const char* line) {
    if(!line) return MMScanLineNone;
    if(strstr(line, "ESSID:")) return MMScanLineAp;
    if(strstr(line, "->")) return MMScanLineStation;
    return MMScanLineNone;
}

// Shared AP-beacon line parser for scanall and sniffbeacon. Handles both the
// firmware format families we've seen on device:
//   older/other fw:  "<rssi> Ch: <ch> <bssid> ESSID: <ssid> <m1> <m2>"
//   v1.10.2 C5:      "<rssi> Ch: <ch> BSSID: <bssid> [ESSID Len: <n> ]ESSID: <ssid>"
// i.e. the BSSID may be bare or carry a "BSSID: " label; sniffbeacon inserts an
// "ESSID Len: <n>" field; and the ESSID either runs to end of line (labeled
// form, no trailing metadata -- that moved to a separate "Beacon:" line) or
// carries two trailing metadata tokens (old bare scanall form).
//
// `strip_meta_if_bare`: when true AND the line used a bare BSSID (old scanall),
// drop the two trailing metadata tokens after the ESSID. sniffbeacon passes
// false (its ESSID always runs to end of line).
static bool mm_parse_ap_beacon(const char* line, MMScanAp* out, bool strip_meta_if_bare) {
    if(!line || !out) return false;
    const char* p = mm_skip_prompt(line);

    char* end;
    long rssi = strtol(p, &end, 10);
    if(end == p) return false;
    p = end;
    while(*p == ' ') p++;

    if(strncmp(p, "Ch:", 3) != 0) return false;
    p += 3;
    while(*p == ' ') p++;
    long ch = strtol(p, &end, 10);
    if(end == p) return false;
    p = end;
    while(*p == ' ') p++;

    // Optional "BSSID: " label (v1.10.2 C5); absent on the older bare form.
    bool labeled = false;
    if(strncmp(p, "BSSID:", 6) == 0) {
        labeled = true;
        p += 6;
        while(*p == ' ') p++;
    }

    // BSSID token (17 chars).
    const char* bstart = p;
    while(*p && *p != ' ' && *p != '\t') p++;
    if((size_t)(p - bstart) != 17) return false;
    char bssid[MM_BSSID_LEN];
    memcpy(bssid, bstart, 17);
    bssid[17] = '\0';
    if(!mm_ap_is_bssid(bssid)) return false;

    // Find the ESSID field. strstr skips any "ESSID Len: <n> " that precedes it
    // ("ESSID Len:" has a space before "Len", so it is not an "ESSID:" match).
    const char* e = strstr(p, "ESSID:");
    if(!e) return false;
    const char* rem = e + 6; // past "ESSID:"

    size_t rlen = strlen(rem);
    while(rlen > 0 && (rem[rlen - 1] == '\n' || rem[rlen - 1] == '\r' ||
                       rem[rlen - 1] == ' ' || rem[rlen - 1] == '\t'))
        rlen--;

    // Old bare scanall form carries two trailing metadata tokens; drop them.
    size_t essid_end = rlen;
    if(strip_meta_if_bare && !labeled) {
        size_t i = rlen;
        for(int tok = 0; tok < 2; tok++) {
            while(i > 0 && rem[i - 1] != ' ' && rem[i - 1] != '\t') i--; // token
            while(i > 0 && (rem[i - 1] == ' ' || rem[i - 1] == '\t')) i--; // gap
        }
        essid_end = i;
    }

    size_t s = 0;
    while(s < essid_end && (rem[s] == ' ' || rem[s] == '\t')) s++;
    while(essid_end > s && (rem[essid_end - 1] == ' ' || rem[essid_end - 1] == '\t')) essid_end--;
    size_t essid_len = essid_end - s;

    char ssid[MM_AP_NAME_MAX];
    if(essid_len >= MM_AP_NAME_MAX) essid_len = MM_AP_NAME_MAX - 1;
    memcpy(ssid, rem + s, essid_len);
    ssid[essid_len] = '\0';

    strcpy(out->bssid, bssid);
    out->channel = (int)ch;
    out->rssi = (int)rssi;
    if(essid_len == 0 || strcmp(ssid, bssid) == 0) {
        out->hidden = true;
        out->ssid[0] = '\0';
    } else {
        out->hidden = false;
        strcpy(out->ssid, ssid);
    }
    return true;
}

bool mm_scanall_parse_ap(const char* line, MMScanAp* out) {
    return mm_parse_ap_beacon(line, out, /*strip_meta_if_bare=*/true);
}

// Parse "<label>: <mac>" where label is "ap" or "sta". Advances nothing;
// operates on [s, e). Sets *is_ap and copies the MAC.
static bool mm_parse_side(const char* s, const char* e, bool* is_ap, char* mac_out) {
    while(s < e && (*s == ' ' || *s == '\t')) s++;
    if(e - s >= 3 && strncmp(s, "ap:", 3) == 0) {
        *is_ap = true;
        s += 3;
    } else if(e - s >= 4 && strncmp(s, "sta:", 4) == 0) {
        *is_ap = false;
        s += 4;
    } else {
        return false;
    }
    while(s < e && (*s == ' ' || *s == '\t')) s++;
    while(e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) e--;
    if((e - s) != 17) return false;
    char mac[MM_BSSID_LEN];
    memcpy(mac, s, 17);
    mac[17] = '\0';
    if(!mm_ap_is_bssid(mac)) return false;
    strcpy(mac_out, mac);
    return true;
}

bool mm_scanall_parse_station(const char* line, char* ap_bssid_out, char* sta_mac_out) {
    if(!line || !ap_bssid_out || !sta_mac_out) return false;
    const char* p = mm_skip_prompt(line);

    char* end;
    strtol(p, &end, 10); // leading counter, value unused
    if(end == p || *end != ':') return false;
    p = end + 1;

    const char* lend = p + strlen(p);
    const char* arrow = strstr(p, "->");
    if(!arrow) return false;

    bool is_ap1, is_ap2;
    char m1[MM_BSSID_LEN], m2[MM_BSSID_LEN];
    if(!mm_parse_side(p, arrow, &is_ap1, m1)) return false;
    if(!mm_parse_side(arrow + 2, lend, &is_ap2, m2)) return false;
    if(is_ap1 == is_ap2) return false; // exactly one ap and one sta

    if(is_ap1) {
        strcpy(ap_bssid_out, m1);
        strcpy(sta_mac_out, m2);
    } else {
        strcpy(ap_bssid_out, m2);
        strcpy(sta_mac_out, m1);
    }
    return true;
}

int mm_scan_store_upsert(
    MMScanAp* store,
    int* count,
    int max,
    const MMScanAp* ap,
    bool* is_new) {
    for(int i = 0; i < *count; i++) {
        if(strcmp(store[i].bssid, ap->bssid) == 0) {
            store[i].rssi = ap->rssi;
            store[i].channel = ap->channel;
            if(!ap->hidden) { // a named sighting fills in a previously-hidden SSID
                store[i].hidden = false;
                strcpy(store[i].ssid, ap->ssid);
            }
            if(is_new) *is_new = false;
            return i;
        }
    }
    if(*count >= max) {
        if(is_new) *is_new = false;
        return -1;
    }
    store[*count] = *ap;
    int idx = *count;
    (*count)++;
    if(is_new) *is_new = true;
    return idx;
}

int mm_resolve_select_index(
    const MMScanAp* target,
    int discovery_index,
    const MMAccessPoint* list_a,
    int list_a_count) {
    if(!target || !list_a) return -1;

    // 1) Trust discovery order if the row at that index verifies.
    if(discovery_index >= 0 && discovery_index < list_a_count) {
        const MMAccessPoint* row = &list_a[discovery_index];
        if(target->hidden) {
            if(row->hidden && strcmp(row->name, target->bssid) == 0) return discovery_index;
        } else {
            if(!row->hidden && strcmp(row->name, target->ssid) == 0) return discovery_index;
        }
    }

    // 2) Fallback: accept only a unique content match.
    int found = -1;
    for(int i = 0; i < list_a_count; i++) {
        const MMAccessPoint* row = &list_a[i];
        bool match = target->hidden ? (row->hidden && strcmp(row->name, target->bssid) == 0) :
                                      (!row->hidden && strcmp(row->name, target->ssid) == 0);
        if(match) {
            if(found == -1)
                found = i;
            else
                return -1; // ambiguous
        }
    }
    return found;
}

// --------------------------------------------------------------------------
// `list -c` support
// --------------------------------------------------------------------------

bool mm_listc_parse_ap_header(const char* line, int* ap_index) {
    if(!line || !ap_index) return false;
    if(line[0] != '[') return false; // AP headers are not indented
    char* end;
    long idx = strtol(line + 1, &end, 10);
    if(end == line + 1 || *end != ']') return false;
    // Must end with ':' after trailing whitespace.
    size_t n = strlen(line);
    while(n > 0 && (line[n - 1] == '\r' || line[n - 1] == '\n' || line[n - 1] == ' ')) n--;
    if(n == 0 || line[n - 1] != ':') return false;
    *ap_index = (int)idx;
    return true;
}

bool mm_listc_parse_station(const char* line, int* sel_index, char* mac_out) {
    if(!line || !sel_index || !mac_out) return false;
    const char* p = line;
    if(*p != ' ' && *p != '\t') return false; // stations are indented
    while(*p == ' ' || *p == '\t') p++;
    if(*p != '[') return false;
    char* end;
    long idx = strtol(p + 1, &end, 10);
    if(end == p + 1 || *end != ']') return false;
    p = end + 1;
    while(*p == ' ') p++;
    size_t n = strlen(p);
    while(n > 0 && (p[n - 1] == '\r' || p[n - 1] == '\n' || p[n - 1] == ' ')) n--;
    if(n != 17) return false;
    char mac[MM_BSSID_LEN];
    memcpy(mac, p, 17);
    mac[17] = '\0';
    if(!mm_ap_is_bssid(mac)) return false;
    *sel_index = (int)idx;
    strcpy(mac_out, mac);
    return true;
}

// --------------------------------------------------------------------------
// Fox Hunt support
// --------------------------------------------------------------------------

bool mm_foxhunt_parse_rssi(const char* line, int* rssi) {
    if(!line || !rssi) return false;
    const char* p = strstr(line, "RSSI:");
    if(!p) return false;
    p += 5; // past "RSSI:"
    while(*p == ' ' || *p == '\t') p++;
    char* end;
    long v = strtol(p, &end, 10);
    if(end == p) return false;
    *rssi = (int)v;
    return true;
}

void mm_sanitize_nuls(uint8_t* buf, size_t len) {
    if(!buf) return;
    for(size_t i = 0; i < len; i++) {
        if(buf[i] == 0) buf[i] = ' ';
    }
}

bool mm_hostscan_parse_ip(const char* line, char* ip_out) {
    if(!line || !ip_out) return false;
    const char* p = line;
    while(*p == ' ' || *p == '\t' || *p == '>') p++; // strip prompt + indent
    size_t n = strlen(p);
    while(n > 0 && (p[n - 1] == '\r' || p[n - 1] == '\n' || p[n - 1] == ' ')) n--;
    if(n == 0 || n >= 16) return false;
    // The whole remaining token must be an IPv4. This rejects the header lines
    // ("IP address: ...", "Gateway: ...", "MAC: ...") which have text first.
    int dots = 0;
    for(size_t i = 0; i < n; i++) {
        char c = p[i];
        if(c == '.')
            dots++;
        else if(c < '0' || c > '9')
            return false;
    }
    if(dots != 3) return false;
    memcpy(ip_out, p, n);
    ip_out[n] = '\0';
    return true;
}

bool mm_portscan_parse_open(const char* line, int* port) {
    if(!line || !port) return false;
    const char* p = line;
    while(*p == ' ' || *p == '\t' || *p == '>') p++;
    int a, b, c, d, pt, n = 0;
    if(sscanf(p, "%d.%d.%d.%d: %d%n", &a, &b, &c, &d, &pt, &n) != 5) return false;
    // The whole token must be consumed (rejects trailing junk); octets/port sane.
    if(p[n] != '\0' && p[n] != '\r' && p[n] != '\n' && p[n] != ' ') return false;
    if(a < 0 || a > 255 || b < 0 || b > 255 || c < 0 || c > 255 || d < 0 || d > 255) return false;
    if(pt < 1 || pt > 65535) return false;
    *port = pt;
    return true;
}

const char* mm_port_service_name(int port) {
    switch(port) {
    case 21:
        return "FTP";
    case 22:
        return "SSH";
    case 23:
        return "Telnet";
    case 25:
        return "SMTP";
    case 53:
        return "DNS";
    case 80:
        return "HTTP";
    case 110:
        return "POP3";
    case 111:
        return "RPC";
    case 139:
        return "NetBIOS";
    case 143:
        return "IMAP";
    case 443:
        return "HTTPS";
    case 445:
        return "SMB";
    case 548:
        return "AFP";
    case 631:
        return "IPP";
    case 1883:
        return "MQTT";
    case 2049:
        return "NFS";
    case 3306:
        return "MySQL";
    case 3389:
        return "RDP";
    case 5000:
        return "DSM/UPnP";
    case 5001:
        return "DSM-TLS";
    case 5432:
        return "Postgres";
    case 5900:
        return "VNC";
    case 6379:
        return "Redis";
    case 8080:
        return "HTTP-alt";
    case 8443:
        return "HTTPS-alt";
    case 32400:
        return "Plex";
    default:
        return "";
    }
}

bool mm_beacon_parse_line(const char* line, MMScanAp* out) {
    // sniffbeacon's ESSID always runs to end of line (its metadata is the
    // "ESSID Len:" field, which the shared parser skips), so never strip.
    return mm_parse_ap_beacon(line, out, /*strip_meta_if_bare=*/false);
}

bool mm_probe_parse_line(const char* line, MMScanAp* out) {
    if(!line || !out) return false;
    const char* p = line;
    while(*p == ' ' || *p == '\t') p++;
    if(p[0] == '>' && p[1] == ' ') {
        p += 2;
        while(*p == ' ') p++;
    }

    char* end;
    long rssi = strtol(p, &end, 10);
    if(end == p) return false;
    p = end;
    while(*p == ' ') p++;

    if(strncmp(p, "Ch:", 3) != 0) return false;
    p += 3;
    while(*p == ' ') p++;
    long ch = strtol(p, &end, 10);
    if(end == p) return false;
    p = end;
    while(*p == ' ') p++;

    if(strncmp(p, "Client:", 7) != 0) return false;
    p += 7;
    while(*p == ' ') p++;
    const char* mstart = p;
    while(*p && *p != ' ' && *p != '\t') p++;
    if((size_t)(p - mstart) != 17) return false;
    char mac[MM_BSSID_LEN];
    memcpy(mac, mstart, 17);
    mac[17] = '\0';
    if(!mm_ap_is_bssid(mac)) return false;
    while(*p == ' ') p++;

    if(strncmp(p, "Requesting:", 11) != 0) return false;
    p += 11;
    while(*p == ' ') p++;

    const char* rem = p;
    size_t n = strlen(rem);
    while(n > 0 && (rem[n - 1] == '\r' || rem[n - 1] == '\n' || rem[n - 1] == ' ' ||
                    rem[n - 1] == '\t'))
        n--;
    if(n >= MM_AP_NAME_MAX) n = MM_AP_NAME_MAX - 1;

    strcpy(out->bssid, mac);
    out->channel = (int)ch;
    out->rssi = (int)rssi;
    if(n == 0) {
        out->hidden = true; // broadcast/wildcard probe (no SSID)
        out->ssid[0] = '\0';
    } else {
        out->hidden = false;
        memcpy(out->ssid, rem, n);
        out->ssid[n] = '\0';
    }
    return true;
}

// --------------------------------------------------------------------------
// `join` output support
// --------------------------------------------------------------------------

// Case-insensitive substring search (portable; avoids GNU strcasestr).
static const char* mm_ci_strstr(const char* hay, const char* needle) {
    if(!hay || !needle) return NULL;
    if(!*needle) return hay;
    for(; *hay; hay++) {
        const char* h = hay;
        const char* n = needle;
        while(*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
            h++;
            n++;
        }
        if(!*n) return hay;
    }
    return NULL;
}

bool mm_join_line_is_noise(const char* line) {
    if(!line) return true;
    const char* p = line;
    while(*p == ' ' || *p == '\t' || *p == '>') p++;
    if(*p == '\0') return true; // blank
    // The settings dump Marauder prints after a join -- and, critically, every
    // line that carries the plaintext password.
    if(*p == '#') return true; // command echo, e.g. "#join -a 15 -p <password>"
    if(mm_ci_strstr(p, " -p ")) return true; // any residual "join ... -p <pw>"
    if(mm_ci_strstr(p, "Password:")) return true; // "Using SSID: X Password: Y"
    if(strncmp(p, "Value:", 6) == 0) return true; // includes ClientPW value
    if(strncmp(p, "Name:", 5) == 0) return true;
    if(strncmp(p, "Type:", 5) == 0) return true;
    if(strncmp(p, "Settings", 8) == 0) return true;
    if(*p == '-') return true; // "----" separators
    return false;
}

bool mm_join_parse_ip(const char* line, char* ip_out) {
    if(!line || !ip_out) return false;
    for(const char* p = line; *p; p++) {
        if(*p < '0' || *p > '9') continue;
        int a, b, c, d, n = 0;
        if(sscanf(p, "%d.%d.%d.%d%n", &a, &b, &c, &d, &n) == 4) {
            if(a >= 0 && a <= 255 && b >= 0 && b <= 255 && c >= 0 && c <= 255 && d >= 0 &&
               d <= 255 && !(a == 0 && b == 0 && c == 0 && d == 0)) {
                snprintf(ip_out, 16, "%d.%d.%d.%d", a, b, c, d);
                return true;
            }
        }
        // skip the rest of this number run to avoid rescanning digits
        while(*p >= '0' && *p <= '9') p++;
        if(!*p) break;
    }
    return false;
}

bool mm_bt_line_unsupported(const char* line) {
    // Firmware prints exactly "Bluetooth not supported" on WiFi-only boards
    // (ESP32-S2) -- CommandLine.cpp. Require both words so other "not supported"
    // strings (SD card, GPS) can't be mistaken for it.
    return line && mm_ci_strstr(line, "bluetooth") != NULL &&
           mm_ci_strstr(line, "not supported") != NULL;
}

bool mm_info_line_is_marauder(const char* line) {
    // info opens with "Firmware: Marauder"; any line naming Marauder proves a
    // board is answering on the UART.
    return line && mm_ci_strstr(line, "marauder") != NULL;
}

MMCap mm_info_line_sd(const char* line) {
    if(!line || !mm_ci_strstr(line, "sd card")) return MMCapUnknown;
    // "SD Card: Not Connected" must beat the substring "Connected".
    if(mm_ci_strstr(line, "not connected")) return MMCapNo;
    if(mm_ci_strstr(line, "connected")) return MMCapYes;
    return MMCapUnknown;
}

MMCap mm_info_line_bt(const char* line) {
    if(!line || !mm_ci_strstr(line, "bluetooth")) return MMCapUnknown;
    // "Not Supported" (custom info) and the stock "not supported" both mean No;
    // check that before the plain "Supported" substring.
    if(mm_ci_strstr(line, "not supported")) return MMCapNo;
    if(mm_ci_strstr(line, "supported")) return MMCapYes;
    return MMCapUnknown;
}

MMCap mm_info_line_gps(const char* line) {
    if(!line || !mm_ci_strstr(line, "gps")) return MMCapUnknown;
    // No usable GPS whether the module is absent or the build lacks support.
    if(mm_ci_strstr(line, "not connected") || mm_ci_strstr(line, "not supported"))
        return MMCapNo;
    if(mm_ci_strstr(line, "connected")) return MMCapYes;
    return MMCapUnknown;
}

bool mm_mac_is_broadcast(const char* mac) {
    if(!mac) return false;
    for(const char* p = mac; *p; p++) {
        if(*p == ':') continue;
        char c = *p;
        if(c >= 'A' && c <= 'F') c = (char)(c + 32);
        if(c != 'f') return false;
    }
    return true;
}

// True if c terminates a MAC token (whitespace or end-of-string). Fixed-offset
// MAC readers use this to reject an over-length token like aa:bb:cc:dd:ee:ffZZ.
static bool mm_is_mac_delim(char c) {
    return c == '\0' || c == ' ' || c == '\r' || c == '\n' || c == '\t';
}

// Copy a MAC token starting at *p (stops at space/end); validate and store in
// out[18]. Returns the char after the token, or NULL if it isn't a MAC.
static const char* mm_take_mac(const char* p, char* out) {
    int n = 0;
    while(p[n] && p[n] != ' ' && p[n] != '\r' && p[n] != '\n' && n < 17) n++;
    if(n != 17) return NULL;
    if(!mm_is_mac_delim(p[17])) return NULL; // reject 17 valid chars + trailing junk
    char tmp[18];
    memcpy(tmp, p, 17);
    tmp[17] = '\0';
    if(!mm_ap_is_bssid(tmp)) return NULL;
    memcpy(out, tmp, 18);
    return p + 17;
}

bool mm_deauth_parse_line(const char* line, MMDeauthFrame* out) {
    if(!line || !out) return false;
    // Anchors that a real "<rssi> Ch: <ch> <src> -> <dst>" line always has.
    const char* ch = mm_ci_strstr(line, " ch: ");
    const char* arrow = strstr(line, " -> ");
    if(!ch || !arrow || arrow < ch) return false;

    const char* p = line;
    while(*p == ' ' || *p == '>' || *p == '\t') p++; // skip any "> " prompt
    out->rssi = (int)strtol(p, NULL, 10);
    out->channel = (int)strtol(ch + 5, NULL, 10); // after " Ch: "

    // src is the MAC immediately before " -> "; walk back to its start.
    const char* s = arrow;
    while(s > line && s[-1] != ' ') s--;
    if(!mm_take_mac(s, out->src)) return false;
    if(!mm_take_mac(arrow + 4, out->dst)) return false;
    return true;
}

// Copy the text after `label` up to `stop` (or end) into out, trimmed.
static void mm_copy_field(const char* start, const char* stop, char* out, size_t out_sz) {
    size_t n = 0;
    const char* p = start;
    while(*p && (!stop || p < stop) && n + 1 < out_sz) out[n++] = *p++;
    while(n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\r' || out[n - 1] == '\n')) n--;
    out[n] = '\0';
}

bool mm_pinescan_parse_line(const char* line, MMPineScan* out) {
    if(!line || !out) return false;
    const char* mac = mm_ci_strstr(line, "mac: ");
    const char* det = mm_ci_strstr(line, " det: ");
    if(!mac || !det) return false; // " det: " is what separates this from multissid
    const char* ch = mm_ci_strstr(line, " ch: ");
    const char* rssi = mm_ci_strstr(line, " rssi: ");
    const char* ssid = mm_ci_strstr(line, " ssid: ");

    // MAC is a fixed 17-char token after "MAC: ".
    {
        char tmp[18];
        mm_copy_field(mac + 5, mac + 5 + 17, tmp, sizeof(tmp));
        if(!mm_ap_is_bssid(tmp)) return false;
        if(!mm_is_mac_delim((mac + 5)[17])) return false; // reject over-length MAC
        memcpy(out->mac, tmp, 18);
    }
    out->channel = ch ? (int)strtol(ch + 5, NULL, 10) : 0;
    out->rssi = rssi ? (int)strtol(rssi + 7, NULL, 10) : 0;
    mm_copy_field(det + 6, ssid, out->det, sizeof(out->det));
    if(ssid) {
        mm_copy_field(ssid + 7, NULL, out->ssid, sizeof(out->ssid));
    } else {
        out->ssid[0] = '\0';
    }
    return true;
}

bool mm_multissid_parse_line(const char* line, MMRogueAp* out) {
    if(!line || !out) return false;
    const char* mac = mm_ci_strstr(line, "mac: ");
    const char* cnt = mm_ci_strstr(line, " ssids: "); // distinguishes from pinescan
    if(!mac || !cnt) return false;
    const char* ch = mm_ci_strstr(line, " ch: ");
    const char* rssi = mm_ci_strstr(line, " rssi: ");
    const char* ssid = mm_ci_strstr(line, " ssid: "); // essid (won't match " ssids: ")

    char tmp[18];
    mm_copy_field(mac + 5, mac + 5 + 17, tmp, sizeof(tmp));
    if(!mm_ap_is_bssid(tmp)) return false;
    if(!mm_is_mac_delim((mac + 5)[17])) return false; // reject over-length MAC
    memcpy(out->mac, tmp, 18);
    out->channel = ch ? (int)strtol(ch + 5, NULL, 10) : 0;
    out->rssi = rssi ? (int)strtol(rssi + 7, NULL, 10) : 0;
    out->ssid_count = (int)strtol(cnt + 8, NULL, 10);
    if(ssid) {
        mm_copy_field(ssid + 7, NULL, out->ssid, sizeof(out->ssid));
    } else {
        out->ssid[0] = '\0';
    }
    return true;
}

bool mm_pwn_line_name(const char* line, char* out, size_t out_sz) {
    if(!line || !out) return false;
    const char* p = line;
    while(*p == ' ' || *p == '>') p++;
    if(strncmp(p, "Name: ", 6) != 0) return false;
    mm_copy_field(p + 6, NULL, out, out_sz);
    return out[0] != '\0';
}

bool mm_pwn_line_pwnd(const char* line, int* out) {
    if(!line || !out) return false;
    const char* p = mm_ci_strstr(line, "pwnd #: ");
    if(!p) return false;
    *out = (int)strtol(p + 8, NULL, 10);
    return true;
}

bool mm_pwn_line_mac(const char* line, char* out) {
    if(!line || !out) return false;
    const char* p = line;
    while(*p == ' ' || *p == '>') p++;
    if(strncmp(p, "MAC: ", 5) != 0) return false;
    char tmp[18];
    mm_copy_field(p + 5, p + 5 + 17, tmp, sizeof(tmp));
    if(!mm_ap_is_bssid(tmp)) return false;
    if(!mm_is_mac_delim((p + 5)[17])) return false; // reject over-length MAC
    memcpy(out, tmp, 18);
    return true;
}

MMCap mm_info_line_direct_upload(const char* line) {
    if(!line || !mm_ci_strstr(line, "direct upload")) return MMCapUnknown;
    if(mm_ci_strstr(line, "not supported")) return MMCapNo;
    if(mm_ci_strstr(line, "supported")) return MMCapYes;
    return MMCapUnknown;
}

MMCap mm_info_line_dual_band(const char* line) {
    if(!line || !mm_ci_strstr(line, "dual band")) return MMCapUnknown;
    if(mm_ci_strstr(line, "not supported")) return MMCapNo;
    if(mm_ci_strstr(line, "supported")) return MMCapYes;
    return MMCapUnknown;
}

MMJoinLineType mm_join_classify(const char* line) {
    if(!line) return MMJoinLineNone;
    if(mm_ci_strstr(line, "fail") || mm_ci_strstr(line, "disconnect") ||
       mm_ci_strstr(line, "error") || mm_ci_strstr(line, "no ssid") ||
       mm_ci_strstr(line, "not found") || mm_ci_strstr(line, "could not") ||
       mm_ci_strstr(line, "couldn't") || mm_ci_strstr(line, "unable"))
        return MMJoinLineFailed;
    if(mm_ci_strstr(line, "connected") || mm_ci_strstr(line, "wifi connected") ||
       mm_ci_strstr(line, "got ip"))
        return MMJoinLineConnected;
    if(mm_ci_strstr(line, "connecting")) return MMJoinLineConnecting;
    return MMJoinLineNone;
}

// ===========================================================================
// GPS parsers
// ===========================================================================

void mm_gps_fix_reset(MMGpsFix* out) {
    if(!out) return;
    memset(out, 0, sizeof(*out));
    out->sats = -1;
    out->have_sats = false;
    out->has_fix = false;
}

// If `line` (after the prompt) begins with `label`, copy the trimmed remainder
// into out[0..out_sz) and return true.
static bool mm_gps_take(const char* line, const char* label, char* out, size_t out_sz) {
    const char* p = mm_skip_prompt(line);
    size_t ll = strlen(label);
    if(strncmp(p, label, ll) != 0) return false;
    p += ll;
    while(*p == ' ' || *p == '\t') p++;
    mm_copy_field(p, NULL, out, out_sz);
    return true;
}

// Parse "Yes"/"No" (case-insensitive) into *b. Returns true if recognised.
static bool mm_gps_yesno(const char* s, bool* b) {
    if(mm_ci_strstr(s, "yes")) {
        *b = true;
        return true;
    }
    if(mm_ci_strstr(s, "no")) {
        *b = false;
        return true;
    }
    return false;
}

bool mm_gps_fix_update(const char* line, MMGpsFix* fix) {
    if(!line || !fix) return false;
    char val[MM_GPS_STR];

    // Fix: block "Good Fix:" and terse "Fix:" (check the longer label first).
    if(mm_gps_take(line, "Good Fix:", val, sizeof(val)) ||
       mm_gps_take(line, "Fix:", val, sizeof(val))) {
        bool b;
        if(mm_gps_yesno(val, &b)) {
            fix->has_fix = b;
            return true;
        }
        return false;
    }
    if(mm_gps_take(line, "Satellites:", val, sizeof(val)) ||
       mm_gps_take(line, "Sats:", val, sizeof(val))) {
        char* end;
        long n = strtol(val, &end, 10);
        if(end == val) return false;
        fix->sats = (int)n;
        fix->have_sats = true;
        return true;
    }
    // Accuracy: v1.10.2 gpsdata block + `gps -g` use "Accuracy:"; v1.17.0's
    // gpsdata block shortened it to "Acc:". Accept both (version-agnostic).
    if(mm_gps_take(line, "Accuracy:", fix->accuracy, sizeof(fix->accuracy)) ||
       mm_gps_take(line, "Acc:", fix->accuracy, sizeof(fix->accuracy)))
        return true;
    if(mm_gps_take(line, "Latitude:", fix->lat, sizeof(fix->lat)) ||
       mm_gps_take(line, "Lat:", fix->lat, sizeof(fix->lat)))
        return true;
    if(mm_gps_take(line, "Longitude:", fix->lon, sizeof(fix->lon)) ||
       mm_gps_take(line, "Lon:", fix->lon, sizeof(fix->lon)))
        return true;
    if(mm_gps_take(line, "Altitude:", fix->alt, sizeof(fix->alt)) ||
       mm_gps_take(line, "Alt:", fix->alt, sizeof(fix->alt)))
        return true;
    // Datetime: "Datetime:" (v1.10.2 block), "Date/Time:" (terse), and "D/T:"
    // (v1.17.0 block). Accept all three.
    if(mm_gps_take(line, "Datetime:", fix->datetime, sizeof(fix->datetime)) ||
       mm_gps_take(line, "Date/Time:", fix->datetime, sizeof(fix->datetime)) ||
       mm_gps_take(line, "D/T:", fix->datetime, sizeof(fix->datetime)))
        return true;
    if(mm_gps_take(line, "Text:", fix->text, sizeof(fix->text))) return true;
    // Bare status line from `gps -g text`, e.g. "ANTENNA OK".
    {
        const char* p = mm_skip_prompt(line);
        if(strncmp(p, "ANTENNA", 7) == 0) {
            mm_copy_field(p, NULL, fix->text, sizeof(fix->text));
            return true;
        }
    }
    return false;
}

int mm_gps_parse_buffer(const char* text, MMGpsFix* fix) {
    if(!text || !fix) return 0;
    mm_gps_fix_reset(fix);
    int updated = 0;
    const char* p = text;
    char line[128];
    while(*p) {
        const char* nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        size_t copy = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
        memcpy(line, p, copy);
        line[copy] = '\0';
        if(mm_gps_fix_update(line, fix)) updated++;
        if(!nl) break;
        p = nl + 1;
    }
    return updated;
}

// --- NMEA ---

bool mm_nmea_checksum_ok(const char* sentence) {
    if(!sentence) return false;
    const char* p = sentence;
    while(*p == ' ' || *p == '\t' || *p == '>') p++;
    if(*p != '$') return false;
    p++;
    unsigned char sum = 0;
    while(*p && *p != '*') {
        sum ^= (unsigned char)*p;
        p++;
    }
    if(*p != '*') return false;
    p++;
    if(!isxdigit((unsigned char)p[0]) || !isxdigit((unsigned char)p[1])) return false;
    unsigned int want = (unsigned int)strtol((char[]){p[0], p[1], '\0'}, NULL, 16);
    return want == sum;
}

// Split a sentence into up to `max` comma fields, pointing into a private copy.
// Returns the field count. `buf` must be >= strlen(sentence)+1.
static int mm_nmea_fields(const char* sentence, char* buf, char** fields, int max) {
    const char* p = sentence;
    while(*p == ' ' || *p == '\t' || *p == '>') p++;
    strcpy(buf, p);
    // Drop a trailing "*HH" and any CR/LF.
    char* star = strchr(buf, '*');
    if(star) *star = '\0';
    size_t n = strlen(buf);
    while(n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == '\n' || buf[n - 1] == ' ')) buf[--n] = '\0';

    int count = 0;
    char* f = buf;
    fields[count++] = f;
    for(char* q = buf; *q && count < max; q++) {
        if(*q == ',') {
            *q = '\0';
            fields[count++] = q + 1;
        }
    }
    return count;
}

// Convert an NMEA "ddmm.mmmm" (lat) / "dddmm.mmmm" (lon) + hemisphere to signed
// decimal degrees. deg_digits is 2 for lat, 3 for lon. Returns false if empty.
static bool mm_nmea_coord(const char* val, const char* hemi, int deg_digits, double* out) {
    (void)deg_digits; // dd vs ddd falls out of raw/100 arithmetic; kept for clarity
    if(!val || !*val) return false;
    // raw = ddmm.mmmm (or dddmm.mmmm); degrees = int(raw/100), minutes = rest.
    // Constants held in double locals so -fsingle-precision-constant can't fold
    // them to float (which would trip -Werror=double-promotion on the target).
    const double hundred = 100.0, sixty = 60.0;
    double raw = strtod(val, NULL);
    int degrees = (int)(raw / hundred);
    double minutes = raw - (double)degrees * hundred;
    double dec = (double)degrees + minutes / sixty;
    if(hemi && (*hemi == 'S' || *hemi == 'W' || *hemi == 's' || *hemi == 'w')) dec = -dec;
    *out = dec;
    return true;
}

bool mm_nmea_parse_gga(const char* sentence, MMNmeaGga* out) {
    if(!sentence || !out) return false;
    char buf[128];
    if(strlen(sentence) >= sizeof(buf)) return false;
    char* f[20];
    int n = mm_nmea_fields(sentence, buf, f, 20);
    if(n < 1) return false;
    // f[0] is "$__GGA"; accept any talker.
    size_t l0 = strlen(f[0]);
    if(l0 < 6 || strcmp(f[0] + l0 - 3, "GGA") != 0) return false;
    if(n < 10) return false; // need through altitude
    memset(out, 0, sizeof(*out));
    if(!mm_nmea_coord(f[2], f[3], 2, &out->lat_deg)) { /* leave 0 */
    }
    if(!mm_nmea_coord(f[4], f[5], 3, &out->lon_deg)) { /* leave 0 */
    }
    out->fix_quality = (int)strtol(f[6], NULL, 10);
    out->sats = (int)strtol(f[7], NULL, 10);
    out->alt_m = strtod(f[9], NULL);
    out->valid = out->fix_quality > 0;
    return true;
}

bool mm_nmea_parse_rmc(const char* sentence, MMNmeaRmc* out) {
    if(!sentence || !out) return false;
    char buf[128];
    if(strlen(sentence) >= sizeof(buf)) return false;
    char* f[20];
    int n = mm_nmea_fields(sentence, buf, f, 20);
    if(n < 1) return false;
    size_t l0 = strlen(f[0]);
    if(l0 < 6 || strcmp(f[0] + l0 - 3, "RMC") != 0) return false;
    if(n < 10) return false;
    memset(out, 0, sizeof(*out));
    // f[1]=time, f[2]=status, f[3/4]=lat, f[5/6]=lon, f[7]=speed, f[8]=course, f[9]=date
    snprintf(out->time, sizeof(out->time), "%.6s", f[1]);
    out->active = (f[2][0] == 'A' || f[2][0] == 'a');
    mm_nmea_coord(f[3], f[4], 2, &out->lat_deg);
    mm_nmea_coord(f[5], f[6], 3, &out->lon_deg);
    out->speed_knots = strtod(f[7], NULL);
    out->course_deg = strtod(f[8], NULL);
    snprintf(out->date, sizeof(out->date), "%.6s", f[9]);
    return true;
}

// True if the "$__XXX" address field f0 ends in the 3-letter sentence type.
static bool mm_nmea_type_is(const char* f0, const char* type) {
    size_t l = strlen(f0);
    return l >= 6 && strcmp(f0 + l - 3, type) == 0;
}

const char* mm_nmea_constellation(const char* talker) {
    if(!talker) return "?";
    if(strncmp(talker, "GP", 2) == 0) return "GPS";
    if(strncmp(talker, "GL", 2) == 0) return "GLO";
    if(strncmp(talker, "GA", 2) == 0) return "GAL";
    if(strncmp(talker, "GB", 2) == 0 || strncmp(talker, "BD", 2) == 0) return "BDS";
    if(strncmp(talker, "GQ", 2) == 0) return "QZSS";
    if(strncmp(talker, "GI", 2) == 0) return "NAVIC";
    if(strncmp(talker, "GN", 2) == 0) return "GNSS";
    return "?";
}

bool mm_nmea_parse_gsv(const char* sentence, MMNmeaGsv* out) {
    if(!sentence || !out) return false;
    char buf[128];
    if(strlen(sentence) >= sizeof(buf)) return false;
    char* f[24];
    int n = mm_nmea_fields(sentence, buf, f, 24);
    if(n < 4 || !mm_nmea_type_is(f[0], "GSV")) return false;

    memset(out, 0, sizeof(*out));
    // Talker = the two chars after '$'.
    const char* p = f[0];
    while(*p == ' ' || *p == '\t' || *p == '>') p++;
    if(*p == '$') p++;
    out->talker[0] = p[0];
    out->talker[1] = p[1];
    out->talker[2] = '\0';

    out->total_msgs = (int)strtol(f[1], NULL, 10);
    out->msg_num = (int)strtol(f[2], NULL, 10);
    out->in_view = (int)strtol(f[3], NULL, 10);

    // Satellites are groups of 4 fields after field 3. A trailing signalID field
    // (u-blox) leaves a remainder of 1, which integer division drops.
    int groups = (n - 4) / 4;
    if(groups > MM_GSV_MAX_SATS) groups = MM_GSV_MAX_SATS;
    int count = 0;
    for(int g = 0; g < groups; g++) {
        const char* prn = f[4 + g * 4];
        const char* elev = f[5 + g * 4];
        const char* azim = f[6 + g * 4];
        const char* snr = f[7 + g * 4];
        if(prn[0] == '\0') continue; // empty group -> skip
        MMSatInfo* s = &out->sats[count];
        s->prn = (int)strtol(prn, NULL, 10);
        s->elevation = (int)strtol(elev, NULL, 10);
        s->azimuth = (int)strtol(azim, NULL, 10);
        if(snr[0] == '\0') {
            s->snr = 0;
            s->has_snr = false;
        } else {
            s->snr = (int)strtol(snr, NULL, 10);
            s->has_snr = true;
        }
        count++;
    }
    out->count = count;
    return true;
}

bool mm_nmea_parse_gsa(const char* sentence, MMNmeaGsa* out) {
    if(!sentence || !out) return false;
    char buf[128];
    if(strlen(sentence) >= sizeof(buf)) return false;
    char* f[24];
    int n = mm_nmea_fields(sentence, buf, f, 24);
    // f[0]=$__GSA f[1]=mode1 f[2]=fixtype f[3..14]=12 PRN slots f[15..17]=P/H/V DOP
    if(n < 18 || !mm_nmea_type_is(f[0], "GSA")) return false;

    memset(out, 0, sizeof(*out));
    out->fix_type = (int)strtol(f[2], NULL, 10);
    int used = 0;
    for(int i = 3; i <= 14; i++) {
        if(f[i][0] != '\0') used++;
    }
    out->sats_used = used;
    out->pdop = strtod(f[15], NULL);
    out->hdop = strtod(f[16], NULL);
    out->vdop = strtod(f[17], NULL);
    return true;
}

bool mm_nmea_parse_vtg(const char* sentence, MMNmeaVtg* out) {
    if(!sentence || !out) return false;
    char buf[128];
    if(strlen(sentence) >= sizeof(buf)) return false;
    char* f[16];
    int n = mm_nmea_fields(sentence, buf, f, 16);
    // f[1]=course true, f[5]=speed knots, f[7]=speed km/h
    if(n < 8 || !mm_nmea_type_is(f[0], "VTG")) return false;

    memset(out, 0, sizeof(*out));
    out->course_deg = strtod(f[1], NULL);
    out->speed_knots = strtod(f[5], NULL);
    out->speed_kmh = strtod(f[7], NULL);
    return true;
}

// ===========================================================================
// Bluetooth parsers
// ===========================================================================

// Strip a trailing " <-NN>" style RSSI (a '-' followed by 1-3 digits at the very
// end of [start,end)) that is glued to a device name in the sniffbt stream.
// Returns the RSSI via *rssi (if found) and the new end (name end).
static const char* mm_strip_trailing_rssi(const char* start, const char* end, int* rssi, bool* found) {
    *found = false;
    const char* e = end;
    // trim trailing spaces first
    while(e > start && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) e--;
    const char* d = e;
    int digits = 0;
    while(d > start && d[-1] >= '0' && d[-1] <= '9' && digits < 3) {
        d--;
        digits++;
    }
    if(digits >= 1 && digits <= 3 && d > start && d[-1] == '-') {
        // require the '-' to abut a MAC/name char, not be a lone token start we want to keep
        *rssi = (int)strtol(d - 1, NULL, 10);
        *found = true;
        return d - 1; // name ends before the '-'
    }
    return e;
}

size_t mm_btall_parse_buffer(const char* text, MMBtDevice* out, size_t max) {
    if(!text || !out || max == 0) return 0;
    const char* MARK = "Device: ";
    const size_t MLEN = 8;

    // Locate the first marker.
    const char* m = strstr(text, MARK);
    if(!m) return 0;

    // The first record's rssi is the trailing signed int of the text before it.
    int cur_rssi = 0;
    bool have_rssi;
    mm_strip_trailing_rssi(text, m, &cur_rssi, &have_rssi);

    size_t count = 0;
    while(m && count < max) {
        const char* name_start = m + MLEN;
        const char* next = strstr(name_start, MARK);
        const char* name_end;
        int next_rssi = 0;
        bool next_has = false;
        if(next) {
            name_end = mm_strip_trailing_rssi(name_start, next, &next_rssi, &next_has);
        } else {
            // Last record: name runs to a control char / prompt / end.
            const char* e = name_start;
            while(*e && *e != '\r' && *e != '\n' && *e != '#' && *e != '>') e++;
            name_end = e;
            // No trailing rssi to strip for the final name.
        }

        // Emit this record.
        char name[33];
        size_t nlen = (size_t)(name_end - name_start);
        // trim surrounding whitespace
        while(nlen > 0 && (name_start[0] == ' ' || name_start[0] == '\t')) {
            name_start++;
            nlen--;
        }
        while(nlen > 0 && (name_start[nlen - 1] == ' ' || name_start[nlen - 1] == '\t')) nlen--;
        if(nlen >= sizeof(name)) nlen = sizeof(name) - 1;
        memcpy(name, name_start, nlen);
        name[nlen] = '\0';

        if(nlen > 0) {
            out[count].rssi = cur_rssi;
            out[count].is_mac = mm_ap_is_bssid(name);
            memcpy(out[count].name, name, nlen + 1);
            count++;
        }

        cur_rssi = next_rssi;
        (void)next_has;
        m = next;
    }
    return count;
}

bool mm_bt_rssi_mac_line(const char* line, int* rssi, char* mac_out) {
    if(!line || !rssi || !mac_out) return false;
    const char* p = mm_skip_prompt(line);
    char* end;
    long r = strtol(p, &end, 10);
    if(end == p) return false;
    p = end;
    while(*p == ' ' || *p == '\t') p++;
    if(strncmp(p, "MAC:", 4) != 0) return false;
    p += 4;
    while(*p == ' ' || *p == '\t') p++;
    if(!mm_take_mac(p, mac_out)) return false;
    *rssi = (int)r;
    return true;
}

bool mm_bt_name_line(const char* line, char* out, size_t out_sz) {
    return mm_gps_take(line, "Name:", out, out_sz);
}

bool mm_bt_len_line(const char* line, int* len) {
    if(!line || !len) return false;
    const char* p = mm_skip_prompt(line);
    if(strncmp(p, "Len:", 4) != 0) return false;
    p += 4;
    while(*p == ' ' || *p == '\t') p++;
    char* end;
    long v = strtol(p, &end, 10);
    if(end == p) return false;
    *len = (int)v;
    return true;
}

bool mm_btwardrive_parse_line(const char* line, MMBtWardrive* out) {
    if(!line || !out) return false;
    const char* anchor = strstr(line, ",,[BLE],");
    if(!anchor) return false;
    // MAC is the 17 chars immediately before the anchor.
    if(anchor - line < 17) return false;
    const char* macp = anchor - 17;
    char mac[18];
    memcpy(mac, macp, 17);
    mac[17] = '\0';
    if(!mm_ap_is_bssid(mac)) return false;

    // After the anchor: "<datetime>,0,<rssi>,<lat>,<lon>,<alt>,<accuracy>,BLE"
    const char* p = anchor + strlen(",,[BLE],");
    char buf[160];
    if(strlen(p) >= sizeof(buf)) return false;
    strcpy(buf, p);
    // trim trailing CR/LF
    size_t n = strlen(buf);
    while(n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == '\n')) buf[--n] = '\0';

    char* f[10];
    int count = 0;
    f[count++] = buf;
    for(char* q = buf; *q && count < 10; q++) {
        if(*q == ',') {
            *q = '\0';
            f[count++] = q + 1;
        }
    }
    // f: [0]=datetime [1]=channel(0) [2]=rssi [3]=lat [4]=lon [5]=alt [6]=accuracy [7]=BLE
    if(count < 7) return false;

    strcpy(out->mac, mac);
    // mm_copy_field (bounded, no format string) avoids -Werror=format-truncation
    // that snprintf("%s", ...) trips when the source could exceed the field.
    mm_copy_field(f[0], NULL, out->datetime, sizeof(out->datetime));
    out->rssi = (int)strtol(f[2], NULL, 10);
    mm_copy_field(f[3], NULL, out->lat, sizeof(out->lat));
    mm_copy_field(f[4], NULL, out->lon, sizeof(out->lon));
    mm_copy_field(f[5], NULL, out->alt, sizeof(out->alt));
    mm_copy_field(f[6], NULL, out->accuracy, sizeof(out->accuracy));
    return true;
}

bool mm_wardrive_parse_line(const char* line, MMWardriveAp* out) {
    if(!line || !out) return false;

    // Data starts after the "<cursor> | " display prefix when present; else
    // after any "> " prompt.
    const char* data = strstr(line, " | ");
    if(data)
        data += 3;
    else
        data = mm_skip_prompt(line);

    char buf[160];
    if(strlen(data) >= sizeof(buf)) return false;
    strcpy(buf, data);
    size_t n = strlen(buf);
    while(n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == '\n' || buf[n - 1] == ' ')) buf[--n] = '\0';

    // A real record ends in ",WIFI"; this rejects banners and the "APs: N" line.
    const size_t suffix_len = 5; // ",WIFI"
    if(n < suffix_len || strcmp(buf + n - suffix_len, ",WIFI") != 0) return false;

    // Comma-split (SSID commas are pre-replaced with '_' by the firmware).
    char* f[12];
    int count = 0;
    f[count++] = buf;
    for(char* q = buf; *q && count < 12; q++) {
        if(*q == ',') {
            *q = '\0';
            f[count++] = q + 1;
        }
    }
    // f: [0]bssid [1]ssid [2]auth [3]datetime [4]ch [5]rssi [6]lat [7]lon
    //    [8]alt [9]accuracy [10]WIFI
    if(count < 11) return false;
    if(!mm_ap_is_bssid(f[0])) return false;

    strcpy(out->bssid, f[0]);
    mm_copy_field(f[1], NULL, out->ssid, sizeof(out->ssid));
    mm_copy_field(f[2], NULL, out->auth, sizeof(out->auth));
    mm_copy_field(f[3], NULL, out->datetime, sizeof(out->datetime));
    out->channel = (int)strtol(f[4], NULL, 10);
    out->rssi = (int)strtol(f[5], NULL, 10);
    mm_copy_field(f[6], NULL, out->lat, sizeof(out->lat));
    mm_copy_field(f[7], NULL, out->lon, sizeof(out->lon));
    mm_copy_field(f[8], NULL, out->alt, sizeof(out->alt));
    mm_copy_field(f[9], NULL, out->accuracy, sizeof(out->accuracy));
    out->hidden = (out->ssid[0] == '\0');
    return true;
}
