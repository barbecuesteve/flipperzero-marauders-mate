// Marauder's Mate: probe-request monitor.
//
// Runs `sniffprobe` (channel-hopping) and turns the raw probe firehose into a
// live list of client devices deduped by MAC, counting probes per client and
// sorting noisiest-first, showing the last non-empty SSID each device probed
// for. Shows who's around and what networks their devices remember.
#include "../wifi_marauder_app_i.h"

#define MM_PROBE_REBUILD_TICKS (5) // rebuild ~2/s

static int s_order[MM_AP_MAX]; // display order (by probe count desc), GUI-thread only

static void wifi_marauder_probe_mon_rx_cb(uint8_t* buf, size_t len, void* context) {
    WifiMarauderApp* app = context;
    furi_stream_buffer_send(app->scan_stream, buf, len, 0);
}

static void wifi_marauder_probe_mon_item_cb(void* context, uint32_t index) {
    UNUSED(context);
    UNUSED(index); // view-only monitor
}

static void wifi_marauder_probe_mon_rebuild(WifiMarauderApp* app) {
    uint32_t sel = submenu_get_selected_item(app->submenu);
    submenu_reset(app->submenu);

    char header[48];
    snprintf(header, sizeof(header), "Probes: %d clients", app->probe_client_count);
    submenu_set_header(app->submenu, header);

    for(int i = 0; i < app->probe_client_count; i++) s_order[i] = i;
    for(int i = 1; i < app->probe_client_count; i++) {
        int v = s_order[i];
        int j = i - 1;
        while(j >= 0 && app->probe_hits[s_order[j]] < app->probe_hits[v]) {
            s_order[j + 1] = s_order[j];
            j--;
        }
        s_order[j + 1] = v;
    }

    char label[96];
    for(int k = 0; k < app->probe_client_count; k++) {
        int i = s_order[k];
        MMScanAp* c = &app->probe_clients[i];
        // .bssid holds the client MAC; .ssid the last requested network (if any)
        if(!c->hidden && c->ssid[0]) {
            snprintf(label, sizeof(label), "%dx %s ->%s", app->probe_hits[i], c->bssid, c->ssid);
        } else {
            snprintf(label, sizeof(label), "%dx %s", app->probe_hits[i], c->bssid);
        }
        submenu_add_item(app->submenu, label, (uint32_t)i, wifi_marauder_probe_mon_item_cb, app);
    }
    if(sel <= (uint32_t)app->probe_client_count) submenu_set_selected_item(app->submenu, sel);
}

static bool wifi_marauder_probe_mon_process_line(WifiMarauderApp* app, const char* line) {
    MMScanAp cur;
    if(!mm_probe_parse_line(line, &cur)) return false;
    bool is_new = false;
    // Dedup by client MAC (stored in .bssid). A named request updates .ssid;
    // an empty (broadcast) probe leaves the last known request intact.
    int idx = mm_scan_store_upsert(
        app->probe_clients, &app->probe_client_count, MM_AP_MAX, &cur, &is_new);
    if(idx < 0) return false;
    if(is_new) app->probe_hits[idx] = 0;
    app->probe_hits[idx]++;
    return true;
}

static void wifi_marauder_probe_mon_start(WifiMarauderApp* app) {
    app->probe_client_count = 0;
    app->probe_ticks = 0;
    furi_stream_buffer_reset(app->scan_stream);
    furi_string_reset(app->scan_line);

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Listening for probes...");

    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, wifi_marauder_probe_mon_rx_cb);
    wifi_marauder_uart_set_handle_rx_pcap_cb(app->uart, NULL);
    wifi_marauder_uart_tx(
        app->uart,
        (uint8_t*)("settings -s ChanHop enable\n"),
        strlen("settings -s ChanHop enable\n"));
    wifi_marauder_uart_tx(app->uart, (uint8_t*)("sniffprobe\n"), strlen("sniffprobe\n"));
}

void wifi_marauder_scene_probe_mon_on_enter(void* context) {
    WifiMarauderApp* app = context;
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiMarauderAppViewSubmenu);
    wifi_marauder_probe_mon_start(app);
}

bool wifi_marauder_scene_probe_mon_on_event(void* context, SceneManagerEvent event) {
    WifiMarauderApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        uint8_t tmp[129];
        size_t got;
        bool changed = false;
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
            char line[128];
            size_t cpy = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
            memcpy(line, cstr, cpy);
            line[cpy] = '\0';
            if(wifi_marauder_probe_mon_process_line(app, line)) changed = true;
            furi_string_right(app->scan_line, len + 1);
        }
        app->probe_ticks++;
        if(changed && (app->probe_ticks % MM_PROBE_REBUILD_TICKS == 0)) {
            wifi_marauder_probe_mon_rebuild(app);
        }
        consumed = true;
    }

    return consumed;
}

void wifi_marauder_scene_probe_mon_on_exit(void* context) {
    WifiMarauderApp* app = context;
    wifi_marauder_uart_tx(app->uart, (uint8_t*)("stopscan\n"), strlen("stopscan\n"));
    wifi_marauder_uart_tx(
        app->uart,
        (uint8_t*)("settings -s ChanHop disable\n"),
        strlen("settings -s ChanHop disable\n"));
    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, NULL);
    submenu_reset(app->submenu);
}
