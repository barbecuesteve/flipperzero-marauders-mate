// Marauder's Mate: beacon activity monitor.
//
// Runs `sniffbeacon` and turns the raw beacon firehose into a live list of APs
// deduped by BSSID, counting beacon frames per AP and sorting noisiest-first.
// Reads as a beacon-spam detector: a flood rockets to the top with a huge
// count; legit APs sit at a steady low rate.
#include "../wifi_marauder_app_i.h"

#define MM_BEACON_REBUILD_TICKS (5) // rebuild ~2/s to keep the GUI responsive

static int s_order[MM_AP_MAX]; // display order (by hit count desc), GUI-thread only

static void wifi_marauder_beacon_mon_rx_cb(uint8_t* buf, size_t len, void* context) {
    WifiMarauderApp* app = context;
    furi_stream_buffer_send(app->scan_stream, buf, len, 0);
}

static void wifi_marauder_beacon_mon_item_cb(void* context, uint32_t index) {
    UNUSED(context);
    UNUSED(index); // view-only monitor
}

static void wifi_marauder_beacon_mon_rebuild(WifiMarauderApp* app) {
    uint32_t sel = submenu_get_selected_item(app->submenu);
    submenu_reset(app->submenu);

    char header[24];
    snprintf(header, sizeof(header), "Beacons: %d APs", app->beacon_ap_count);
    submenu_set_header(app->submenu, header);

    // Sort display order by beacon hit count, descending (insertion sort, <=64).
    for(int i = 0; i < app->beacon_ap_count; i++) s_order[i] = i;
    for(int i = 1; i < app->beacon_ap_count; i++) {
        int v = s_order[i];
        int j = i - 1;
        while(j >= 0 && app->beacon_hits[s_order[j]] < app->beacon_hits[v]) {
            s_order[j + 1] = s_order[j];
            j--;
        }
        s_order[j + 1] = v;
    }

    char label[96];
    for(int k = 0; k < app->beacon_ap_count; k++) {
        int i = s_order[k];
        MMScanAp* a = &app->beacon_aps[i];
        const char* name = a->hidden ? "[hidden]" : a->ssid;
        snprintf(label, sizeof(label), "%dx %s C%d", app->beacon_hits[i], name, a->channel);
        submenu_add_item(app->submenu, label, (uint32_t)i, wifi_marauder_beacon_mon_item_cb, app);
    }
    if(sel <= (uint32_t)app->beacon_ap_count) submenu_set_selected_item(app->submenu, sel);
}

static bool wifi_marauder_beacon_mon_process_line(WifiMarauderApp* app, const char* line) {
    MMScanAp cur;
    if(!mm_beacon_parse_line(line, &cur)) return false;
    bool is_new = false;
    int idx = mm_scan_store_upsert(
        app->beacon_aps, &app->beacon_ap_count, MM_AP_MAX, &cur, &is_new);
    if(idx < 0) return false; // store full
    if(is_new) app->beacon_hits[idx] = 0;
    app->beacon_hits[idx]++;
    return true;
}

static void wifi_marauder_beacon_mon_start(WifiMarauderApp* app) {
    app->beacon_ap_count = 0;
    app->beacon_ticks = 0;
    furi_stream_buffer_reset(app->scan_stream);
    furi_string_reset(app->scan_line);

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Listening for beacons...");

    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, wifi_marauder_beacon_mon_rx_cb);
    wifi_marauder_uart_set_handle_rx_pcap_cb(app->uart, NULL);
    // Hop channels so we catch beacons on every channel, not just the parked one.
    wifi_marauder_uart_tx(
        app->uart,
        (uint8_t*)("settings -s ChanHop enable\n"),
        strlen("settings -s ChanHop enable\n"));
    wifi_marauder_uart_tx(app->uart, (uint8_t*)("sniffbeacon\n"), strlen("sniffbeacon\n"));
}

void wifi_marauder_scene_beacon_mon_on_enter(void* context) {
    WifiMarauderApp* app = context;
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiMarauderAppViewSubmenu);
    wifi_marauder_beacon_mon_start(app);
}

bool wifi_marauder_scene_beacon_mon_on_event(void* context, SceneManagerEvent event) {
    WifiMarauderApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        // Drain the RX stream and count complete beacon lines.
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
            if(wifi_marauder_beacon_mon_process_line(app, line)) changed = true;
            furi_string_right(app->scan_line, len + 1);
        }

        // Throttled rebuild (~2/s).
        app->beacon_ticks++;
        if(changed && (app->beacon_ticks % MM_BEACON_REBUILD_TICKS == 0)) {
            wifi_marauder_beacon_mon_rebuild(app);
        }
        consumed = true;
    }

    return consumed;
}

void wifi_marauder_scene_beacon_mon_on_exit(void* context) {
    WifiMarauderApp* app = context;
    wifi_marauder_uart_tx(app->uart, (uint8_t*)("stopscan\n"), strlen("stopscan\n"));
    // Restore channel hopping to its default-off state.
    wifi_marauder_uart_tx(
        app->uart,
        (uint8_t*)("settings -s ChanHop disable\n"),
        strlen("settings -s ChanHop disable\n"));
    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, NULL);
    submenu_reset(app->submenu);
}
