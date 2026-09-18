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

    // ===================== NMEA sky/quality: GSV / GSA / VTG =====================
    {
        // GSV: 3-message set, 11 sats in view, this msg carries 4 (last with SNR).
        MMNmeaGsv v;
        CHECK(mm_nmea_parse_gsv("$GPGSV,3,1,11,05,58,223,25,06,25,065,21,09,13,046,18,11,55,035,23,0*6D", &v),
              "GSV parses");
        CHECK(strcmp(v.talker, "GP") == 0, "GSV talker: '%s'", v.talker);
        CHECK(strcmp(mm_nmea_constellation(v.talker), "GPS") == 0, "GSV constellation");
        CHECK(v.total_msgs == 3 && v.msg_num == 1 && v.in_view == 11, "GSV header");
        CHECK(v.count == 4, "GSV count: %d", v.count);
        CHECK(v.sats[0].prn == 5 && v.sats[0].elevation == 58 && v.sats[0].azimuth == 223 &&
                  v.sats[0].snr == 25 && v.sats[0].has_snr, "GSV sat0");
        CHECK(v.sats[3].prn == 11 && v.sats[3].snr == 23, "GSV sat3");

        // GSV last message with a blank SNR slot (sat in view, not tracked).
        MMNmeaGsv v2;
        CHECK(mm_nmea_parse_gsv("$GPGSV,3,3,11,21,81,063,29,25,36,280,12,29,18,313,,0*59", &v2),
              "GSV msg3 parses");
        CHECK(v2.count == 3, "GSV msg3 count: %d", v2.count);
        CHECK(v2.sats[2].prn == 29 && !v2.sats[2].has_snr, "GSV blank SNR -> has_snr false");

        // BeiDou single-message GSV.
        MMNmeaGsv v3;
        CHECK(mm_nmea_parse_gsv("$BDGSV,1,1,02,28,55,071,17,33,39,057,31,0*76", &v3), "BDGSV parses");
        CHECK(strcmp(mm_nmea_constellation(v3.talker), "BDS") == 0, "BDGSV constellation");
        CHECK(v3.in_view == 2 && v3.count == 2, "BDGSV counts");

        // GSA: 3D fix, 7 sats used, DOP trio.
        MMNmeaGsa a;
        CHECK(mm_nmea_parse_gsa("$GNGSA,A,3,05,06,09,11,12,21,25,,,,,,2.7,1.9,1.9,1*39", &a),
              "GSA parses");
        CHECK(a.fix_type == 3, "GSA fix_type: %d", a.fix_type);
        CHECK(a.sats_used == 7, "GSA sats_used: %d", a.sats_used);
        CHECK(near(a.pdop, 2.7) && near(a.hdop, 1.9) && near(a.vdop, 1.9), "GSA DOP");

        // VTG: course + speed (knots and km/h), stationary sample.
        MMNmeaVtg t;
        CHECK(mm_nmea_parse_vtg("$GNVTG,304.27,T,,M,0.00,N,0.00,K,A*21", &t), "VTG parses");
        CHECK(near(t.course_deg, 304.27), "VTG course: %.2f", t.course_deg);
        CHECK(near(t.speed_knots, 0.0) && near(t.speed_kmh, 0.0), "VTG speed");

        // Cross-type rejection.
        MMNmeaGsv vr;
        CHECK(!mm_nmea_parse_gsv("$GNVTG,304.27,T,,M,0.00,N,0.00,K,A*21", &vr), "GSV rejects VTG");
        MMNmeaGsa ar;
        CHECK(!mm_nmea_parse_gsa("$GPGSV,3,1,11,05,58,223,25,06,25,065,21,09,13,046,18,11,55,035,23,0*6D", &ar),
              "GSA rejects GSV");
    }

    // ===================== WiFi wardrive CSV line =====================
    {
        MMWardriveAp w;
        // Named AP with the "<cursor> | " prefix (synthetic BSSID/SSID).
        CHECK(mm_wardrive_parse_line(
                  "1 | 02:00:00:00:00:01,TestNet,[WPA2_PSK],2026-09-18 10:56:45,11,-44,"
                  "33.7418976,-84.3218460,285.30,2.25,WIFI",
                  &w),
              "wardrive named parses");
        CHECK(strcmp(w.bssid, "02:00:00:00:00:01") == 0, "wardrive bssid: '%s'", w.bssid);
        CHECK(strcmp(w.ssid, "TestNet") == 0 && !w.hidden, "wardrive ssid: '%s'", w.ssid);
        CHECK(strcmp(w.auth, "[WPA2_PSK]") == 0, "wardrive auth: '%s'", w.auth);
        CHECK(w.channel == 11 && w.rssi == -44, "wardrive ch/rssi: %d/%d", w.channel, w.rssi);
        CHECK(strcmp(w.datetime, "2026-09-18 10:56:45") == 0, "wardrive dt");
        CHECK(strcmp(w.lat, "33.7418976") == 0 && strcmp(w.lon, "-84.3218460") == 0, "wardrive coords");
        CHECK(strcmp(w.alt, "285.30") == 0 && strcmp(w.accuracy, "2.25") == 0, "wardrive alt/acc");

        // Hidden AP (empty ssid field) on a 5 GHz channel, WEP auth.
        CHECK(mm_wardrive_parse_line(
                  "5 | 02:00:00:00:00:05,,[WEP],2026-09-18 10:56:45,157,-68,33.74,-84.32,285.3,2.25,WIFI",
                  &w),
              "wardrive hidden parses");
        CHECK(w.hidden && w.ssid[0] == '\0', "wardrive hidden -> empty ssid");
        CHECK(w.channel == 157 && strcmp(w.auth, "[WEP]") == 0, "wardrive 5GHz/WEP");

        // SSID with a space (no comma -- firmware replaces commas), WPA2/WPA3.
        CHECK(mm_wardrive_parse_line(
                  "8 | 02:00:00:00:00:08,My Printer 5G,[WPA2_WPA3_PSK],2026-09-18 10:56:45,44,-59,"
                  "33.74,-84.32,285.3,2.25,WIFI",
                  &w) &&
                  strcmp(w.ssid, "My Printer 5G") == 0,
              "wardrive spaced ssid: '%s'", w.ssid);

        // Works without the "<cursor> | " prefix too.
        CHECK(mm_wardrive_parse_line(
                  "02:00:00:00:00:09,Bare,[WPA2_PSK],2026-09-18 10:56:45,6,-70,33.74,-84.32,285.3,2.25,WIFI",
                  &w) &&
                  strcmp(w.ssid, "Bare") == 0,
              "wardrive no-prefix parses");

        // Rejections: banner, the "APs: N" summary, and a BLE-phase line.
        CHECK(!mm_wardrive_parse_line("Starting Wardrive. Stop with stopscan", &w),
              "wardrive banner rejected");
        CHECK(!mm_wardrive_parse_line("APs: 21", &w), "wardrive summary rejected");
        CHECK(!mm_wardrive_parse_line(
                  "Device: 02:00:00:00:00:0a02:00:00:00:00:0a,,[BLE],2026-09-18 10:56:45,0,-66,"
                  "33.74,-84.32,285.3,2.25,BLE",
                  &w),
              "wardrive rejects BLE line");
    }

    if(failures == 0) {
        printf("OK: all gps+bt-parser tests passed\n");
        return 0;
    }
    printf("%d test(s) failed\n", failures);
    return 1;
}
