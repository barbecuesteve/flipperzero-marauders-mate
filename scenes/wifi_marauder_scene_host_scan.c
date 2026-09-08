// Marauder's Mate: L3 host discovery scene.
//
// After the ESP has joined a network (via the AP detail's Join), `arpscan`
// sweeps the subnet. It is much faster than pingscan and STREAMS each active
// host as a bare "<IPv4>" line, so we parse those live and grow the list as
// they arrive. The scan keeps running until you leave or Rescan.
#include "../wifi_marauder_app_i.h"

#define MM_ITEM_RESCAN (0xFFFFFFFFu)

static char s_portscan_cmd[24]; // "portscan -t <n> -a"

static void wifi_marauder_host_scan_rx_cb(uint8_t* buf, size_t len, void* context) {
    WifiMarauderApp* app = context;
    furi_stream_buffer_send(app->scan_stream, buf, len, 0);
}

static void wifi_marauder_host_scan_tx(WifiMarauderApp* app, const char* cmd) {
    wifi_marauder_uart_tx(app->uart, (uint8_t*)cmd, strlen(cmd));
}

static void wifi_marauder_host_scan_start(WifiMarauderApp* app);

static void wifi_marauder_host_scan_item_cb(void* context, uint32_t index) {
    WifiMarauderApp* app = context;
    if(index == MM_ITEM_RESCAN) {
        wifi_marauder_host_scan_start(app);
        return;
    }
    // Selecting a host full-port-scans it. The row value is the host's display
    // index, which equals the ESP's ipList index (arpscan clears+repopulates
    // ipList in stream order each scan), so `portscan -t <index> -a` hits it.
    app->host_selected = (int)index;
    view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventHostPortScan);
}

static void wifi_marauder_host_scan_build(WifiMarauderApp* app) {
    Submenu* submenu = app->submenu;
    uint32_t sel = submenu_get_selected_item(submenu); // by item value (the IP index)
    submenu_reset(submenu);
    char header[48];
    if(app->host_state == MMHostScanning) {
        snprintf(header, sizeof(header), "Scanning... (%d)", app->host_count);
    } else if(app->host_count == 0) {
        snprintf(header, sizeof(header), "No hosts found");
    } else {
        snprintf(header, sizeof(header), "Hosts: %d", app->host_count);
    }
    submenu_set_header(submenu, header);
    for(int i = 0; i < app->host_count; i++) {
        submenu_add_item(
            submenu, app->hosts[i], (uint32_t)i, wifi_marauder_host_scan_item_cb, app);
    }
    submenu_add_item(submenu, "> Rescan", MM_ITEM_RESCAN, wifi_marauder_host_scan_item_cb, app);
    submenu_set_selected_item(submenu, sel);
}

static void wifi_marauder_host_scan_start(WifiMarauderApp* app) {
    app->host_count = 0;
    app->host_built = 0;
    app->host_dirty = false;
    app->host_state = MMHostScanning;
    app->host_ticks = 0;
    furi_stream_buffer_reset(app->scan_stream);
    furi_string_reset(app->scan_line);

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Scanning...");

    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, wifi_marauder_host_scan_rx_cb);
    wifi_marauder_uart_set_handle_rx_pcap_cb(app->uart, NULL);
    wifi_marauder_host_scan_tx(app, "arpscan\n");
}

// Drain the RX stream and parse each streamed host IP into the store (deduped).
static void wifi_marauder_host_scan_drain(WifiMarauderApp* app) {
    uint8_t tmp[129];
    size_t got;
    while((got = furi_stream_buffer_receive(app->scan_stream, tmp, sizeof(tmp) - 1, 0)) > 0) {
        mm_sanitize_nuls(tmp, got);
        tmp[got] = '\0';
        furi_string_cat_str(app->scan_line, (const char*)tmp);
    }

    int processed = 0;
    for(;;) {
        const char* cstr = furi_string_get_cstr(app->scan_line);
        const char* nl = strchr(cstr, '\n');
        if(!nl) break;
        size_t len = (size_t)(nl - cstr);
        char line[64];
        size_t cpy = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
        memcpy(line, cstr, cpy);
        line[cpy] = '\0';
        furi_string_right(app->scan_line, len + 1);

        char ip[16];
        if(mm_hostscan_parse_ip(line, ip)) {
            bool dup = false;
            for(int i = 0; i < app->host_count; i++) {
                if(strcmp(app->hosts[i], ip) == 0) {
                    dup = true;
                    break;
                }
            }
            if(!dup && app->host_count < MM_HOST_MAX) {
                strcpy(app->hosts[app->host_count++], ip);
                app->host_dirty = true;
            }
        }
        if(++processed >= 24) break; // bound GUI-thread work per tick
    }
}

void wifi_marauder_scene_host_scan_on_enter(void* context) {
    WifiMarauderApp* app = context;
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiMarauderAppViewSubmenu);
    if(app->host_state == MMHostReady) {
        wifi_marauder_host_scan_build(app);
        submenu_set_selected_item(app->submenu, (uint32_t)app->host_selected);
    } else {
        wifi_marauder_host_scan_start(app);
    }
}

bool wifi_marauder_scene_host_scan_on_event(void* context, SceneManagerEvent event) {
    WifiMarauderApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == WifiMarauderEventHostPortScan) {
            // Full port scan of the selected host by its ipList index.
            snprintf(
                s_portscan_cmd, sizeof(s_portscan_cmd), "portscan -t %d -a", app->host_selected);
            app->selected_tx_string = s_portscan_cmd;
            app->is_command = true;
            app->is_custom_tx_string = false;
            app->focus_console_start = false;
            app->show_stopscan_tip = true;
            app->script = NULL;
            scene_manager_next_scene(app->scene_manager, WifiMarauderSceneConsoleOutput);
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        if(app->host_state == MMHostScanning) {
            wifi_marauder_host_scan_drain(app);
            // Rebuild only when the host set grew (throttled ~2/s) to keep the
            // GUI thread free -- same lockup-avoidance as the live AP scan.
            bool grew = app->host_count > app->host_built;
            if(grew && (app->host_ticks % 5 == 0)) {
                wifi_marauder_host_scan_build(app);
                app->host_built = app->host_count;
                app->host_dirty = false;
            } else if(app->host_ticks % 10 == 0) {
                // keep the "Scanning... (N)" header ticking even with no new host
                char header[48];
                snprintf(header, sizeof(header), "Scanning... (%d)", app->host_count);
                submenu_set_header(app->submenu, header);
            }
            app->host_ticks++;
        }
        consumed = true;
    }

    return consumed;
}

void wifi_marauder_scene_host_scan_on_exit(void* context) {
    WifiMarauderApp* app = context;
    if(app->host_state == MMHostScanning) {
        wifi_marauder_host_scan_tx(app, "stopscan\n");
        app->host_state = MMHostReady;
    }
    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, NULL);
    submenu_reset(app->submenu);
}
