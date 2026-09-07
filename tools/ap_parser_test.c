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

    if(failures == 0) {
        printf("OK: all AP-parser tests passed\n");
        return 0;
    }
    printf("%d test(s) failed\n", failures);
    return 1;
}
