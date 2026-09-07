// Marauder's Mate: L3 host discovery scene.
//
// After the ESP has joined a network (via the AP detail's Join), pingscan
// sweeps the subnet and `list -i` yields the live host IPs. pingscan only
// populates the list once its full sweep completes (~45s), so this waits out
// the sweep, then parses the IP list into a navigable view.
//
// Port scanning is intentionally not wired here yet: `portscan -s <service>`
// is the working form, but its output could not be reliably captured on this
// board, so shipping it would show an empty console. Deferred.
#include "../wifi_marauder_app_i.h"

#define MM_PINGSCAN_TICKS (450) // ~45s: pingscan stores results only on completion
#define MM_HOSTLIST_TICKS (20) // ~2s to collect list -i output
#define MM_ITEM_RESCAN (0xFFFFFFFFu)

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
    app->host_selected = (int)index; // view-only for now
}

static void wifi_marauder_host_scan_build(WifiMarauderApp* app) {
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    char header[48];
    if(app->host_count == 0) {
        submenu_set_header(submenu, "No hosts found");
    } else {
        snprintf(header, sizeof(header), "Hosts: %d", app->host_count);
        submenu_set_header(submenu, header);
    }
    for(int i = 0; i < app->host_count; i++) {
        submenu_add_item(
            submenu, app->hosts[i], (uint32_t)i, wifi_marauder_host_scan_item_cb, app);
    }
    submenu_add_item(submenu, "> Rescan", MM_ITEM_RESCAN, wifi_marauder_host_scan_item_cb, app);
}

static void wifi_marauder_host_scan_start(WifiMarauderApp* app) {
    app->host_count = 0;
    app->host_state = MMHostScanning;
    app->host_ticks = 0;
    furi_stream_buffer_reset(app->scan_stream);
    furi_string_reset(app->scan_line);
    furi_string_reset(app->ap_scan_buffer);

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Scanning subnet...");

    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, wifi_marauder_host_scan_rx_cb);
    wifi_marauder_uart_set_handle_rx_pcap_cb(app->uart, NULL);
    wifi_marauder_host_scan_tx(app, "pingscan\n");
}

// Drain the RX stream; during the list phase, accumulate for parsing.
static void wifi_marauder_host_scan_drain(WifiMarauderApp* app) {
    uint8_t tmp[129];
    size_t got;
    while((got = furi_stream_buffer_receive(app->scan_stream, tmp, sizeof(tmp) - 1, 0)) > 0) {
        if(app->host_state != MMHostListing) continue; // discard pingscan chatter
        mm_sanitize_nuls(tmp, got);
        tmp[got] = '\0';
        furi_string_cat_str(app->ap_scan_buffer, (const char*)tmp);
    }
}

static void wifi_marauder_host_scan_parse(WifiMarauderApp* app) {
    app->host_count = 0;
    const char* text = furi_string_get_cstr(app->ap_scan_buffer);
    char line[64];
    while(*text && app->host_count < MM_HOST_MAX) {
        const char* nl = strchr(text, '\n');
        size_t len = nl ? (size_t)(nl - text) : strlen(text);
        size_t cpy = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
        memcpy(line, text, cpy);
        line[cpy] = '\0';
        if(mm_listi_parse_ip(line, app->hosts[app->host_count])) app->host_count++;
        if(!nl) break;
        text = nl + 1;
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

    if(event.type == SceneManagerEventTypeTick) {
        wifi_marauder_host_scan_drain(app);

        if(app->host_state == MMHostScanning) {
            if(app->host_ticks % 10 == 0) {
                int left = (MM_PINGSCAN_TICKS - app->host_ticks) / 10;
                char header[48];
                snprintf(header, sizeof(header), "Scanning subnet ~%ds", left);
                submenu_set_header(app->submenu, header);
            }
            if(++app->host_ticks >= MM_PINGSCAN_TICKS) {
                wifi_marauder_host_scan_tx(app, "stopscan\n");
                furi_string_reset(app->ap_scan_buffer);
                wifi_marauder_host_scan_tx(app, "list -i\n");
                app->host_state = MMHostListing;
                app->host_ticks = 0;
                submenu_set_header(app->submenu, "Loading hosts...");
            }
        } else if(app->host_state == MMHostListing) {
            if(++app->host_ticks >= MM_HOSTLIST_TICKS) {
                wifi_marauder_uart_set_handle_rx_data_cb(app->uart, NULL);
                wifi_marauder_host_scan_drain(app);
                wifi_marauder_host_scan_parse(app);
                app->host_state = MMHostReady;
                wifi_marauder_host_scan_build(app);
            }
        }
        consumed = true;
    }

    return consumed;
}

void wifi_marauder_scene_host_scan_on_exit(void* context) {
    WifiMarauderApp* app = context;
    if(app->host_state == MMHostScanning || app->host_state == MMHostListing) {
        wifi_marauder_host_scan_tx(app, "stopscan\n");
        app->host_state = MMHostReady;
    }
    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, NULL);
    submenu_reset(app->submenu);
}
