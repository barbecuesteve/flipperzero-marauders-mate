// Marauder's Mate: Fox Hunt RSSI meter.
//
// Runs `foxhunt -w <ap>` for the selected AP and renders the streaming RSSI as
// a live strength bar + big number + peak-hold, so you can physically home in
// on the transmitter (stronger/less-negative = closer). Raw Marauder output is
// just "<name> RSSI: <n>" scrolling; this turns it into an instrument.
#include "../wifi_marauder_app_i.h"

// Map RSSI to a 0..100 strength percentage over a practical WiFi range.
#define MM_RSSI_FLOOR (-90)
#define MM_RSSI_CEIL (-20)

static int mm_rssi_pct(int rssi) {
    if(rssi < MM_RSSI_FLOOR) rssi = MM_RSSI_FLOOR;
    if(rssi > MM_RSSI_CEIL) rssi = MM_RSSI_CEIL;
    return (rssi - MM_RSSI_FLOOR) * 100 / (MM_RSSI_CEIL - MM_RSSI_FLOOR);
}

static void wifi_marauder_fox_hunt_rx_cb(uint8_t* buf, size_t len, void* context) {
    WifiMarauderApp* app = context;
    furi_stream_buffer_send(app->scan_stream, buf, len, 0);
}

static void wifi_marauder_fox_hunt_draw(WifiMarauderApp* app) {
    Widget* widget = app->widget;
    widget_reset(widget);

    char title[40];
    snprintf(title, sizeof(title), "Fox: %s", app->fox_title);
    widget_add_string_element(widget, 2, 2, AlignLeft, AlignTop, FontSecondary, title);

    if(!app->fox_have) {
        widget_add_string_element(
            widget, 64, 34, AlignCenter, AlignCenter, FontPrimary, "Acquiring...");
        return;
    }

    char num[12];
    snprintf(num, sizeof(num), "%d", app->fox_smoothed);
    widget_add_string_element(widget, 60, 30, AlignRight, AlignCenter, FontBigNumbers, num);
    widget_add_string_element(widget, 64, 24, AlignLeft, AlignTop, FontSecondary, "dBm");

    // Strength bar: outline + fill proportional to signal strength.
    int pct = mm_rssi_pct(app->fox_smoothed);
    widget_add_frame_element(widget, 4, 44, 120, 11, 1);
    int fill = pct * 116 / 100;
    if(fill > 0) widget_add_rect_element(widget, 6, 46, fill, 7, 0, true);

    char best[24];
    snprintf(best, sizeof(best), "best: %d dBm", app->fox_best);
    widget_add_string_element(widget, 2, 56, AlignLeft, AlignTop, FontSecondary, best);
}

void wifi_marauder_scene_fox_hunt_on_enter(void* context) {
    WifiMarauderApp* app = context;

    app->fox_have = false;
    app->fox_rssi = 0;
    app->fox_smoothed = 0;
    app->fox_best = MM_RSSI_FLOOR;
    app->fox_ticks = 0;
    furi_stream_buffer_reset(app->scan_stream);
    furi_string_reset(app->scan_line);

    wifi_marauder_fox_hunt_draw(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiMarauderAppViewWidget);

    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, wifi_marauder_fox_hunt_rx_cb);
    wifi_marauder_uart_set_handle_rx_pcap_cb(app->uart, NULL);

    char cmd[32];
    if(app->fox_is_station) {
        snprintf(cmd, sizeof(cmd), "foxhunt -s %d %d\n", app->fox_ap_arg, app->fox_sta_arg);
    } else {
        snprintf(cmd, sizeof(cmd), "foxhunt -w %d\n", app->fox_ap_arg);
    }
    wifi_marauder_uart_tx(app->uart, (uint8_t*)cmd, strlen(cmd));
}

bool wifi_marauder_scene_fox_hunt_on_event(void* context, SceneManagerEvent event) {
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

        for(;;) {
            const char* cstr = furi_string_get_cstr(app->scan_line);
            const char* nl = strchr(cstr, '\n');
            if(!nl) break;
            size_t len = (size_t)(nl - cstr);
            char line[96];
            size_t cpy = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
            memcpy(line, cstr, cpy);
            line[cpy] = '\0';
            int r;
            if(mm_foxhunt_parse_rssi(line, &r)) {
                app->fox_rssi = r;
                if(!app->fox_have) {
                    app->fox_smoothed = r;
                    app->fox_have = true;
                } else {
                    // EMA (alpha = 1/4): steadies the jittery raw RSSI.
                    app->fox_smoothed = (app->fox_smoothed * 3 + r) / 4;
                }
                if(app->fox_smoothed > app->fox_best) app->fox_best = app->fox_smoothed;
            }
            furi_string_right(app->scan_line, len + 1);
        }
        // Redraw at ~5 Hz regardless of sample rate, to keep the GUI responsive.
        app->fox_ticks++;
        if(app->fox_have && (app->fox_ticks % 2 == 0)) wifi_marauder_fox_hunt_draw(app);
        consumed = true;
    }

    return consumed;
}

void wifi_marauder_scene_fox_hunt_on_exit(void* context) {
    WifiMarauderApp* app = context;
    wifi_marauder_uart_tx(app->uart, (uint8_t*)("stopscan\n"), strlen("stopscan\n"));
    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, NULL);
    widget_reset(app->widget);
}
