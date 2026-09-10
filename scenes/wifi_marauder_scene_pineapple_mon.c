// Marauder's Mate: WiFi Pineapple / rogue-AP monitor.
//
// Runs `sniffpinescan` and turns each confirmed detection into a live list,
// deduped by MAC and sorted strongest-first (closest threat on top). Firmware
// line (WiFiScan.cpp): "MAC: <mac> CH: <ch> RSSI: <rssi> DET: <type> SSID: <e>".
#include "../wifi_marauder_app_i.h"

#define MM_PINE_MAX 48
#define MM_PINE_REBUILD_TICKS (5)
#define MM_LINES_PER_TICK (24)

static MMPineScan s_pine[MM_PINE_MAX];
static int s_pine_count;
static int s_order[MM_PINE_MAX];
static int s_ticks;

static void wifi_marauder_pineapple_mon_rx_cb(uint8_t* buf, size_t len, void* context) {
    WifiMarauderApp* app = context;
    furi_stream_buffer_send(app->scan_stream, buf, len, 0);
}

static void wifi_marauder_pineapple_mon_item_cb(void* context, uint32_t index) {
    UNUSED(context);
    UNUSED(index); // view-only monitor
}

// Compact detection tag for the narrow row.
static const char* mm_pine_det_short(const char* det) {
    if(strncmp(det, "SUSP_OUI", 8) == 0) return "OUI";
    if(strncmp(det, "TAG", 3) == 0) return "TAG";
    return "?";
}

static void wifi_marauder_pineapple_mon_upsert(const MMPineScan* p) {
    for(int i = 0; i < s_pine_count; i++) {
        if(strcmp(s_pine[i].mac, p->mac) == 0) {
            if(p->rssi > s_pine[i].rssi) s_pine[i].rssi = p->rssi; // keep strongest
            if(p->ssid[0] && strcmp(p->ssid, "[hidden]") != 0) strcpy(s_pine[i].ssid, p->ssid);
            strcpy(s_pine[i].det, p->det);
            s_pine[i].channel = p->channel;
            return;
        }
    }
    if(s_pine_count >= MM_PINE_MAX) return;
    s_pine[s_pine_count++] = *p;
}

static void wifi_marauder_pineapple_mon_rebuild(WifiMarauderApp* app) {
    uint32_t sel = submenu_get_selected_item(app->submenu);
    submenu_reset(app->submenu);

    char header[40];
    snprintf(header, sizeof(header), "Pineapples: %d", s_pine_count);
    submenu_set_header(app->submenu, header);

    // Sort indices by RSSI descending (strongest / closest first).
    for(int i = 0; i < s_pine_count; i++) s_order[i] = i;
    for(int i = 1; i < s_pine_count; i++) {
        int v = s_order[i];
        int j = i - 1;
        while(j >= 0 && s_pine[s_order[j]].rssi < s_pine[v].rssi) {
            s_order[j + 1] = s_order[j];
            j--;
        }
        s_order[j + 1] = v;
    }

    char label[64];
    for(int k = 0; k < s_pine_count; k++) {
        int i = s_order[k];
        const char* name =
            (s_pine[i].ssid[0] && strcmp(s_pine[i].ssid, "[hidden]") != 0) ? s_pine[i].ssid :
                                                                             s_pine[i].mac + 9;
        snprintf(
            label, sizeof(label), "%s %s %d", name, mm_pine_det_short(s_pine[i].det),
            s_pine[i].rssi);
        submenu_add_item(app->submenu, label, (uint32_t)i, wifi_marauder_pineapple_mon_item_cb, app);
    }
    if(sel <= (uint32_t)s_pine_count) submenu_set_selected_item(app->submenu, sel);
}

static void wifi_marauder_pineapple_mon_start(WifiMarauderApp* app) {
    s_pine_count = 0;
    s_ticks = 0;
    furi_stream_buffer_reset(app->scan_stream);
    furi_string_reset(app->scan_line);

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Scanning for Pineapples...");

    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, wifi_marauder_pineapple_mon_rx_cb);
    wifi_marauder_uart_set_handle_rx_pcap_cb(app->uart, NULL);
    wifi_marauder_uart_tx(
        app->uart,
        (uint8_t*)("settings -s ChanHop enable\n"),
        strlen("settings -s ChanHop enable\n"));
    wifi_marauder_uart_tx(app->uart, (uint8_t*)("sniffpinescan\n"), strlen("sniffpinescan\n"));
}

void wifi_marauder_scene_pineapple_mon_on_enter(void* context) {
    WifiMarauderApp* app = context;
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiMarauderAppViewSubmenu);
    wifi_marauder_pineapple_mon_start(app);
}

bool wifi_marauder_scene_pineapple_mon_on_event(void* context, SceneManagerEvent event) {
    WifiMarauderApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        uint8_t tmp[129];
        size_t got;
        bool changed = false;
        int processed = 0;
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
            char line[160];
            size_t cpy = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
            memcpy(line, cstr, cpy);
            line[cpy] = '\0';
            MMPineScan p;
            if(mm_pinescan_parse_line(line, &p)) {
                wifi_marauder_pineapple_mon_upsert(&p);
                changed = true;
            }
            furi_string_right(app->scan_line, len + 1);
            if(++processed >= MM_LINES_PER_TICK) break;
        }
        s_ticks++;
        if(changed && (s_ticks % MM_PINE_REBUILD_TICKS == 0)) {
            wifi_marauder_pineapple_mon_rebuild(app);
        }
        consumed = true;
    }

    return consumed;
}

void wifi_marauder_scene_pineapple_mon_on_exit(void* context) {
    WifiMarauderApp* app = context;
    wifi_marauder_uart_tx(app->uart, (uint8_t*)("stopscan\n"), strlen("stopscan\n"));
    wifi_marauder_uart_tx(
        app->uart,
        (uint8_t*)("settings -s ChanHop disable\n"),
        strlen("settings -s ChanHop disable\n"));
    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, NULL);
    submenu_reset(app->submenu);
}
