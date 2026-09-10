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

bool mm_scanall_parse_ap(const char* line, MMScanAp* out) {
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

    // BSSID token
    const char* bstart = p;
    while(*p && *p != ' ' && *p != '\t') p++;
    if((size_t)(p - bstart) != 17) return false;
    char bssid[MM_BSSID_LEN];
    memcpy(bssid, bstart, 17);
    bssid[17] = '\0';
    if(!mm_ap_is_bssid(bssid)) return false;
    while(*p == ' ') p++;

    if(strncmp(p, "ESSID:", 6) != 0) return false;
    p += 6;

    // Remainder holds "<ssid> <m1> <m2>". Strip trailing whitespace, then drop
    // the two trailing metadata tokens positionally; what's left is the ESSID.
    const char* rem = p;
    size_t rlen = strlen(rem);
    while(rlen > 0 && (rem[rlen - 1] == '\n' || rem[rlen - 1] == '\r' ||
                       rem[rlen - 1] == ' ' || rem[rlen - 1] == '\t'))
        rlen--;

    size_t i = rlen;
    for(int tok = 0; tok < 2; tok++) {
        while(i > 0 && rem[i - 1] != ' ' && rem[i - 1] != '\t') i--; // skip token
        while(i > 0 && (rem[i - 1] == ' ' || rem[i - 1] == '\t')) i--; // skip gap
    }
    size_t essid_end = i;

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

bool mm_listi_parse_ip(const char* line, char* ip_out) {
    if(!line || !ip_out) return false;
    const char* p = line;
    while(*p == ' ' || *p == '\t') p++;
    if(*p != '[') return false;
    char* end;
    strtol(p + 1, &end, 10);
    if(end == p + 1 || *end != ']') return false;
    p = end + 1;
    while(*p == ' ') p++;

    size_t n = strlen(p);
    while(n > 0 && (p[n - 1] == '\r' || p[n - 1] == '\n' || p[n - 1] == ' ')) n--;
    if(n == 0 || n >= 16) return false;
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

    const char* bstart = p;
    while(*p && *p != ' ' && *p != '\t') p++;
    if((size_t)(p - bstart) != 17) return false;
    char bssid[MM_BSSID_LEN];
    memcpy(bssid, bstart, 17);
    bssid[17] = '\0';
    if(!mm_ap_is_bssid(bssid)) return false;
    while(*p == ' ') p++;

    if(strncmp(p, "ESSID:", 6) != 0) return false;
    p += 6;
    while(*p == ' ') p++;

    // ESSID runs to end of line (no trailing metadata for sniffbeacon).
    const char* rem = p;
    size_t n = strlen(rem);
    while(n > 0 && (rem[n - 1] == '\r' || rem[n - 1] == '\n' || rem[n - 1] == ' ' ||
                    rem[n - 1] == '\t'))
        n--;
    if(n >= MM_AP_NAME_MAX) n = MM_AP_NAME_MAX - 1;
    char ssid[MM_AP_NAME_MAX];
    memcpy(ssid, rem, n);
    ssid[n] = '\0';

    strcpy(out->bssid, bssid);
    out->channel = (int)ch;
    out->rssi = (int)rssi;
    if(n == 0 || strcmp(ssid, bssid) == 0) {
        out->hidden = true;
        out->ssid[0] = '\0';
    } else {
        out->hidden = false;
        strcpy(out->ssid, ssid);
    }
    return true;
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

// Copy a MAC token starting at *p (stops at space/end); validate and store in
// out[18]. Returns the char after the token, or NULL if it isn't a MAC.
static const char* mm_take_mac(const char* p, char* out) {
    int n = 0;
    while(p[n] && p[n] != ' ' && p[n] != '\r' && p[n] != '\n' && n < 17) n++;
    if(n != 17) return NULL;
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
