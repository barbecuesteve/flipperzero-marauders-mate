// Marauder's Mate: parsed device info scene.
//
// Runs `info` and shows the ESP's "Key: Value" fields (Firmware, Version,
// Hardware, MACs, SD card, ...) as a clean scrollable list instead of raw
// console. Info is a one-shot query, so we collect for ~1.5s then render.
#include "../wifi_marauder_app_i.h"

#define MM_INFO_COLLECT_TICKS 15 // ~1.5s to collect the info reply
#define MM_INFO_ROW 700
#define MM_INFO_DISCONNECT 699 // actionable row: stopscan -f

// Forceful stop: ends any scan AND drops the joined network (WiFi.disconnect).
static const char* const MM_INFO_CMD_DISCONNECT = "stopscan -f";

static int s_info_ticks;
static bool s_info_built;

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
    }
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
    if(shown == 0) {
        submenu_add_item(
            submenu, "No response", MM_INFO_ROW, wifi_marauder_device_info_noop_cb, app);
    }
}

void wifi_marauder_scene_device_info_on_enter(void* context) {
    WifiMarauderApp* app = context;
    s_info_ticks = 0;
    s_info_built = false;
    furi_stream_buffer_reset(app->scan_stream);
    furi_string_reset(app->scan_line);
    furi_string_reset(app->ap_scan_buffer);

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Querying...");
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiMarauderAppViewSubmenu);

    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, wifi_marauder_device_info_rx_cb);
    wifi_marauder_uart_set_handle_rx_pcap_cb(app->uart, NULL);
    wifi_marauder_uart_tx(app->uart, (uint8_t*)"info\n", strlen("info\n"));
}

bool wifi_marauder_scene_device_info_on_event(void* context, SceneManagerEvent event) {
    WifiMarauderApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == WifiMarauderEventScanDisconnect) {
            app->wifi_connected = false;
            app->connected_bssid[0] = '\0';
            app->connected_ssid[0] = '\0';
            app->selected_tx_string = MM_INFO_CMD_DISCONNECT;
            app->is_command = true;
            app->is_custom_tx_string = false;
            app->focus_console_start = false;
            app->show_stopscan_tip = false;
            app->script = NULL;
            scene_manager_next_scene(app->scene_manager, WifiMarauderSceneConsoleOutput);
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeTick) {
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
    submenu_reset(app->submenu);
}
