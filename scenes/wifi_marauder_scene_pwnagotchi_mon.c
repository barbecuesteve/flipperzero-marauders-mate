// Marauder's Mate: Pwnagotchi monitor.
//
// Runs `sniffpwn` and lists nearby Pwnagotchi units, deduped by name and sorted
// by handshakes captured (pwnd). Firmware prints two lines per beacon
// (processPwnagotchiBeacon): "Name: <name>" then "Pwnd #: <n>". No MAC is
// emitted over serial, so name is the only identity we have.
#include "../wifi_marauder_app_i.h"

#define MM_PWN_MAX 32
#define MM_PWN_REBUILD_TICKS (5)
#define MM_LINES_PER_TICK (24)

typedef struct {
    char name[33];
    int pwnd;
    uint32_t seen; // beacons observed
} MMPwnEntry;

static MMPwnEntry s_pwn[MM_PWN_MAX];
static int s_pwn_count;
static int s_order[MM_PWN_MAX];
static char s_pending[33]; // name awaiting its "Pwnd #:" line
static int s_ticks;

static void wifi_marauder_pwn_mon_rx_cb(uint8_t* buf, size_t len, void* context) {
    WifiMarauderApp* app = context;
    furi_stream_buffer_send(app->scan_stream, buf, len, 0);
}

static void wifi_marauder_pwn_mon_item_cb(void* context, uint32_t index) {
    UNUSED(context);
    UNUSED(index); // view-only monitor
}

static void wifi_marauder_pwn_mon_upsert(const char* name, int pwnd) {
    for(int i = 0; i < s_pwn_count; i++) {
        if(strcmp(s_pwn[i].name, name) == 0) {
            s_pwn[i].pwnd = pwnd; // latest reported total
            s_pwn[i].seen++;
            return;
        }
    }
    if(s_pwn_count >= MM_PWN_MAX) return;
    MMPwnEntry* e = &s_pwn[s_pwn_count++];
    strncpy(e->name, name, sizeof(e->name) - 1);
    e->name[sizeof(e->name) - 1] = '\0';
    e->pwnd = pwnd;
    e->seen = 1;
}

static void wifi_marauder_pwn_mon_rebuild(WifiMarauderApp* app) {
    uint32_t sel = submenu_get_selected_item(app->submenu);
    submenu_reset(app->submenu);

    char header[40];
    snprintf(header, sizeof(header), "Pwnagotchi: %d", s_pwn_count);
    submenu_set_header(app->submenu, header);

    // Sort by pwnd count descending (busiest units first).
    for(int i = 0; i < s_pwn_count; i++) s_order[i] = i;
    for(int i = 1; i < s_pwn_count; i++) {
        int v = s_order[i];
        int j = i - 1;
        while(j >= 0 && s_pwn[s_order[j]].pwnd < s_pwn[v].pwnd) {
            s_order[j + 1] = s_order[j];
            j--;
        }
        s_order[j + 1] = v;
    }

    char label[64];
    for(int k = 0; k < s_pwn_count; k++) {
        int i = s_order[k];
        snprintf(label, sizeof(label), "%s  Pwnd:%d", s_pwn[i].name, s_pwn[i].pwnd);
        submenu_add_item(app->submenu, label, (uint32_t)i, wifi_marauder_pwn_mon_item_cb, app);
    }
    if(sel <= (uint32_t)s_pwn_count) submenu_set_selected_item(app->submenu, sel);
}

static void wifi_marauder_pwn_mon_start(WifiMarauderApp* app) {
    s_pwn_count = 0;
    s_ticks = 0;
    s_pending[0] = '\0';
    furi_stream_buffer_reset(app->scan_stream);
    furi_string_reset(app->scan_line);

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Listening for Pwnagotchi...");

    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, wifi_marauder_pwn_mon_rx_cb);
    wifi_marauder_uart_set_handle_rx_pcap_cb(app->uart, NULL);
    wifi_marauder_uart_tx(
        app->uart,
        (uint8_t*)("settings -s ChanHop enable\n"),
        strlen("settings -s ChanHop enable\n"));
    wifi_marauder_uart_tx(app->uart, (uint8_t*)("sniffpwn\n"), strlen("sniffpwn\n"));
}

void wifi_marauder_scene_pwnagotchi_mon_on_enter(void* context) {
    WifiMarauderApp* app = context;
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiMarauderAppViewSubmenu);
    wifi_marauder_pwn_mon_start(app);
}

bool wifi_marauder_scene_pwnagotchi_mon_on_event(void* context, SceneManagerEvent event) {
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
            char line[96];
            size_t cpy = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
            memcpy(line, cstr, cpy);
            line[cpy] = '\0';
            // A "Name:" line arms the pending name; the following "Pwnd #:" line
            // commits the pair.
            char name[33];
            int pwnd;
            if(mm_pwn_line_name(line, name, sizeof(name))) {
                strncpy(s_pending, name, sizeof(s_pending) - 1);
                s_pending[sizeof(s_pending) - 1] = '\0';
            } else if(mm_pwn_line_pwnd(line, &pwnd) && s_pending[0]) {
                wifi_marauder_pwn_mon_upsert(s_pending, pwnd);
                s_pending[0] = '\0';
                changed = true;
            }
            furi_string_right(app->scan_line, len + 1);
            if(++processed >= MM_LINES_PER_TICK) break;
        }
        s_ticks++;
        if(changed && (s_ticks % MM_PWN_REBUILD_TICKS == 0)) {
            wifi_marauder_pwn_mon_rebuild(app);
        }
        consumed = true;
    }

    return consumed;
}

void wifi_marauder_scene_pwnagotchi_mon_on_exit(void* context) {
    WifiMarauderApp* app = context;
    wifi_marauder_uart_tx(app->uart, (uint8_t*)("stopscan\n"), strlen("stopscan\n"));
    wifi_marauder_uart_tx(
        app->uart,
        (uint8_t*)("settings -s ChanHop disable\n"),
        strlen("settings -s ChanHop disable\n"));
    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, NULL);
    submenu_reset(app->submenu);
}
