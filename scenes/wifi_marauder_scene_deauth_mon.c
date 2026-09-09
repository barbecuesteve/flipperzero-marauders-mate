// Marauder's Mate: deauth/disassoc monitor.
//
// Runs `sniffdeauth` (channel-hopping) and turns the raw deauth firehose into a
// live list of attack flows, deduped by (src -> dst) pair, counting frames per
// flow and sorting noisiest-first. This is a defensive view: "is someone
// deauthing my network, and who?". Firmware line (WiFiScan.cpp WIFI_SCAN_DEAUTH):
//   <rssi> Ch: <channel> <src> -> <dst>
#include "../wifi_marauder_app_i.h"

#define MM_DEAUTH_MAX 48
#define MM_DEAUTH_REBUILD_TICKS (5)
#define MM_LINES_PER_TICK (24) // cap lines parsed per tick to avoid GUI lockups

typedef struct {
    char src[18];
    char dst[18];
    int channel;
    int rssi;
    uint32_t hits;
} MMDeauthFlow;

static MMDeauthFlow s_flows[MM_DEAUTH_MAX];
static int s_flow_count;
static int s_order[MM_DEAUTH_MAX];
static uint32_t s_total; // total frames seen
static int s_ticks;

static void wifi_marauder_deauth_mon_rx_cb(uint8_t* buf, size_t len, void* context) {
    WifiMarauderApp* app = context;
    furi_stream_buffer_send(app->scan_stream, buf, len, 0);
}

static void wifi_marauder_deauth_mon_item_cb(void* context, uint32_t index) {
    UNUSED(context);
    UNUSED(index); // view-only monitor
}

// Last three octets of a MAC ("aa:bb:cc:dd:ee:ff" -> "dd:ee:ff") for compact rows.
static const char* mm_mac_tail(const char* mac) {
    return (mac && strlen(mac) == 17) ? mac + 9 : mac;
}

static void wifi_marauder_deauth_mon_upsert(const MMDeauthFrame* f) {
    for(int i = 0; i < s_flow_count; i++) {
        if(strcmp(s_flows[i].src, f->src) == 0 && strcmp(s_flows[i].dst, f->dst) == 0) {
            s_flows[i].hits++;
            s_flows[i].rssi = f->rssi;
            s_flows[i].channel = f->channel;
            return;
        }
    }
    if(s_flow_count >= MM_DEAUTH_MAX) return; // store full: drop new flows
    MMDeauthFlow* n = &s_flows[s_flow_count++];
    memcpy(n->src, f->src, sizeof(n->src));
    memcpy(n->dst, f->dst, sizeof(n->dst));
    n->channel = f->channel;
    n->rssi = f->rssi;
    n->hits = 1;
}

static void wifi_marauder_deauth_mon_rebuild(WifiMarauderApp* app) {
    uint32_t sel = submenu_get_selected_item(app->submenu);
    submenu_reset(app->submenu);

    char header[48];
    snprintf(
        header, sizeof(header), "Deauth: %d flow%s / %lu", s_flow_count,
        s_flow_count == 1 ? "" : "s", (unsigned long)s_total);
    submenu_set_header(app->submenu, header);

    // Insertion sort flow indices by hit count, descending.
    for(int i = 0; i < s_flow_count; i++) s_order[i] = i;
    for(int i = 1; i < s_flow_count; i++) {
        int v = s_order[i];
        int j = i - 1;
        while(j >= 0 && s_flows[s_order[j]].hits < s_flows[v].hits) {
            s_order[j + 1] = s_order[j];
            j--;
        }
        s_order[j + 1] = v;
    }

    char label[64];
    for(int k = 0; k < s_flow_count; k++) {
        int i = s_order[k];
        const char* dst =
            mm_mac_is_broadcast(s_flows[i].dst) ? "bcast" : mm_mac_tail(s_flows[i].dst);
        snprintf(
            label, sizeof(label), "%lux %s>%s", (unsigned long)s_flows[i].hits,
            mm_mac_tail(s_flows[i].src), dst);
        submenu_add_item(app->submenu, label, (uint32_t)i, wifi_marauder_deauth_mon_item_cb, app);
    }
    if(sel <= (uint32_t)s_flow_count) submenu_set_selected_item(app->submenu, sel);
}

static void wifi_marauder_deauth_mon_start(WifiMarauderApp* app) {
    s_flow_count = 0;
    s_total = 0;
    s_ticks = 0;
    furi_stream_buffer_reset(app->scan_stream);
    furi_string_reset(app->scan_line);

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Listening for deauths...");

    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, wifi_marauder_deauth_mon_rx_cb);
    wifi_marauder_uart_set_handle_rx_pcap_cb(app->uart, NULL);
    wifi_marauder_uart_tx(
        app->uart,
        (uint8_t*)("settings -s ChanHop enable\n"),
        strlen("settings -s ChanHop enable\n"));
    wifi_marauder_uart_tx(app->uart, (uint8_t*)("sniffdeauth\n"), strlen("sniffdeauth\n"));
}

void wifi_marauder_scene_deauth_mon_on_enter(void* context) {
    WifiMarauderApp* app = context;
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiMarauderAppViewSubmenu);
    wifi_marauder_deauth_mon_start(app);
}

bool wifi_marauder_scene_deauth_mon_on_event(void* context, SceneManagerEvent event) {
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
            char line[128];
            size_t cpy = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
            memcpy(line, cstr, cpy);
            line[cpy] = '\0';
            MMDeauthFrame f;
            if(mm_deauth_parse_line(line, &f)) {
                wifi_marauder_deauth_mon_upsert(&f);
                s_total++;
                changed = true;
            }
            furi_string_right(app->scan_line, len + 1);
            if(++processed >= MM_LINES_PER_TICK) break; // bound GUI-thread work per tick
        }
        s_ticks++;
        if(changed && (s_ticks % MM_DEAUTH_REBUILD_TICKS == 0)) {
            wifi_marauder_deauth_mon_rebuild(app);
        }
        consumed = true;
    }

    return consumed;
}

void wifi_marauder_scene_deauth_mon_on_exit(void* context) {
    WifiMarauderApp* app = context;
    wifi_marauder_uart_tx(app->uart, (uint8_t*)("stopscan\n"), strlen("stopscan\n"));
    wifi_marauder_uart_tx(
        app->uart,
        (uint8_t*)("settings -s ChanHop disable\n"),
        strlen("settings -s ChanHop disable\n"));
    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, NULL);
    submenu_reset(app->submenu);
}
