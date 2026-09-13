// Host-side unit test for the scanall parser + dedup store. Builds+runs via
// tools/run_tests.sh, asserting against dumps/scanall_sample.txt.
#include "../marauders_mate_ap_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, ...)                               \
    do {                                               \
        if(!(cond)) {                                  \
            printf("FAIL: ");                          \
            printf(__VA_ARGS__);                       \
            printf("  (%s:%d)\n", __FILE__, __LINE__); \
            failures++;                                \
        }                                              \
    } while(0)

static char* slurp(const char* path) {
    FILE* f = fopen(path, "rb");
    if(!f) {
        printf("FAIL: cannot open %s\n", path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc(n + 1);
    size_t rd = fread(buf, 1, n, f);
    buf[rd] = '\0';
    fclose(f);
    return buf;
}

int main(void) {
    MMScanAp ap;

    // --- AP line parsing ---
    // Named AP, with "> " prompt and a space in the SSID.
    CHECK(mm_scanall_parse_ap("> -60 Ch: 3 02:00:00:00:00:04 ESSID: Harbor View 31 04 ", &ap),
          "named AP should parse");
    CHECK(strcmp(ap.bssid, "02:00:00:00:00:04") == 0, "bssid: got '%s'", ap.bssid);
    CHECK(strcmp(ap.ssid, "Harbor View") == 0, "ssid with space: got '%s'", ap.ssid);
    CHECK(ap.channel == 3 && ap.rssi == -60 && !ap.hidden, "named fields");

    // Hidden AP: ESSID slot repeats the BSSID.
    CHECK(mm_scanall_parse_ap("> -85 Ch: 3 02:00:00:00:00:01 ESSID:  02:00:00:00:00:01 31 14 ", &ap),
          "hidden AP should parse");
    CHECK(ap.hidden && ap.ssid[0] == '\0', "hidden -> empty ssid: '%s'", ap.ssid);
    CHECK(strcmp(ap.bssid, "02:00:00:00:00:01") == 0, "hidden bssid");

    // Hidden AP with heavy padding before the repeated BSSID.
    CHECK(
        mm_scanall_parse_ap(
            "-60 Ch: 8 02:00:00:00:00:0a ESSID:                                02:00:00:00:00:0a 11 15 ",
            &ap),
        "padded hidden AP should parse");
    CHECK(ap.hidden && ap.channel == 8, "padded hidden fields");

    // Hyphenated SSID must not be confused with metadata.
    CHECK(mm_scanall_parse_ap("-53 Ch: 11 02:00:00:00:00:0b ESSID: 4833-WDS 31 14 ", &ap),
          "hyphen SSID should parse");
    CHECK(strcmp(ap.ssid, "4833-WDS") == 0, "hyphen ssid: got '%s'", ap.ssid);

    // --- station line parsing (both directions) ---
    char apb[MM_BSSID_LEN], sta[MM_BSSID_LEN];
    CHECK(mm_scanall_parse_station("15: ap: 02:00:00:00:00:04 -> sta: 01:00:5e:7f:ff:fa", apb, sta),
          "ap->sta line should parse");
    CHECK(strcmp(apb, "02:00:00:00:00:04") == 0 && strcmp(sta, "01:00:5e:7f:ff:fa") == 0,
          "ap->sta fields: ap='%s' sta='%s'", apb, sta);
    CHECK(mm_scanall_parse_station("17: sta: 02:00:00:00:01:02 -> ap: 02:00:00:00:00:0b", apb, sta),
          "sta->ap line should parse");
    CHECK(strcmp(apb, "02:00:00:00:00:0b") == 0 && strcmp(sta, "02:00:00:00:01:02") == 0,
          "sta->ap fields: ap='%s' sta='%s'", apb, sta);

    // --- classification ---
    CHECK(mm_scanall_classify("> -60 Ch: 3 02:00:00:00:00:04 ESSID: Harbor View 31 04") ==
              MMScanLineAp,
          "classify AP");
    CHECK(mm_scanall_classify("15: ap: 02:00:00:00:00:04 -> sta: 01:00:5e:7f:ff:fa") ==
              MMScanLineStation,
          "classify station");
    CHECK(mm_scanall_classify("Scanning for APs and Stations. Stop with stopscan") ==
              MMScanLineNone,
          "classify noise");
    CHECK(mm_scanall_classify("> #scanall") == MMScanLineNone, "classify command echo");

    // --- dedup store ---
    MMScanAp store[64];
    int count = 0;
    bool is_new;
    MMScanAp a1 = {"02:00:00:00:00:06", "Lantern", 6, -79, false};
    MMScanAp a1b = {"02:00:00:00:00:06", "Lantern", 6, -70, false}; // re-sighting
    mm_scan_store_upsert(store, &count, 64, &a1, &is_new);
    CHECK(is_new, "first sighting is new");
    mm_scan_store_upsert(store, &count, 64, &a1b, &is_new);
    CHECK(!is_new && count == 1, "re-sighting updates in place, count stays 1");
    CHECK(store[0].rssi == -70, "rssi updated on re-sighting: got %d", store[0].rssi);

    // --- full-buffer run: classify + upsert everything ---
    char* text = slurp("dumps/scanall_sample.txt");
    MMScanAp all[128];
    int n = 0;
    int stations = 0;
    char* save;
    for(char* line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        MMScanLineType t = mm_scanall_classify(line);
        if(t == MMScanLineAp) {
            MMScanAp cur;
            if(mm_scanall_parse_ap(line, &cur)) mm_scan_store_upsert(all, &n, 128, &cur, NULL);
        } else if(t == MMScanLineStation) {
            char b[MM_BSSID_LEN], m[MM_BSSID_LEN];
            if(mm_scanall_parse_station(line, b, m)) stations++;
        }
    }
    CHECK(n == 14, "expected 14 unique APs, got %d", n);
    CHECK(stations == 4, "expected 4 station lines, got %d", stations);

    int hidden = 0;
    for(int i = 0; i < n; i++)
        if(all[i].hidden) hidden++;
    CHECK(hidden == 4, "expected 4 hidden APs, got %d", hidden);

    // duplicate SSID / different BSSID must remain two distinct entries
    int lantern = 0;
    for(int i = 0; i < n; i++)
        if(strcmp(all[i].ssid, "Lantern") == 0) lantern++;
    CHECK(lantern == 2, "expected 2 distinct 'Lantern' APs, got %d", lantern);


    // --- select-index resolver ---
    {
        MMAccessPoint la[4] = {
            {0, 2, -70, true, "02:00:00:00:00:01"}, // hidden: list -a shows BSSID
            {1, 3, -60, false, "Basecamp"},
            {5, 6, -79, false, "Lantern"},
            {6, 6, -68, false, "Lantern"}, // duplicate SSID, different radio
        };
        MMScanAp t_hidden = {"02:00:00:00:00:01", "", 2, -70, true};
        MMScanAp t_named = {"02:00:00:00:00:03", "Basecamp", 3, -60, false};
        MMScanAp t_dupA = {"02:00:00:00:00:06", "Lantern", 6, -79, false};
        MMScanAp t_dupB = {"02:00:00:00:00:07", "Lantern", 6, -68, false};

        // discovery order verifies
        CHECK(mm_resolve_select_index(&t_hidden, 0, la, 4) == 0, "hidden resolves by order");
        CHECK(mm_resolve_select_index(&t_named, 1, la, 4) == 1, "named resolves by order");
        // duplicate SSID: discovery order disambiguates the exact radio
        CHECK(mm_resolve_select_index(&t_dupA, 2, la, 4) == 2, "dup A by order");
        CHECK(mm_resolve_select_index(&t_dupB, 3, la, 4) == 3, "dup B by order");
        // discovery order out of range but unique name -> fallback resolves
        CHECK(mm_resolve_select_index(&t_named, 99, la, 4) == 1, "named unique fallback");
        // duplicate SSID with an unverifiable order -> ambiguous -> -1
        CHECK(mm_resolve_select_index(&t_dupA, 99, la, 4) == -1, "dup ambiguous fallback -> -1");
    }


    // --- list -c parsing (stations grouped under APs) ---
    {
        int ap_i = -1, sel = -1;
        char mac[MM_BSSID_LEN];
        CHECK(mm_listc_parse_ap_header("[0] Homestead -56:", &ap_i) && ap_i == 0,
              "listc named AP header");
        CHECK(mm_listc_parse_ap_header("[1] 02:00:00:00:00:15 -55:", &ap_i) && ap_i == 1,
              "listc hidden AP header");
        CHECK(!mm_listc_parse_ap_header("  [0] 02:00:00:00:01:00", &ap_i),
              "indented line is not an AP header");
        CHECK(!mm_listc_parse_ap_header("0 selected", &ap_i), "'selected' not a header");

        CHECK(mm_listc_parse_station("  [0] 02:00:00:00:01:00", &sel, mac) && sel == 0 &&
                  strcmp(mac, "02:00:00:00:01:00") == 0,
              "listc station 0");
        CHECK(mm_listc_parse_station("  [7] 02:00:00:00:01:07", &sel, mac) && sel == 7,
              "listc station 7 index");
        CHECK(!mm_listc_parse_station("[0] Homestead -56:", &sel, mac),
              "AP header is not a station");

        // Walk a small nested block: assign stations to the current AP.
        const char* blk =
            "[0] Homestead -56:\n"
            "  [0] 02:00:00:00:01:00\n"
            "  [7] 02:00:00:00:01:07\n"
            "[3] 4833-WDS -56:\n"
            "  [3] 02:00:00:00:01:03\n";
        int cur = -1, pairs = 0, sch = 0;
        char line[128];
        const char* q = blk;
        while(*q) {
            const char* nl = strchr(q, '\n');
            size_t len = nl ? (size_t)(nl - q) : strlen(q);
            size_t cpy = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
            memcpy(line, q, cpy);
            line[cpy] = 0;
            int ai;
            if(mm_listc_parse_ap_header(line, &ai)) {
                cur = ai;
            } else if(mm_listc_parse_station(line, &sel, mac)) {
                pairs++;
                if(cur == 0) sch++;
            }
            if(!nl) break;
            q = nl + 1;
        }
        CHECK(pairs == 3, "walked 3 stations, got %d", pairs);
        CHECK(sch == 2, "Homestead has 2 stations, got %d", sch);
    }


    // --- Fox Hunt RSSI parsing ---
    {
        int r = 0;
        CHECK(mm_foxhunt_parse_rssi("Homestead RSSI: -59", &r) && r == -59, "foxhunt rssi");
        CHECK(mm_foxhunt_parse_rssi("> Homestead RSSI: -19", &r) && r == -19,
              "foxhunt rssi with prompt");
        CHECK(mm_foxhunt_parse_rssi("Harbor View RSSI: -72", &r) && r == -72,
              "foxhunt rssi spaced name");
        CHECK(!mm_foxhunt_parse_rssi("StartingFox Hunt. Stop with stopscan", &r),
              "foxhunt banner rejected");
        CHECK(!mm_foxhunt_parse_rssi("Stopping WiFi tran/recv", &r), "foxhunt stop rejected");
    }


    // --- NUL sanitization (hidden APs pad ESSID with 0x00) ---
    {
        uint8_t b[8] = {'A', 0, 'B', 0, 0, 'C', 0, 'D'};
        mm_sanitize_nuls(b, 8);
        CHECK(b[0]=='A' && b[1]==' ' && b[2]=='B' && b[3]==' ' && b[4]==' ' &&
                  b[5]=='C' && b[6]==' ' && b[7]=='D',
              "nuls -> spaces");
        // A hidden AP line whose padding is NUL parses once sanitized.
        char raw[] = "-54 Ch: 3 02:00:00:00:00:1b ESSID: XXX 02:00:00:00:00:1b 11 04";
        raw[35]=0; raw[36]=0; raw[37]=0; // turn the "XXX" padding into NULs
        mm_sanitize_nuls((uint8_t*)raw, sizeof(raw)-1);
        MMScanAp h;
        CHECK(mm_scanall_parse_ap(raw, &h) && h.hidden &&
                  strcmp(h.bssid, "02:00:00:00:00:1b") == 0,
              "NUL-padded hidden AP parses after sanitize");
    }



    // --- sniffbeacon line parsing (no trailing metadata; ESSID to EOL) ---
    {
        MMScanAp b;
        CHECK(mm_beacon_parse_line("> -83 Ch: 1 02:00:00:00:00:0d ESSID: Lighthouse", &b),
              "beacon line parses");
        CHECK(strcmp(b.bssid, "02:00:00:00:00:0d") == 0 && strcmp(b.ssid, "Lighthouse") == 0 &&
                  b.channel == 1 && b.rssi == -83 && !b.hidden,
              "beacon fields");
        // ESSID with a space must survive (would break the scanall parser).
        CHECK(mm_beacon_parse_line("-60 Ch: 3 02:00:00:00:00:1a ESSID: Harbor View", &b) &&
                  strcmp(b.ssid, "Harbor View") == 0,
              "beacon spaced ssid: got '%s'", b.ssid);
        // Empty ESSID -> hidden.
        CHECK(mm_beacon_parse_line("-70 Ch: 6 aa:bb:cc:dd:ee:ff ESSID: ", &b) && b.hidden,
              "beacon empty essid -> hidden");
        CHECK(!mm_beacon_parse_line("StartingBeacon sniff. Stop with stopscan", &b),
              "beacon banner rejected");
    }


    // --- sniffprobe line parsing (client + requested SSID) ---
    {
        MMScanAp pr;
        CHECK(mm_probe_parse_line(
                  "> -80 Ch: 5 Client: 02:00:00:00:02:01 Requesting: TestNet", &pr),
              "probe line parses");
        CHECK(strcmp(pr.bssid, "02:00:00:00:02:01") == 0 && strcmp(pr.ssid, "TestNet") == 0 &&
                  pr.channel == 5 && pr.rssi == -80 && !pr.hidden,
              "probe fields");
        // Empty request -> hidden (broadcast probe).
        CHECK(mm_probe_parse_line("-88 Ch: 2 Client: 02:00:00:00:02:01 Requesting: ", &pr) &&
                  pr.hidden,
              "probe empty request -> hidden");
        // Requested SSID may contain spaces.
        CHECK(mm_probe_parse_line("-70 Ch: 6 Client: 02:00:00:00:02:02 Requesting: My Net", &pr) &&
                  strcmp(pr.ssid, "My Net") == 0,
              "probe spaced ssid: got '%s'", pr.ssid);
        CHECK(!mm_probe_parse_line("StartingProbe sniff. Stop with stopscan", &pr),
              "probe banner rejected");
    }

    free(text);

    if(failures == 0) {
        printf("OK: all scanall-parser tests passed\n");
        return 0;
    }
    printf("%d test(s) failed\n", failures);
    return 1;
}
