// Host-side unit tests for the GPS + Bluetooth parsers (v1.10.2 ESP32-C5
// formats, confirmed on device). All device identifiers below are SYNTHETIC.
// Built + run via tools/run_tests.sh.
#include "../marauders_mate_ap_parser.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

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

static int near(double a, double b) { return fabs(a - b) < 1e-4; }

int main(void) {
    // ===================== GPS: gpsdata block =====================
    {
        const char* block =
            "Refreshing GPS Data on screen...\r\n"
            "==== GPS Data ====\r\n"
            "  Good Fix: Yes\r\n"
            "      Text: ANTENNA OK\r\n"
            "Satellites: 9\r\n"
            "  Accuracy: 4.75\r\n"
            "  Latitude: 33.7415810\r\n"
            " Longitude: -84.3219833\r\n"
            "  Altitude: 217.80\r\n"
            "  Datetime: 2026-09-18 09:09:59\r\n";
        MMGpsFix fix;
        int upd = mm_gps_parse_buffer(block, &fix);
        CHECK(upd == 8, "gpsdata: expected 8 updates, got %d", upd);
        CHECK(fix.has_fix, "gpsdata: good fix");
        CHECK(fix.have_sats && fix.sats == 9, "gpsdata: sats=%d", fix.sats);
        CHECK(strcmp(fix.accuracy, "4.75") == 0, "gpsdata accuracy: '%s'", fix.accuracy);
        CHECK(strcmp(fix.lat, "33.7415810") == 0, "gpsdata lat: '%s'", fix.lat);
        CHECK(strcmp(fix.lon, "-84.3219833") == 0, "gpsdata lon: '%s'", fix.lon);
        CHECK(strcmp(fix.alt, "217.80") == 0, "gpsdata alt: '%s'", fix.alt);
        CHECK(strcmp(fix.datetime, "2026-09-18 09:09:59") == 0, "gpsdata dt: '%s'", fix.datetime);
        CHECK(strcmp(fix.text, "ANTENNA OK") == 0, "gpsdata text: '%s'", fix.text);
    }

    // ===================== GPS: terse `gps -g` replies =====================
    {
        MMGpsFix fix;
        mm_gps_fix_reset(&fix);
        CHECK(mm_gps_fix_update("Fix: Yes", &fix) && fix.has_fix, "gps -g fix");
        CHECK(mm_gps_fix_update("> Sats: 9", &fix) && fix.sats == 9, "gps -g sat (prompt)");
        CHECK(mm_gps_fix_update("Lat: 33.7415695", &fix) &&
                  strcmp(fix.lat, "33.7415695") == 0, "gps -g lat: '%s'", fix.lat);
        CHECK(mm_gps_fix_update("Lon: -84.3219833", &fix) &&
                  strcmp(fix.lon, "-84.3219833") == 0, "gps -g lon");
        CHECK(mm_gps_fix_update("Alt: 220.80", &fix) &&
                  strcmp(fix.alt, "220.80") == 0, "gps -g alt");
        CHECK(mm_gps_fix_update("Date/Time: 2026-09-18 09:10:13", &fix) &&
                  strcmp(fix.datetime, "2026-09-18 09:10:13") == 0, "gps -g date");
        CHECK(mm_gps_fix_update("Accuracy: 4.75", &fix) &&
                  strcmp(fix.accuracy, "4.75") == 0, "gps -g accuracy");
        CHECK(mm_gps_fix_update("ANTENNA OK", &fix) &&
                  strcmp(fix.text, "ANTENNA OK") == 0, "gps -g text bare");
        // "Fix: No" flips the flag.
        CHECK(mm_gps_fix_update("Fix: No", &fix) && !fix.has_fix, "gps -g fix No");
        // A non-GPS line updates nothing.
        CHECK(!mm_gps_fix_update("Stopping WiFi tran/recv", &fix), "gps: noise rejected");
    }

    // ===================== NMEA =====================
    {
        // Real-shaped sentences with valid checksums (from the device).
        const char* gga =
            "$GNGGA,091039,3344.49000,N,08419.31778,W,1,09,4.75,219.1,M,,M,,*65";
        CHECK(mm_nmea_checksum_ok(gga), "GGA checksum ok");
        MMNmeaGga g;
        CHECK(mm_nmea_parse_gga(gga, &g), "GGA parses");
        CHECK(near(g.lat_deg, 33.7415), "GGA lat: %.6f", g.lat_deg);
        CHECK(near(g.lon_deg, -84.3219630), "GGA lon: %.6f", g.lon_deg);
        CHECK(g.fix_quality == 1 && g.valid, "GGA fix quality");
        CHECK(g.sats == 9, "GGA sats: %d", g.sats);
        CHECK(near(g.alt_m, 219.1), "GGA alt: %.2f", g.alt_m);

        const char* rmc =
            "$GNRMC,091039,A,3344.49000,N,08419.31778,W,0.0,304.3,180926,,,A*7D";
        CHECK(mm_nmea_checksum_ok(rmc), "RMC checksum ok");
        MMNmeaRmc r;
        CHECK(mm_nmea_parse_rmc(rmc, &r), "RMC parses");
        CHECK(r.active, "RMC active");
        CHECK(near(r.lat_deg, 33.7415), "RMC lat: %.6f", r.lat_deg);
        CHECK(near(r.lon_deg, -84.3219630), "RMC lon: %.6f", r.lon_deg);
        CHECK(strcmp(r.date, "180926") == 0, "RMC date: '%s'", r.date);
        CHECK(strcmp(r.time, "091039") == 0, "RMC time: '%s'", r.time);

        // Corrupted checksum is rejected by the validator but body still parses.
        CHECK(!mm_nmea_checksum_ok("$GNGGA,091039,3344.49000,N,08419.31778,W,1,09,4.75,219.1,M,,M,,*00"),
              "bad checksum rejected");
        // Wrong sentence type.
        MMNmeaGga g2;
        CHECK(!mm_nmea_parse_gga(rmc, &g2), "GGA rejects RMC");
        // Southern/eastern hemisphere signs.
        MMNmeaGga g3;
        CHECK(mm_nmea_parse_gga("$GPGGA,120000,3344.49000,S,08419.31778,E,1,05,1.0,10.0,M,,M,,*XX", &g3),
              "GGA S/E parses (ignoring checksum)");
        CHECK(g3.lat_deg < 0 && g3.lon_deg > 0, "GGA S/E signs: %.4f %.4f", g3.lat_deg, g3.lon_deg);
    }

    // ===================== BT: sniffbt concatenated stream =====================
    {
        // v1.10.2 headless C5 glues every record onto one line, no separator.
        const char* stream =
            "> -70 Device: 02:00:00:00:00:01-90 Device: Flipper Ougilot-72 "
            "Device: 02:00:00:00:00:03-64 Device: Ember Ceramic Mug-97 "
            "Device: LE-black box-86 Device: 02:00:00:00:00:06";
        MMBtDevice devs[16];
        size_t n = mm_btall_parse_buffer(stream, devs, 16);
        CHECK(n == 6, "sniffbt: expected 6 devices, got %zu", n);
        CHECK(devs[0].rssi == -70 && devs[0].is_mac &&
                  strcmp(devs[0].name, "02:00:00:00:00:01") == 0, "sniffbt dev0");
        CHECK(devs[1].rssi == -90 && !devs[1].is_mac &&
                  strcmp(devs[1].name, "Flipper Ougilot") == 0, "sniffbt dev1: '%s' %d",
              devs[1].name, devs[1].rssi);
        CHECK(devs[3].rssi == -64 && strcmp(devs[3].name, "Ember Ceramic Mug") == 0,
              "sniffbt dev3: '%s'", devs[3].name);
        CHECK(devs[4].rssi == -97 && !devs[4].is_mac &&
                  strcmp(devs[4].name, "LE-black box") == 0, "sniffbt dev4 (name has '-'): '%s'",
              devs[4].name);
        // Final partial record uses the preceding rssi and runs to end-of-buffer.
        CHECK(devs[5].rssi == -86 && devs[5].is_mac &&
                  strcmp(devs[5].name, "02:00:00:00:00:06") == 0, "sniffbt dev5 (tail): '%s' %d",
              devs[5].name, devs[5].rssi);
        // No markers -> nothing.
        CHECK(mm_btall_parse_buffer("Starting Bluetooth scan. Stop with stopscan", devs, 16) == 0,
              "sniffbt banner -> 0");
    }

    // ===================== BT: flipper + airtag two-line records =====================
    {
        int rssi, len;
        char mac[18], name[33];
        CHECK(mm_bt_rssi_mac_line("-42 MAC: 80:E1:27:F3:29:42", &rssi, mac) &&
                  rssi == -42 && strcmp(mac, "80:E1:27:F3:29:42") == 0, "flipper MAC line");
        CHECK(mm_bt_name_line("Name: Flipper Ougilot", name, sizeof(name)) &&
                  strcmp(name, "Flipper Ougilot") == 0, "flipper Name line: '%s'", name);
        CHECK(mm_bt_rssi_mac_line("> -64 MAC: 02:00:00:00:00:07", &rssi, mac) && rssi == -64,
              "airtag MAC line (prompt)");
        CHECK(mm_bt_len_line("Len: 31", &len) && len == 31, "airtag Len line: %d", len);
        // Rejections.
        CHECK(!mm_bt_rssi_mac_line("Started BLE Scan", &rssi, mac), "reject non-MAC line");
        CHECK(!mm_bt_len_line("Name: x", &len), "Len rejects Name");
    }

    // ===================== BT: btwardrive CSV line =====================
    {
        MMBtWardrive w;
        // Display prefix (a real name) glued to the WiGLE CSV; synthetic MAC.
        const char* line =
            "Device: LE-black box02:00:00:00:00:08,,[BLE],2026-09-18 09:14:20,0,"
            "-86,33.7411995,-84.3216934,182.70,2.75,BLE";
        CHECK(mm_btwardrive_parse_line(line, &w), "btwardrive parses");
        CHECK(strcmp(w.mac, "02:00:00:00:00:08") == 0, "btwardrive mac: '%s'", w.mac);
        CHECK(w.rssi == -86, "btwardrive rssi: %d", w.rssi);
        CHECK(strcmp(w.datetime, "2026-09-18 09:14:20") == 0, "btwardrive dt: '%s'", w.datetime);
        CHECK(strcmp(w.lat, "33.7411995") == 0, "btwardrive lat");
        CHECK(strcmp(w.lon, "-84.3216934") == 0, "btwardrive lon");
        CHECK(strcmp(w.alt, "182.70") == 0, "btwardrive alt");
        CHECK(strcmp(w.accuracy, "2.75") == 0, "btwardrive acc");
        // MAC display prefix variant (name == mac).
        const char* line2 =
            "Device: 02:00:00:00:00:0902:00:00:00:00:09,,[BLE],2026-09-18 09:14:18,0,"
            "-64,33.7411995,-84.3216934,182.50,2.75,BLE";
        CHECK(mm_btwardrive_parse_line(line2, &w) &&
                  strcmp(w.mac, "02:00:00:00:00:09") == 0, "btwardrive mac-prefix: '%s'", w.mac);
        CHECK(!mm_btwardrive_parse_line("Starting BT Wardrive. Stop with stopscan", &w),
              "btwardrive banner rejected");
    }

    if(failures == 0) {
        printf("OK: all gps+bt-parser tests passed\n");
        return 0;
    }
    printf("%d test(s) failed\n", failures);
    return 1;
}
