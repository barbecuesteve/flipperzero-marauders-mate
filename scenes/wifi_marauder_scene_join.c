// Marauder's Mate: join result scene.
//
// Sends the prepared `join -a <n> -p <pw>` command (in selected_tx_string) and
// renders a clean status instead of Marauder's raw output. Critically, it
// SUPPRESSES the settings dump Marauder prints after a join -- which echoes the
// command and the ClientPW value, i.e. the plaintext password -- so the
// password is never shown on screen. mm_join_line_is_noise() is the filter;
// only lines it passes are eligible to display.
#include "../wifi_marauder_app_i.h"

#define MM_JOIN_SOFT_TIMEOUT 200 // ~20s before we stop implying progress

static void wifi_marauder_join_rx_cb(uint8_t* buf, size_t len, void* context) {
    WifiMarauderApp* app = context;
    furi_stream_buffer_send(app->scan_stream, buf, len, 0);
}

static void wifi_marauder_join_draw(WifiMarauderApp* app) {
    Widget* w = app->widget;
    widget_reset(w);
    widget_add_string_element(w, 2, 2, AlignLeft, AlignTop, FontSecondary, "Joining");
    widget_add_string_element(
        w, 2, 13, AlignLeft, AlignTop, FontPrimary,
        app->join_target[0] ? app->join_target : "network");
    widget_add_string_element(w, 2, 34, AlignLeft, AlignTop, FontSecondary, app->join_status);
    if(app->join_ip[0]) {
        char ipline[24];
        snprintf(ipline, sizeof(ipline), "IP: %s", app->join_ip);
        widget_add_string_element(w, 2, 47, AlignLeft, AlignTop, FontSecondary, ipline);
    } else if(app->join_last[0]) {
        widget_add_string_element(w, 2, 47, AlignLeft, AlignTop, FontSecondary, app->join_last);
    }
}

void wifi_marauder_scene_join_on_enter(void* context) {
    WifiMarauderApp* app = context;

    strncpy(app->join_status, "Connecting...", sizeof(app->join_status) - 1);
    app->join_status[sizeof(app->join_status) - 1] = '\0';
    app->join_last[0] = '\0';
    app->join_ip[0] = '\0';
    app->join_done = false;
    app->join_ticks = 0;
    furi_stream_buffer_reset(app->scan_stream);
    furi_string_reset(app->scan_line);

    wifi_marauder_join_draw(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiMarauderAppViewWidget);

    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, wifi_marauder_join_rx_cb);
    wifi_marauder_uart_set_handle_rx_pcap_cb(app->uart, NULL);

    if(app->selected_tx_string) {
        wifi_marauder_uart_tx(
            app->uart, (uint8_t*)app->selected_tx_string, strlen(app->selected_tx_string));
        wifi_marauder_uart_tx(app->uart, (uint8_t*)"\n", 1);
    }
}

bool wifi_marauder_scene_join_on_event(void* context, SceneManagerEvent event) {
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
            furi_string_right(app->scan_line, len + 1);

            // Reaching Marauder's post-join settings dump means the join
            // routine finished. On success it goes straight to settings; on
            // failure it prints "Could not connect..." first (caught below and
            // marks join_done), so an un-failed settings dump == connected.
            {
                const char* pp = line;
                while(*pp == ' ' || *pp == '\t' || *pp == '>') pp++;
                if(!app->join_done &&
                   (strncmp(pp, "Name:", 5) == 0 || strncmp(pp, "Settings", 8) == 0)) {
                    strncpy(app->join_status, "Connected", sizeof(app->join_status) - 1);
                    app->join_status[sizeof(app->join_status) - 1] = '\0';
                    app->join_done = true;
                    changed = true;
                    // Remember what we joined so the detail screen can flag it.
                    app->wifi_connected = true;
                    strncpy(
                        app->connected_bssid, app->join_bssid,
                        sizeof(app->connected_bssid) - 1);
                    app->connected_bssid[sizeof(app->connected_bssid) - 1] = '\0';
                    strncpy(
                        app->connected_ssid, app->join_target,
                        sizeof(app->connected_ssid) - 1);
                    app->connected_ssid[sizeof(app->connected_ssid) - 1] = '\0';
                }
            }

            if(mm_join_line_is_noise(line)) continue; // never show the password/settings

            // Keep the newest meaningful line for the status area.
            strncpy(app->join_last, line, sizeof(app->join_last) - 1);
            app->join_last[sizeof(app->join_last) - 1] = '\0';
            changed = true;

            char ip[16];
            if(!app->join_ip[0] && mm_join_parse_ip(line, ip)) {
                strncpy(app->join_ip, ip, sizeof(app->join_ip) - 1);
                app->join_ip[sizeof(app->join_ip) - 1] = '\0';
            }
            switch(mm_join_classify(line)) {
            case MMJoinLineConnected:
                strncpy(app->join_status, "Connected", sizeof(app->join_status) - 1);
                app->join_done = true;
                break;
            case MMJoinLineFailed:
                strncpy(app->join_status, "Failed", sizeof(app->join_status) - 1);
                app->join_done = true;
                // A failed join leaves the ESP disconnected (WiFi.begin drops any
                // prior link before failing), so clear believed connection.
                app->wifi_connected = false;
                app->connected_bssid[0] = '\0';
                app->connected_ssid[0] = '\0';
                break;
            case MMJoinLineConnecting:
                strncpy(app->join_status, "Connecting...", sizeof(app->join_status) - 1);
                break;
            case MMJoinLineNone:
            default:
                break;
            }
            app->join_status[sizeof(app->join_status) - 1] = '\0';
        }

        app->join_ticks++;
        if(!app->join_done && app->join_ticks == MM_JOIN_SOFT_TIMEOUT) {
            // No conclusive result: stop implying live progress, but leave the
            // last line on screen so the user can judge.
            strncpy(app->join_status, "No response yet", sizeof(app->join_status) - 1);
            app->join_status[sizeof(app->join_status) - 1] = '\0';
            changed = true;
        }
        if(changed && (app->join_ticks % 3 == 0 || app->join_done)) wifi_marauder_join_draw(app);
        consumed = true;
    }

    return consumed;
}

void wifi_marauder_scene_join_on_exit(void* context) {
    WifiMarauderApp* app = context;
    // Do NOT stopscan here: that would not disconnect, but we also don't want to
    // disturb the freshly-joined link. Just detach and clean up the view.
    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, NULL);
    widget_reset(app->widget);
}
