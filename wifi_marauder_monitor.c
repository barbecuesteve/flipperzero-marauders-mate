// Marauder's Mate: shared live-monitor engine (see wifi_marauder_monitor.h).
#include "wifi_marauder_app_i.h"
#include "wifi_marauder_monitor.h"

#define MM_MON_MAX 64
#define MM_MON_REBUILD_TICKS 5
#define MM_MON_LINES_PER_TICK 24 // bound GUI-thread work per tick

// Only one monitor runs at a time, so a single shared store is fine.
static MMMonRec s_rec[MM_MON_MAX];
static int s_count;
static int s_order[MM_MON_MAX];
static int s_ticks;

static void mm_monitor_rx_cb(uint8_t* buf, size_t len, void* context) {
    WifiMarauderApp* app = context;
    furi_stream_buffer_send(app->scan_stream, buf, len, 0);
}

static void mm_monitor_item_cb(void* context, uint32_t index) {
    UNUSED(context);
    UNUSED(index); // view-only
}

static int mm_monitor_sortval(const MMMonDef* def, const MMMonRec* r) {
    return def->sort_by_metric ? r->metric : (int)r->hits;
}

static void mm_monitor_upsert(const MMMonDef* def, const MMMonRec* in) {
    for(int i = 0; i < s_count; i++) {
        if(strcmp(s_rec[i].key, in->key) == 0) {
            s_rec[i].hits++;
            // Refresh display fields; for a metric, keep the strongest/highest.
            strncpy(s_rec[i].t1, in->t1, sizeof(s_rec[i].t1) - 1);
            strncpy(s_rec[i].t2, in->t2, sizeof(s_rec[i].t2) - 1);
            s_rec[i].channel = in->channel;
            s_rec[i].rssi = in->rssi;
            if(!def->sort_by_metric || in->metric > s_rec[i].metric)
                s_rec[i].metric = in->metric;
            return;
        }
    }
    if(s_count >= MM_MON_MAX) return; // store full: drop new keys
    s_rec[s_count] = *in;
    s_rec[s_count].hits = 1;
    s_count++;
}

static void mm_monitor_rebuild(WifiMarauderApp* app, const MMMonDef* def) {
    uint32_t sel = submenu_get_selected_item(app->submenu);
    submenu_reset(app->submenu);

    char header[48];
    snprintf(header, sizeof(header), def->header_fmt, s_count);
    submenu_set_header(app->submenu, header);

    // Insertion sort indices by the chosen value, descending (n <= 64).
    for(int i = 0; i < s_count; i++) s_order[i] = i;
    for(int i = 1; i < s_count; i++) {
        int v = s_order[i];
        int j = i - 1;
        while(j >= 0 && mm_monitor_sortval(def, &s_rec[s_order[j]]) < mm_monitor_sortval(def, &s_rec[v])) {
            s_order[j + 1] = s_order[j];
            j--;
        }
        s_order[j + 1] = v;
    }

    char label[64];
    for(int k = 0; k < s_count; k++) {
        int i = s_order[k];
        def->label(&s_rec[i], label, sizeof(label));
        submenu_add_item(app->submenu, label, (uint32_t)i, mm_monitor_item_cb, app);
    }
    if(sel <= (uint32_t)s_count) submenu_set_selected_item(app->submenu, sel);
}

void mm_monitor_start(WifiMarauderApp* app, const MMMonDef* def) {
    s_count = 0;
    s_ticks = 0;
    furi_stream_buffer_reset(app->scan_stream);
    furi_string_reset(app->scan_line);

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, def->waiting);
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiMarauderAppViewSubmenu);

    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, mm_monitor_rx_cb);
    wifi_marauder_uart_set_handle_rx_pcap_cb(app->uart, NULL);
    if(def->chanhop) {
        wifi_marauder_uart_tx(
            app->uart,
            (uint8_t*)"settings -s ChanHop enable\n",
            strlen("settings -s ChanHop enable\n"));
    }
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "%s\n", def->cmd);
    wifi_marauder_uart_tx(app->uart, (uint8_t*)cmd, strlen(cmd));
}

void mm_monitor_tick(WifiMarauderApp* app, const MMMonDef* def) {
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
        MMMonRec rec;
        memset(&rec, 0, sizeof(rec));
        if(def->parse(line, &rec)) {
            mm_monitor_upsert(def, &rec);
            changed = true;
        }
        furi_string_right(app->scan_line, len + 1);
        if(++processed >= MM_MON_LINES_PER_TICK) break;
    }
    if(changed && (++s_ticks % MM_MON_REBUILD_TICKS == 0)) {
        mm_monitor_rebuild(app, def);
    }
}

void mm_monitor_stop(WifiMarauderApp* app, const MMMonDef* def) {
    wifi_marauder_uart_tx(app->uart, (uint8_t*)"stopscan\n", strlen("stopscan\n"));
    if(def->chanhop) {
        wifi_marauder_uart_tx(
            app->uart,
            (uint8_t*)"settings -s ChanHop disable\n",
            strlen("settings -s ChanHop disable\n"));
    }
    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, NULL);
    submenu_reset(app->submenu);
}
