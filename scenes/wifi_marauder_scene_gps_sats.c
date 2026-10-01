// Marauder's Mate: NMEA satellite view.
//
// Runs `nmea` and renders the sky as an SNR bar chart -- the classic GPS
// "signal bars" instrument -- plus fix quality (2D/3D), satellites-used, HDOP
// and ground speed. GSV sentences (per constellation, in up to several messages)
// are upserted into a rolling per-satellite table keyed by constellation+PRN;
// GSA supplies fix/DOP; VTG supplies speed. Satellites that stop being re-sent
// age out so the bars track what's actually visible now.
#include "../wifi_marauder_app_i.h"

#define MM_SAT_TAG "MM-SAT"
#define MM_SAT_DBG_MAX 60
static int mm_sat_dbg_lines;
// Source of the NMEA stream this session: true = direct GPS on LPUART (15/16),
// false = the C5's `nmea` command over the Marauder USART. Chosen on enter.
static bool s_gps_direct;

// A sat not re-sent within this many ticks (~100ms each => ~3s) has dropped.
#define MM_SAT_STALE_TICKS 30
// SNR (dB-Hz) mapped to full bar height; readings above this clamp.
#define MM_SAT_SNR_MAX 50
// How many bars fit across the screen.
#define MM_SAT_BARS 16

static void wifi_marauder_gps_sats_rx_cb(uint8_t* buf, size_t len, void* context) {
    WifiMarauderApp* app = context;
    furi_stream_buffer_send(app->scan_stream, buf, len, 0);
}

// Find a satellite slot by constellation+PRN, or -1.
static int mm_sat_find(WifiMarauderApp* app, const char* talker, int prn) {
    for(int i = 0; i < app->sat_count; i++) {
        if(app->sat_slots[i].prn == prn && strncmp(app->sat_slots[i].talker, talker, 2) == 0)
            return i;
    }
    return -1;
}

// Upsert one satellite. When the table is full and the sat is new, evict the
// stalest slot so fresh sightings can't be starved out.
static void mm_sat_upsert(WifiMarauderApp* app, const char* talker, const MMSatInfo* s) {
    int idx = mm_sat_find(app, talker, s->prn);
    if(idx < 0) {
        if(app->sat_count < MM_SAT_MAX) {
            idx = app->sat_count++;
        } else {
            int oldest = 0;
            for(int i = 1; i < app->sat_count; i++) {
                if(app->sat_slots[i].last_tick < app->sat_slots[oldest].last_tick) oldest = i;
            }
            idx = oldest;
        }
    }
    MMSatSlot* dst = &app->sat_slots[idx];
    dst->talker[0] = talker[0];
    dst->talker[1] = talker[1];
    dst->talker[2] = '\0';
    dst->prn = s->prn;
    dst->elevation = s->elevation;
    dst->azimuth = s->azimuth;
    dst->snr = s->snr;
    dst->has_snr = s->has_snr;
    dst->last_tick = app->sat_ticks;
}

static void wifi_marauder_gps_sats_draw(WifiMarauderApp* app) {
    Widget* widget = app->widget;
    widget_reset(widget);

    // Header: fix type + HDOP.
    const char* fixstr = "No fix";
    if(app->sat_fix_type == 2)
        fixstr = "2D fix";
    else if(app->sat_fix_type == 3)
        fixstr = "3D fix";
    char fixline[24];
    snprintf(fixline, sizeof(fixline), "%s %s", fixstr, s_gps_direct ? "LPUART" : "C5");
    widget_add_string_element(widget, 2, 1, AlignLeft, AlignTop, FontSecondary, fixline);

    if(app->sat_fix_type >= 2 || app->sat_hdop_x10 > 0) {
        char h[32];
        snprintf(h, sizeof(h), "HDOP %d.%d", app->sat_hdop_x10 / 10, app->sat_hdop_x10 % 10);
        widget_add_string_element(widget, 126, 1, AlignRight, AlignTop, FontSecondary, h);
    }

    // Collect active (non-stale) satellites, strongest first (insertion sort).
    int order[MM_SAT_MAX];
    int active = 0;
    for(int i = 0; i < app->sat_count; i++) {
        if(app->sat_ticks - app->sat_slots[i].last_tick > MM_SAT_STALE_TICKS) continue;
        int j = active - 1;
        while(j >= 0 && app->sat_slots[order[j]].snr < app->sat_slots[i].snr) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = i;
        active++;
    }

    // Second header line: sats used / in view + speed.
    char line[32];
    snprintf(line, sizeof(line), "Sat %d/%d", app->sat_used, active);
    widget_add_string_element(widget, 2, 12, AlignLeft, AlignTop, FontSecondary, line);
    if(app->sat_have_speed) {
        char sp[32];
        snprintf(sp, sizeof(sp), "%d.%d km/h", app->sat_speed_x10 / 10, app->sat_speed_x10 % 10);
        widget_add_string_element(widget, 126, 12, AlignRight, AlignTop, FontSecondary, sp);
    }

    if(active == 0) {
        widget_add_string_element(
            widget, 64, 40, AlignCenter, AlignCenter, FontSecondary, "Acquiring...");
        return;
    }

    // SNR bar chart. Baseline at y=63; bars grow upward from there.
    const int baseline = 63;
    const int max_h = 38; // 25..63
    const int bw = 124 / MM_SAT_BARS; // bar+gap pitch
    int show = active < MM_SAT_BARS ? active : MM_SAT_BARS;
    for(int k = 0; k < show; k++) {
        const MMSatSlot* s = &app->sat_slots[order[k]];
        int snr = s->snr;
        if(snr > MM_SAT_SNR_MAX) snr = MM_SAT_SNR_MAX;
        int h = s->has_snr ? (snr * max_h / MM_SAT_SNR_MAX) : 0;
        int x = 2 + k * bw;
        if(s->has_snr && h > 0) {
            widget_add_rect_element(widget, x, baseline - h, bw - 1, h, 0, true);
        } else {
            // In view but not tracked: a low hollow stub so it still shows.
            widget_add_frame_element(widget, x, baseline - 4, bw - 1, 4, 0);
        }
    }
}

void wifi_marauder_scene_gps_sats_on_enter(void* context) {
    WifiMarauderApp* app = context;

    app->sat_count = 0;
    app->sat_fix_type = 0;
    app->sat_used = 0;
    app->sat_hdop_x10 = 0;
    app->sat_speed_x10 = 0;
    app->sat_have_speed = false;
    app->sat_ticks = 0;
    furi_stream_buffer_reset(app->scan_stream);
    furi_string_reset(app->scan_line);

    wifi_marauder_gps_sats_draw(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiMarauderAppViewWidget);
    mm_sat_dbg_lines = 0;

    // Prefer the direct GPS on LPUART: it streams NMEA continuously (no command)
    // and leaves the C5 link free. Fall back to the C5 `nmea` command if LPUART
    // can't be opened -- busy, or hardware with no GPS on 15/16.
    app->gps_uart = wifi_marauder_uart_gps_init(app);
    s_gps_direct = (app->gps_uart != NULL);
    if(s_gps_direct) {
        wifi_marauder_uart_set_handle_rx_data_cb(app->uart, NULL); // C5 stays quiet
        wifi_marauder_uart_set_handle_rx_data_cb(app->gps_uart, wifi_marauder_gps_sats_rx_cb);
        wifi_marauder_uart_set_handle_rx_pcap_cb(app->gps_uart, NULL);
        FURI_LOG_I(MM_SAT_TAG, "sat view: direct LPUART GPS");
    } else {
        wifi_marauder_uart_set_handle_rx_data_cb(app->uart, wifi_marauder_gps_sats_rx_cb);
        wifi_marauder_uart_set_handle_rx_pcap_cb(app->uart, NULL);
        wifi_marauder_uart_tx(app->uart, (uint8_t*)"nmea\n", strlen("nmea\n"));
        FURI_LOG_I(MM_SAT_TAG, "sat view: C5 nmea");
    }
}

bool wifi_marauder_scene_gps_sats_on_event(void* context, SceneManagerEvent event) {
    WifiMarauderApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        uint8_t tmp[129];
        size_t got;
        while((got = furi_stream_buffer_receive(app->scan_stream, tmp, sizeof(tmp) - 1, 0)) > 0) {
            mm_sanitize_nuls(tmp, got);
            tmp[got] = '\0';
            furi_string_cat_str(app->scan_line, (const char*)tmp);
        }

        const double ten = 10.0; // double local so *10 doesn't trip double-promotion
        for(;;) {
            const char* cstr = furi_string_get_cstr(app->scan_line);
            const char* nl = strchr(cstr, '\n');
            if(!nl) break;
            size_t len = (size_t)(nl - cstr);
            char sline[128];
            size_t cpy = len < sizeof(sline) - 1 ? len : sizeof(sline) - 1;
            memcpy(sline, cstr, cpy);
            sline[cpy] = '\0';

            const char* kind = "----";
            MMNmeaGsv gsv;
            MMNmeaGsa gsa;
            MMNmeaVtg vtg;
            if(mm_nmea_parse_gsv(sline, &gsv)) {
                kind = "gsv";
                for(int i = 0; i < gsv.count; i++) mm_sat_upsert(app, gsv.talker, &gsv.sats[i]);
            } else if(mm_nmea_parse_gsa(sline, &gsa)) {
                kind = "gsa";
                app->sat_fix_type = gsa.fix_type;
                app->sat_used = gsa.sats_used;
                app->sat_hdop_x10 = (int)(gsa.hdop * ten);
            } else if(mm_nmea_parse_vtg(sline, &vtg)) {
                kind = "vtg";
                app->sat_speed_x10 = (int)(vtg.speed_kmh * ten);
                app->sat_have_speed = true;
            }
            if(mm_sat_dbg_lines < MM_SAT_DBG_MAX && sline[0] != '\0') {
                FURI_LOG_D(MM_SAT_TAG, "rx[%s]: %s", kind, sline);
                mm_sat_dbg_lines++;
            }
            furi_string_right(app->scan_line, len + 1);
        }

        // Redraw ~2 Hz; the sky changes slowly and bars would otherwise flicker.
        app->sat_ticks++;
        if(app->sat_ticks % 5 == 0) wifi_marauder_gps_sats_draw(app);
        consumed = true;
    }

    return consumed;
}

void wifi_marauder_scene_gps_sats_on_exit(void* context) {
    WifiMarauderApp* app = context;
    if(s_gps_direct) {
        // Close the direct GPS UART and hand LPUART back to the log console.
        if(app->gps_uart) {
            wifi_marauder_uart_set_handle_rx_data_cb(app->gps_uart, NULL);
            wifi_marauder_uart_gps_free(app->gps_uart);
            app->gps_uart = NULL;
        }
    } else {
        wifi_marauder_uart_tx(app->uart, (uint8_t*)("stopscan\n"), strlen("stopscan\n"));
        wifi_marauder_uart_set_handle_rx_data_cb(app->uart, NULL);
    }
    widget_reset(app->widget);
}
