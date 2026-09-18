// Marauder's Mate: GPS Data live panel.
//
// Runs `gpsdata`, which streams a repeating multi-line block (fix, sats,
// accuracy, lat/lon/alt, datetime) roughly once a second. Raw, that block just
// scrolls the console; this parses each line into a persistent MMGpsFix and
// renders a stable, at-a-glance panel that updates in place. The terse
// `gps -g <field>` replies use different labels but the same parser accepts
// both, so a stray single-field reply still lands in the right slot.
#include "../wifi_marauder_app_i.h"

#define MM_GPS_TAG "MM-GPS"
// Debug: echo the first N raw ESP lines to the log after launch (only visible at
// Debug log level), enough to see the first block or an error without flooding.
#define MM_GPS_DBG_MAX 40
static int mm_gps_dbg_lines;

// A field that never arrived shows this rather than a blank.
static const char* mm_gps_or_dash(const char* s) {
    return (s && s[0]) ? s : "--";
}

static void wifi_marauder_gps_data_rx_cb(uint8_t* buf, size_t len, void* context) {
    WifiMarauderApp* app = context;
    furi_stream_buffer_send(app->scan_stream, buf, len, 0);
}

static void wifi_marauder_gps_data_draw(WifiMarauderApp* app) {
    Widget* widget = app->widget;
    widget_reset(widget);

    widget_add_string_element(widget, 2, 2, AlignLeft, AlignTop, FontSecondary, "GPS Data");

    // Header right: fix + satellite count, the two things you check first.
    char hdr[24];
    const MMGpsFix* g = &app->gps_fix;
    if(g->have_sats) {
        snprintf(hdr, sizeof(hdr), "%s %dsat", g->has_fix ? "Fix" : "No fix", g->sats);
    } else {
        snprintf(hdr, sizeof(hdr), "%s", g->has_fix ? "Fix" : "No fix");
    }
    widget_add_string_element(widget, 126, 2, AlignRight, AlignTop, FontSecondary, hdr);

    if(!app->gps_have) {
        // No block has parsed yet: acquiring a fix / waiting for the module.
        widget_add_string_element(
            widget, 64, 36, AlignCenter, AlignCenter, FontSecondary, "Acquiring...");
        return;
    }

    char row[40];
    snprintf(row, sizeof(row), "Lat %s", mm_gps_or_dash(g->lat));
    widget_add_string_element(widget, 2, 16, AlignLeft, AlignTop, FontSecondary, row);
    snprintf(row, sizeof(row), "Lon %s", mm_gps_or_dash(g->lon));
    widget_add_string_element(widget, 2, 28, AlignLeft, AlignTop, FontSecondary, row);

    // Altitude + horizontal accuracy on one line.
    if(g->accuracy[0]) {
        snprintf(row, sizeof(row), "Alt %sm  \xb1%s", mm_gps_or_dash(g->alt), g->accuracy);
    } else {
        snprintf(row, sizeof(row), "Alt %sm", mm_gps_or_dash(g->alt));
    }
    widget_add_string_element(widget, 2, 40, AlignLeft, AlignTop, FontSecondary, row);

    widget_add_string_element(
        widget, 2, 52, AlignLeft, AlignTop, FontSecondary, mm_gps_or_dash(g->datetime));
}

void wifi_marauder_scene_gps_data_on_enter(void* context) {
    WifiMarauderApp* app = context;

    mm_gps_fix_reset(&app->gps_fix);
    app->gps_have = false;
    app->gps_ticks = 0;
    furi_stream_buffer_reset(app->scan_stream);
    furi_string_reset(app->scan_line);

    wifi_marauder_gps_data_draw(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiMarauderAppViewWidget);

    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, wifi_marauder_gps_data_rx_cb);
    wifi_marauder_uart_set_handle_rx_pcap_cb(app->uart, NULL);

    mm_gps_dbg_lines = 0;
    const char* cmd = "gpsdata\n";
    wifi_marauder_uart_tx(app->uart, (uint8_t*)cmd, strlen(cmd));
    FURI_LOG_I(MM_GPS_TAG, "launch gpsdata");
}

bool wifi_marauder_scene_gps_data_on_event(void* context, SceneManagerEvent event) {
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

        bool changed = false;
        for(;;) {
            const char* cstr = furi_string_get_cstr(app->scan_line);
            const char* nl = strchr(cstr, '\n');
            if(!nl) break;
            size_t len = (size_t)(nl - cstr);
            char line[96];
            size_t cpy = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
            memcpy(line, cstr, cpy);
            line[cpy] = '\0';

            bool parsed = mm_gps_fix_update(line, &app->gps_fix);
            if(mm_gps_dbg_lines < MM_GPS_DBG_MAX && line[0] != '\0') {
                FURI_LOG_D(MM_GPS_TAG, "rx[%s]: %s", parsed ? "fld" : "---", line);
                mm_gps_dbg_lines++;
            }
            if(parsed) {
                app->gps_have = true;
                changed = true;
            }
            furi_string_right(app->scan_line, len + 1);
        }

        // gpsdata refreshes ~1 Hz; redraw only when a field actually changed.
        app->gps_ticks++;
        if(changed) wifi_marauder_gps_data_draw(app);
        consumed = true;
    }

    return consumed;
}

void wifi_marauder_scene_gps_data_on_exit(void* context) {
    WifiMarauderApp* app = context;
    wifi_marauder_uart_tx(app->uart, (uint8_t*)("stopscan\n"), strlen("stopscan\n"));
    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, NULL);
    widget_reset(app->widget);
}
