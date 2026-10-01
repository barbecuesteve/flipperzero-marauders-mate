// Marauder's Mate: parsed device info scene.
//
// Runs `info` and shows the ESP's "Key: Value" fields (Firmware, Version,
// Hardware, MACs, SD card, ...) as a clean scrollable list instead of raw
// console. Info is a one-shot query, so we collect for ~1.5s then render.
#include "../wifi_marauder_app_i.h"

#define MM_INFO_COLLECT_TICKS 15 // ~1.5s to collect the info reply
#define MM_INFO_ROW 700
#define MM_INFO_DISCONNECT 699 // actionable row: stopscan -f
#define MM_INFO_REDETECT 698 // actionable row: re-run the info/capability probe
#define MM_INFO_GPS_PROBE 697 // actionable row: probe direct GPS on LPUART (G0)

#define MM_GPS_PROBE_TICKS 20 // ~2s reading LPUART before showing the result
#define MM_GPS_SAMPLE_CAP 480 // bytes of NMEA kept for the sample line (bounded)

// Forceful stop: ends any scan AND drops the joined network (WiFi.disconnect).
static const char* const MM_INFO_CMD_DISCONNECT = "stopscan -f";

static int s_info_ticks;
static bool s_info_built;
// Direct-GPS probe state (on-demand, off the launch path).
static bool s_gps_probing;
static int s_gps_ticks;
static uint32_t s_gps_bytes;

static void wifi_marauder_device_info_rx_cb(uint8_t* buf, size_t len, void* context) {
    WifiMarauderApp* app = context;
    furi_stream_buffer_send(app->scan_stream, buf, len, 0);
}

static void wifi_marauder_device_info_noop_cb(void* context, uint32_t index) {
    UNUSED(context);
    UNUSED(index); // info rows are display-only
}

static void wifi_marauder_device_info_row_cb(void* context, uint32_t index) {
    WifiMarauderApp* app = context;
    if(index == MM_INFO_DISCONNECT) {
        view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventScanDisconnect);
    } else if(index == MM_INFO_REDETECT) {
        view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventDeviceRedetect);
    } else if(index == MM_INFO_GPS_PROBE) {
        view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventGpsProbe);
    }
}

// Close the on-demand GPS UART (if open) and hand LPUART back to the log
// console. Safe to call when nothing is open. Used on probe completion and exit.
static void wifi_marauder_device_gps_close(WifiMarauderApp* app) {
    s_gps_probing = false;
    if(app->gps_uart) {
        wifi_marauder_uart_set_handle_rx_data_cb(app->gps_uart, NULL);
        wifi_marauder_uart_gps_free(app->gps_uart);
        app->gps_uart = NULL;
    }
}

// Render the probe result: RX byte count + the first NMEA sentence seen.
static void wifi_marauder_device_gps_build_result(WifiMarauderApp* app) {
    Submenu* s = app->submenu;
    submenu_reset(s);
    submenu_set_header(s, "Direct GPS (LPUART)");
    char row[64];
    snprintf(row, sizeof(row), "RX bytes: %lu", (unsigned long)s_gps_bytes);
    submenu_add_item(s, row, MM_INFO_ROW, wifi_marauder_device_info_noop_cb, app);

    const char* text = furi_string_get_cstr(app->ap_scan_buffer);
    const char* dollar = strchr(text, '$'); // first NMEA sentence start
    if(dollar) {
        char line[64];
        int i = 0;
        while(dollar[i] && dollar[i] != '\r' && dollar[i] != '\n' && i < (int)sizeof(line) - 1) {
            line[i] = dollar[i];
            i++;
        }
        line[i] = '\0';
        submenu_add_item(s, line, MM_INFO_ROW + 1, wifi_marauder_device_info_noop_cb, app);
        submenu_add_item(
            s, "Result: NMEA on 15/16", MM_INFO_ROW + 2, wifi_marauder_device_info_noop_cb, app);
    } else {
        submenu_add_item(
            s,
            s_gps_bytes > 0 ? "(data, not NMEA)" : "No data on LPUART",
            MM_INFO_ROW + 1,
            wifi_marauder_device_info_noop_cb,
            app);
    }
    submenu_add_item(s, "Probe again", MM_INFO_GPS_PROBE, wifi_marauder_device_info_row_cb, app);
    submenu_add_item(s, "Re-detect", MM_INFO_REDETECT, wifi_marauder_device_info_row_cb, app);
}

// Start an on-demand direct-GPS probe: open LPUART, stream NMEA into scan_stream
// for a couple seconds (collected by the tick handler), then show the result.
static void wifi_marauder_device_gps_start_probe(WifiMarauderApp* app) {
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Reading GPS...");
    // Open the optional GPS UART now (never on the launch path). NULL = LPUART
    // busy or absent -- report it instead of faulting.
    app->gps_uart = wifi_marauder_uart_gps_init(app);
    if(!app->gps_uart) {
        submenu_add_item(
            app->submenu, "LPUART busy/absent", MM_INFO_ROW, wifi_marauder_device_info_noop_cb, app);
        submenu_add_item(
            app->submenu, "Probe again", MM_INFO_GPS_PROBE, wifi_marauder_device_info_row_cb, app);
        submenu_add_item(
            app->submenu, "Re-detect", MM_INFO_REDETECT, wifi_marauder_device_info_row_cb, app);
        return;
    }
    furi_stream_buffer_reset(app->scan_stream);
    furi_string_reset(app->ap_scan_buffer);
    s_gps_bytes = 0;
    s_gps_ticks = 0;
    s_gps_probing = true;
    // Reuse the info rx cb: both just forward bytes into scan_stream.
    wifi_marauder_uart_set_handle_rx_data_cb(app->gps_uart, wifi_marauder_device_info_rx_cb);
    wifi_marauder_uart_set_handle_rx_pcap_cb(app->gps_uart, NULL);
}

// Fire off the `info` query: reset the collectors and (re)send the command.
static void wifi_marauder_device_info_start_query(WifiMarauderApp* app) {
    s_info_ticks = 0;
    s_info_built = false;
    furi_stream_buffer_reset(app->scan_stream);
    furi_string_reset(app->scan_line);
    furi_string_reset(app->ap_scan_buffer);
    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Querying...");
    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, wifi_marauder_device_info_rx_cb);
    wifi_marauder_uart_set_handle_rx_pcap_cb(app->uart, NULL);
    wifi_marauder_uart_tx(app->uart, (uint8_t*)"info\n", strlen("info\n"));
}

// A displayable field line: "Key: Value". Skips the "#info" echo, the "> "
// prompt, and blank lines.
static bool wifi_marauder_device_info_is_field(const char* line) {
    while(*line == ' ' || *line == '\t' || *line == '>') line++;
    if(*line == '\0' || *line == '#') return false;
    return strstr(line, ": ") != NULL || strstr(line, ":") != NULL;
}

static void wifi_marauder_device_info_build(WifiMarauderApp* app) {
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    submenu_set_header(submenu, "Device Info");

    // The info reply also drives capability greying app-wide, so re-detecting
    // here refreshes the category menu (e.g. after hotplugging the board or SD).
    mm_apply_info_caps(app, furi_string_get_cstr(app->ap_scan_buffer));

    // No board answering: the only useful action is to try again.
    if(app->device_state != MMDevPresent) {
        submenu_add_item(
            submenu, "No Marauder detected", MM_INFO_ROW, wifi_marauder_device_info_noop_cb, app);
        submenu_add_item(
            submenu, "Re-detect", MM_INFO_REDETECT, wifi_marauder_device_info_row_cb, app);
        return;
    }

    // Our believed connection state (from the join flow) up top -- `info` itself
    // does not report whether the STA is joined to a network.
    char conn[64];
    if(app->wifi_connected && app->connected_ssid[0]) {
        snprintf(conn, sizeof(conn), "Connected: %s", app->connected_ssid);
    } else {
        snprintf(conn, sizeof(conn), "Not connected");
    }
    submenu_add_item(submenu, conn, MM_INFO_ROW, wifi_marauder_device_info_noop_cb, app);
    if(app->wifi_connected) {
        submenu_add_item(
            submenu, "Disconnect", MM_INFO_DISCONNECT, wifi_marauder_device_info_row_cb, app);
    }

    int shown = 0;
    const char* text = furi_string_get_cstr(app->ap_scan_buffer);
    char line[96];
    while(*text) {
        const char* nl = strchr(text, '\n');
        size_t len = nl ? (size_t)(nl - text) : strlen(text);
        size_t cpy = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
        memcpy(line, text, cpy);
        line[cpy] = '\0';
        // trim trailing CR/space
        size_t n = strlen(line);
        while(n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = '\0';
        // trim a leading "> " prompt for display
        char* disp = line;
        while(*disp == ' ' || *disp == '>') disp++;
        if(wifi_marauder_device_info_is_field(line)) {
            submenu_add_item(
                submenu, disp, MM_INFO_ROW + shown, wifi_marauder_device_info_noop_cb, app);
            shown++;
        }
        if(!nl) break;
        text = nl + 1;
    }
    // Direct GPS on LPUART (pins 15/16), independent of this C5 link (G0).
    submenu_add_item(
        submenu, "Probe direct GPS", MM_INFO_GPS_PROBE, wifi_marauder_device_info_row_cb, app);
    // Always offer a re-probe (handy after inserting an SD card, etc.).
    submenu_add_item(
        submenu, "Re-detect", MM_INFO_REDETECT, wifi_marauder_device_info_row_cb, app);
}

void wifi_marauder_scene_device_info_on_enter(void* context) {
    WifiMarauderApp* app = context;
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiMarauderAppViewSubmenu);
    wifi_marauder_device_info_start_query(app);
}

bool wifi_marauder_scene_device_info_on_event(void* context, SceneManagerEvent event) {
    WifiMarauderApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == WifiMarauderEventDeviceRedetect) {
            wifi_marauder_device_gps_close(app); // drop any GPS probe before re-info
            wifi_marauder_device_info_start_query(app); // re-probe; rebuilds on completion
            consumed = true;
        } else if(event.event == WifiMarauderEventGpsProbe) {
            wifi_marauder_device_gps_start_probe(app);
            consumed = true;
        } else if(event.event == WifiMarauderEventScanDisconnect) {
            app->wifi_connected = false;
            app->connected_bssid[0] = '\0';
            app->connected_ssid[0] = '\0';
            app->selected_tx_string = MM_INFO_CMD_DISCONNECT;
            app->is_command = true;
            app->is_custom_tx_string = false;
            app->focus_console_start = false;
            app->show_stopscan_tip = false;
            scene_manager_next_scene(app->scene_manager, WifiMarauderSceneConsoleOutput);
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        if(s_gps_probing) {
            // Direct-GPS probe: count all RX bytes, keep a bounded sample so a
            // 2s NMEA stream at 115200 can't grow the buffer without limit.
            uint8_t tmp[129];
            size_t got;
            while((got = furi_stream_buffer_receive(app->scan_stream, tmp, sizeof(tmp) - 1, 0)) >
                  0) {
                s_gps_bytes += (uint32_t)got;
                if(furi_string_size(app->ap_scan_buffer) < MM_GPS_SAMPLE_CAP) {
                    mm_sanitize_nuls(tmp, got);
                    tmp[got] = '\0';
                    furi_string_cat_str(app->ap_scan_buffer, (const char*)tmp);
                }
            }
            if(++s_gps_ticks >= MM_GPS_PROBE_TICKS) {
                wifi_marauder_device_gps_close(app); // stop RX, free UART, restore log console
                wifi_marauder_device_gps_build_result(app);
            }
            consumed = true;
            return consumed;
        }
        uint8_t tmp[129];
        size_t got;
        while((got = furi_stream_buffer_receive(app->scan_stream, tmp, sizeof(tmp) - 1, 0)) > 0) {
            mm_sanitize_nuls(tmp, got);
            tmp[got] = '\0';
            furi_string_cat_str(app->ap_scan_buffer, (const char*)tmp);
        }
        if(!s_info_built && ++s_info_ticks >= MM_INFO_COLLECT_TICKS) {
            wifi_marauder_uart_set_handle_rx_data_cb(app->uart, NULL);
            wifi_marauder_device_info_build(app);
            s_info_built = true;
        }
        consumed = true;
    }

    return consumed;
}

void wifi_marauder_scene_device_info_on_exit(void* context) {
    WifiMarauderApp* app = context;
    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, NULL);
    // Leaving mid-probe must release LPUART and restore the log console, or the
    // channel stays held and logging stays off after we're gone.
    wifi_marauder_device_gps_close(app);
    submenu_reset(app->submenu);
}
