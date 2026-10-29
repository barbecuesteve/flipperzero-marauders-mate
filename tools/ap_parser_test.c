// Host-side unit test for the AP parser. Reads dumps/list_sample.log (the real
// capture) and asserts the parsed result. Build+run:
//   cc -Wall -Wextra -std=c11 -I.. tools/ap_parser_test.c ../marauders_mate_ap_parser.c -o /tmp/mm_test && /tmp/mm_test
// (see tools/run_tests.sh)
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
    // --- unit checks on single lines ---
    MMAccessPoint ap;
    CHECK(mm_ap_parse_line("[4][CH:3] Harbor View -57", &ap), "space-name line should parse");
    CHECK(strcmp(ap.name, "Harbor View") == 0, "name with space: got '%s'", ap.name);
    CHECK(ap.index == 4 && ap.channel == 3 && ap.rssi == -57, "space-name fields");
    CHECK(!ap.hidden, "named AP not hidden");

    CHECK(mm_ap_parse_line("[18][CH:11] 4833-WDS -48", &ap), "hyphen-name line should parse");
    CHECK(strcmp(ap.name, "4833-WDS") == 0, "hyphen name: got '%s'", ap.name);
    CHECK(ap.rssi == -48, "hyphen name rssi: got %d", ap.rssi);

    CHECK(mm_ap_parse_line("[2][CH:2] 02:00:00:00:00:02 -77", &ap), "bssid line should parse");
    CHECK(ap.hidden, "bssid row should be flagged hidden");
    CHECK(strcmp(ap.name, "02:00:00:00:00:02") == 0, "bssid name: got '%s'", ap.name);

    // --- 5 GHz (dual-band C5): channels 36..177 must pass through unclamped ---
    CHECK(mm_ap_parse_line("[7][CH:149] Schmetterling_5G -58", &ap), "5GHz list -a should parse");
    CHECK(ap.channel == 149, "5GHz channel kept: got %d", ap.channel);
    CHECK(strcmp(ap.name, "Schmetterling_5G") == 0, "5GHz name: got '%s'", ap.name);
    CHECK(mm_ap_parse_line("[8][CH:36] CoastNet -41", &ap) && ap.channel == 36,
          "5GHz low channel 36: got %d", ap.channel);
    CHECK(mm_ap_parse_line("[9][CH:177] EdgeNet -70", &ap) && ap.channel == 177,
          "5GHz top channel 177: got %d", ap.channel);

    // --- band predicate: 1..14 are 2.4 GHz; above is 5 GHz ---
    CHECK(!mm_channel_is_5ghz(1), "ch 1 is 2.4GHz");
    CHECK(!mm_channel_is_5ghz(6), "ch 6 is 2.4GHz");
    CHECK(!mm_channel_is_5ghz(14), "ch 14 is 2.4GHz");
    CHECK(mm_channel_is_5ghz(36), "ch 36 is 5GHz");
    CHECK(mm_channel_is_5ghz(149), "ch 149 is 5GHz");
    CHECK(mm_channel_is_5ghz(177), "ch 177 is 5GHz");

    // --- dual-band inferred from observation (one-way latch) ---
    {
        // A list -a line on a 5 GHz channel flips Unknown -> inferred dual-band.
        MMCap cap = MMCapUnknown;
        CHECK(mm_ap_parse_line("[3][CH:149] Net5 -55", &ap), "5GHz list -a parses");
        CHECK(mm_dual_band_observe(&cap, ap.channel) && cap == MMCapYes,
              "5GHz list -a sighting latches MMCapYes (was %d)", cap);

        // A scanall AP line on a 5 GHz channel does the same.
        MMScanAp sa;
        MMCap cap_s = MMCapUnknown;
        CHECK(mm_scanall_parse_ap("-60 Ch: 157 BSSID: 02:00:00:00:00:04 ESSID: Net5G ", &sa),
              "5GHz scanall parses");
        CHECK(mm_dual_band_observe(&cap_s, sa.channel) && cap_s == MMCapYes,
              "5GHz scanall sighting latches MMCapYes");

        // A 2.4 GHz line proves nothing: no change, no return-true.
        MMCap cap24 = MMCapUnknown;
        CHECK(mm_ap_parse_line("[1][CH:6] Net24 -50", &ap), "2.4GHz list -a parses");
        CHECK(!mm_dual_band_observe(&cap24, ap.channel) && cap24 == MMCapUnknown,
              "2.4GHz sighting does NOT infer dual-band (got %d)", cap24);

        // One-way: never downgrade; a 2.4 line leaves an existing Yes latched,
        // and a 5 GHz line upgrades even an explicit No (positive proof wins).
        MMCap latched = MMCapYes;
        CHECK(!mm_dual_band_observe(&latched, 6) && latched == MMCapYes,
              "2.4GHz never downgrades a latched MMCapYes");
        MMCap was_no = MMCapNo;
        CHECK(mm_dual_band_observe(&was_no, 149) && was_no == MMCapYes,
              "5GHz sighting overrides a stale MMCapNo");
    }

    // noise lines must be rejected
    CHECK(!mm_ap_parse_line("#list -a", &ap), "command echo rejected");
    CHECK(!mm_ap_parse_line("0 selected", &ap), "'selected' line rejected");
    CHECK(!mm_ap_parse_line("> #stopscan", &ap), "prompt line rejected");
    CHECK(!mm_ap_parse_line("Stopping WiFi tran/recv", &ap), "status line rejected");
    CHECK(!mm_ap_parse_line("", &ap), "empty line rejected");

    // --- full-buffer check against the real capture ---
    char* text = slurp("dumps/list_sample.log");
    MMAccessPoint aps[64];
    size_t n = mm_ap_parse_buffer(text, aps, 64);
    CHECK(n == 21, "expected 21 APs from list_15.log, got %zu", n);

    if(n == 21) {
        // contiguous indices 0..20
        for(size_t i = 0; i < n; i++)
            CHECK(aps[i].index == (int)i, "row %zu index %d", i, aps[i].index);

        CHECK(strcmp(aps[0].name, "Sunflower") == 0 && aps[0].channel == 2 && aps[0].rssi == -72,
              "row0");
        CHECK(strcmp(aps[4].name, "Harbor View") == 0, "row4 name '%s'", aps[4].name);
        CHECK(aps[5].hidden && strcmp(aps[5].name, "02:00:00:00:00:05") == 0, "row5 bssid");
        CHECK(strcmp(aps[20].name, "Trailhead") == 0 && aps[20].channel == 3 && aps[20].rssi == -87,
              "row20");

        // count hidden vs named
        int hidden = 0;
        for(size_t i = 0; i < n; i++)
            if(aps[i].hidden) hidden++;
        CHECK(hidden == 7, "expected 7 hidden(bssid) rows, got %d", hidden);
    }

    free(text);

    // --- fixed-offset MAC parsers must reject 17 valid chars + trailing junk ---
    {
        MMPineScan ps;
        CHECK(
            mm_pinescan_parse_line("MAC: aa:bb:cc:dd:ee:ff CH: 6 RSSI: -50 DET: OTHER SSID: X", &ps),
            "pinescan: valid MAC parses");
        CHECK(strcmp(ps.mac, "aa:bb:cc:dd:ee:ff") == 0, "pinescan mac: got '%s'", ps.mac);
        CHECK(
            !mm_pinescan_parse_line(
                "MAC: aa:bb:cc:dd:ee:ffZZ CH: 6 RSSI: -50 DET: OTHER SSID: X", &ps),
            "pinescan: over-length MAC rejected");

        MMRogueAp ra;
        CHECK(
            mm_multissid_parse_line("MAC: aa:bb:cc:dd:ee:ff CH: 6 RSSI: -50 SSIDs: 3 SSID: X", &ra),
            "multissid: valid MAC parses");
        CHECK(
            !mm_multissid_parse_line(
                "MAC: aa:bb:cc:dd:ee:ffZZ CH: 6 RSSI: -50 SSIDs: 3 SSID: X", &ra),
            "multissid: over-length MAC rejected");

        char mac[18];
        CHECK(mm_pwn_line_mac("MAC: aa:bb:cc:dd:ee:ff", mac), "pwn: valid MAC parses");
        CHECK(!mm_pwn_line_mac("MAC: aa:bb:cc:dd:ee:ffZZ", mac), "pwn: over-length MAC rejected");

        MMDeauthFrame df;
        CHECK(
            mm_deauth_parse_line("-50 Ch: 6 aa:bb:cc:dd:ee:ff -> ff:ff:ff:ff:ff:ff", &df),
            "deauth: valid MACs parse");
        CHECK(
            !mm_deauth_parse_line("-50 Ch: 6 aa:bb:cc:dd:ee:ffZZ -> ff:ff:ff:ff:ff:ff", &df),
            "deauth: over-length src MAC rejected");
    }

    if(failures == 0) {
        printf("OK: all AP-parser tests passed\n");
        return 0;
    }
    printf("%d test(s) failed\n", failures);
    return 1;
}
