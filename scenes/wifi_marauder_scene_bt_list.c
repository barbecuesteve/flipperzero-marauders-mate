// Marauder's Mate: live BLE device list (ESP32-C5).
//
// Runs `sniffbt` (BT_SCAN_ALL) and builds a deduped, selectable device list
// that mirrors the Wi-Fi AP list UX. The firmware glues every record onto one
// continuous line ("<rssi> Device: <name>..."), so this consumes the stream up
// to the last complete record each tick (mm_btall_parse_buffer), upserts each
// sighting into a BSSID/name-keyed store (best-effort: the BT_SCAN_ALL stream
// carries only rssi + name/MAC), and rebuilds the submenu strongest-first.
// Selecting a device opens its detail scene.
#include "../wifi_marauder_app_i.h"
#include <ctype.h>

#define MM_BT_TAG "MM-BT"
// A device not re-sighted within this many ticks (~100ms each) ages out.
#define MM_BT_STALE_TICKS 100 // ~10s
// Safety cap on the accumulation buffer if markers stop arriving. Kept small:
// the whole loaded .fap (text+data+bss) shares one contiguous RAM block, and
// large statics here starve the runtime heap (-> out-of-memory on load).
#define MM_BT_SCAN_BUF 512

// Set when we leave for the detail scene, so the next on_enter resumes (keeps
// the accumulated device store) instead of starting a fresh scan. Any other
// exit (back to the menu) leaves it false -> fresh scan next time.
static bool s_bt_resume;

static void wifi_marauder_bt_list_rx_cb(uint8_t* buf, size_t len, void* context) {
    WifiMarauderApp* app = context;
    furi_stream_buffer_send(app->scan_stream, buf, len, 0);
}

// Upsert one sighting into the store, keyed on the advertised name/MAC string.
static void mm_bt_upsert(WifiMarauderApp* app, const MMBtDevice* d) {
    for(int i = 0; i < app->bt_device_count; i++) {
        if(strcmp(app->bt_devices[i].name, d->name) == 0) {
            app->bt_devices[i].rssi = d->rssi; // most recent
            app->bt_devices[i].is_mac = d->is_mac;
            app->bt_seen[i]++;
            app->bt_last_tick[i] = app->bt_ticks;
            return;
        }
    }
    int idx;
    if(app->bt_device_count < MM_BT_MAX) {
        idx = app->bt_device_count++;
    } else {
        // Full: evict the stalest slot so fresh sightings aren't starved.
        idx = 0;
        for(int i = 1; i < app->bt_device_count; i++)
            if(app->bt_last_tick[i] < app->bt_last_tick[idx]) idx = i;
    }
    app->bt_devices[idx] = *d;
    app->bt_seen[idx] = 1;
    app->bt_last_tick[idx] = app->bt_ticks;
}

static void wifi_marauder_bt_list_item_cb(void* context, uint32_t index) {
    WifiMarauderApp* app = context;
    if((int)index >= app->bt_device_count) return; // "scanning" placeholder
    app->bt_selected = (int)index;
    view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventBtSelected);
}

static void wifi_marauder_bt_list_rebuild(WifiMarauderApp* app) {
    uint32_t sel_value = submenu_get_selected_item(app->submenu);
    submenu_reset(app->submenu);

    char header[32];
    snprintf(header, sizeof(header), "BLE devices: %d", app->bt_device_count);
    submenu_set_header(app->submenu, header);

    // Stable discovery order: devices keep their row as RSSI fluctuates, so the
    // highlighted device doesn't shuffle under the cursor. The row value is the
    // store index, so the cursor (restored by value below) stays on the same
    // device; only the RSSI number in the label updates in place.
    char label[64];
    for(int i = 0; i < app->bt_device_count; i++) {
        const MMBtDevice* d = &app->bt_devices[i];
        snprintf(label, sizeof(label), "%d %s", d->rssi, d->name);
        submenu_add_item(app->submenu, label, (uint32_t)i, wifi_marauder_bt_list_item_cb, app);
    }
    if(app->bt_device_count == 0) {
        submenu_add_item(
            app->submenu, "Scanning...", (uint32_t)MM_BT_MAX, wifi_marauder_bt_list_item_cb, app);
    }
    submenu_set_selected_item(app->submenu, sel_value);
}

// Consume complete records from the accumulation buffer. The trailing (still
// growing) record is left in place; its leading rssi stays with it.
static bool wifi_marauder_bt_list_consume(WifiMarauderApp* app) {
    const char* cstr = furi_string_get_cstr(app->scan_line);
    // Find the last " Device: " marker; everything before it is complete.
    const char* marker = NULL;
    for(const char* p = cstr; (p = strstr(p, " Device: ")) != NULL; p++) marker = p;
    if(!marker) return false;

    // Walk left over the pending record's rssi token so it stays in the buffer.
    long L = (long)(marker - cstr);
    long i = L - 1;
    while(i >= 0 && cstr[i] == ' ') i--;
    long digend = i;
    while(i >= 0 && isdigit((unsigned char)cstr[i])) i--;
    if(i >= 0 && cstr[i] == '-') i--;
    long S = (i == digend) ? L : (i + 1); // no rssi token -> split at the marker
    if(S <= 0) return false; // nothing complete yet

    // Copy the complete prefix into a bounded scratch buffer (no strndup on the
    // target libc) and parse it. If S somehow exceeds the scratch, clamp at a
    // marker-free byte count; the remainder is re-consumed next tick.
    static char head[MM_BT_SCAN_BUF + 256];
    size_t cpy = (size_t)S < sizeof(head) - 1 ? (size_t)S : sizeof(head) - 1;
    memcpy(head, cstr, cpy);
    head[cpy] = '\0';

    static MMBtDevice tmp[MM_BT_MAX];
    size_t n = mm_btall_parse_buffer(head, tmp, MM_BT_MAX);
    for(size_t k = 0; k < n; k++) mm_bt_upsert(app, &tmp[k]);
    furi_string_right(app->scan_line, cpy); // drop consumed prefix
    return n > 0;
}

void wifi_marauder_scene_bt_list_on_enter(void* context) {
    WifiMarauderApp* app = context;

    bool resume = s_bt_resume;
    s_bt_resume = false;

    if(!resume) app->bt_device_count = 0; // fresh entry wipes the store
    app->bt_ticks = 0;
    furi_stream_buffer_reset(app->scan_stream);
    furi_string_reset(app->scan_line);

    // Populate the submenu BEFORE switching to it, so the first painted frame
    // already shows the list (avoids a flicker from an empty intermediate frame
    // on the way back from detail).
    if(resume) {
        // Returning from detail: show the list we already built and keep the
        // cursor on the device the user drilled into.
        wifi_marauder_bt_list_rebuild(app);
        submenu_set_selected_item(app->submenu, (uint32_t)app->bt_selected);
    } else {
        submenu_reset(app->submenu);
        submenu_set_header(app->submenu, "Scanning BLE...");
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiMarauderAppViewSubmenu);

    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, wifi_marauder_bt_list_rx_cb);
    wifi_marauder_uart_set_handle_rx_pcap_cb(app->uart, NULL);
    wifi_marauder_uart_tx(app->uart, (uint8_t*)"sniffbt\n", strlen("sniffbt\n"));
    FURI_LOG_I(MM_BT_TAG, resume ? "BLE list: resume sniffbt" : "BLE list: sniffbt");
}

bool wifi_marauder_scene_bt_list_on_event(void* context, SceneManagerEvent event) {
    WifiMarauderApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom && event.event == WifiMarauderEventBtSelected) {
        s_bt_resume = true; // keep the store across the round-trip to detail
        scene_manager_next_scene(app->scene_manager, WifiMarauderSceneBtDetail);
        consumed = true;
    } else if(event.type == SceneManagerEventTypeTick) {
        uint8_t tmp[129];
        size_t got;
        while((got = furi_stream_buffer_receive(app->scan_stream, tmp, sizeof(tmp) - 1, 0)) > 0) {
            mm_sanitize_nuls(tmp, got);
            tmp[got] = '\0';
            furi_string_cat_str(app->scan_line, (const char*)tmp);
        }

        bool changed = wifi_marauder_bt_list_consume(app);
        // Bound the buffer if markers somehow stop arriving (one giant record).
        if(furi_string_size(app->scan_line) > MM_BT_SCAN_BUF) {
            furi_string_right(app->scan_line, furi_string_size(app->scan_line) - 512);
        }

        app->bt_ticks++;
        // Rebuild ~2 Hz (or right after new devices arrive); a full submenu
        // re-alloc every tick would stall the GUI thread.
        if(changed || app->bt_ticks % 5 == 0) wifi_marauder_bt_list_rebuild(app);
        consumed = true;
    }

    return consumed;
}

void wifi_marauder_scene_bt_list_on_exit(void* context) {
    WifiMarauderApp* app = context;
    wifi_marauder_uart_tx(app->uart, (uint8_t*)"stopscan\n", strlen("stopscan\n"));
    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, NULL);
    submenu_reset(app->submenu);
}
