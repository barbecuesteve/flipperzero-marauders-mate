// Marauder's Mate: live scanall scene.
//
// clearlist -a -> scanall (streaming, deduped by BSSID, growing list with
// per-AP client counts) -> stopscan -> list -a (to resolve select indices).
//
// The RX worker thread pushes raw bytes into a thread-safe FuriStreamBuffer;
// the GUI tick drains it, assembles complete lines, and updates the store on
// the GUI thread -- so no lock is needed and no line is lost across the
// worker/GUI boundary.
#include "../wifi_marauder_app_i.h"
#include <ctype.h>

#define MM_LIVE_LIST_TICKS (15) // wait for each of list -a / list -c (~1.5s)
#define MM_ITEM_RESCAN (0xFFFFFFFFu)

static bool mm_mac_is_multicast(const char* mac) {
    // multicast/broadcast if the low bit of the first octet is set
    char b[3] = {mac[0], mac[1], '\0'};
    char* end;
    long v = strtol(b, &end, 16);
    if(end == b) return false;
    return (v & 0x01) != 0;
}

static void wifi_marauder_scan_live_rx_cb(uint8_t* buf, size_t len, void* context) {
    WifiMarauderApp* app = context;
    furi_stream_buffer_send(app->scan_stream, buf, len, 0);
}

static void wifi_marauder_scan_live_tx(WifiMarauderApp* app, const char* cmd) {
    wifi_marauder_uart_tx(app->uart, (uint8_t*)cmd, strlen(cmd));
}

static void wifi_marauder_scan_live_item_cb(void* context, uint32_t index) {
    WifiMarauderApp* app = context;
    if(index == MM_ITEM_RESCAN) {
        view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventScanRescan);
        return;
    }
    app->scan_selected = (int)index;
    view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventScanApSelected);
}

// Display order -> store index. GUI-thread-only; lets the on-screen list be
// sorted while each row still carries its true store index to the callback.
static int s_order[MM_AP_MAX];

static int mm_ci_cmp(const char* a, const char* b) {
    while(*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if(ca != cb) return ca - cb;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

// Named APs alphabetically (case-insensitive); hidden APs after, by BSSID.
static int mm_ap_cmp(const MMScanAp* A, const MMScanAp* B) {
    if(A->hidden != B->hidden) return A->hidden ? 1 : -1;
    if(A->hidden) return strcmp(A->bssid, B->bssid);
    return mm_ci_cmp(A->ssid, B->ssid);
}

static void wifi_marauder_scan_live_select_store(WifiMarauderApp* app, int store_idx);

// Log any GUI-thread operation that runs long enough to risk a ViewPort
// lockup. Info level but very low volume (only fires past the threshold).
#define MM_PERF_TAG "MM-PERF"
#define MM_PERF_MS 8

static void wifi_marauder_scan_live_rebuild(WifiMarauderApp* app, bool sorted) {
    uint32_t t0 = furi_get_tick();
    // Preserve the cursor across the re-sort. submenu get/set operate on the
    // item VALUE (what we pass to add_item), not the row position -- and our
    // value IS the store index, which is stable as APs arrive and rows shuffle.
    // So capturing and restoring the value keeps the cursor on the same AP.
    uint32_t sel_value = submenu_get_selected_item(app->submenu);
    submenu_reset(app->submenu);

    char header[48];
    snprintf(header, sizeof(header), "Live APs: %d", app->scan_ap_count);
    submenu_set_header(app->submenu, header);

    for(int i = 0; i < app->scan_ap_count; i++) s_order[i] = i;
    if(sorted) {
        // insertion sort on the order[] indices (count <= MM_AP_MAX, tiny)
        for(int i = 1; i < app->scan_ap_count; i++) {
            int v = s_order[i];
            int j = i - 1;
            while(j >= 0 && mm_ap_cmp(&app->scan_aps[s_order[j]], &app->scan_aps[v]) > 0) {
                s_order[j + 1] = s_order[j];
                j--;
            }
            s_order[j + 1] = v;
        }
    }

    char label[96];
    for(int k = 0; k < app->scan_ap_count; k++) {
        int i = s_order[k]; // store index for this display row
        MMScanAp* a = &app->scan_aps[i];
        const char* name = a->hidden ? "[hidden]" : a->ssid;
        if(app->scan_clients[i] > 0) {
            snprintf(
                label, sizeof(label), "%s C%d %d %dc", name, a->channel, a->rssi,
                app->scan_clients[i]);
        } else {
            snprintf(label, sizeof(label), "%s C%d %d", name, a->channel, a->rssi);
        }
        // Callback value is the STORE index, not the display row, so sorting
        // never disturbs the select-index mapping.
        submenu_add_item(app->submenu, label, (uint32_t)i, wifi_marauder_scan_live_item_cb, app);
    }
    submenu_add_item(
        app->submenu, "> Rescan", MM_ITEM_RESCAN, wifi_marauder_scan_live_item_cb, app);

    // Restore the cursor to the same AP (by value); harmlessly a no-op if that
    // value is gone. The "> Rescan" row's value is preserved too.
    submenu_set_selected_item(app->submenu, sel_value);

    uint32_t dt = furi_get_tick() - t0;
    if(dt >= MM_PERF_MS)
        FURI_LOG_D(
            MM_PERF_TAG, "rebuild n=%d sorted=%d %lums", app->scan_ap_count, sorted,
            (unsigned long)dt);
}

// Move the cursor to the AP with store index `store_idx`. The submenu selects
// by item VALUE, and each row's value is its store index, so this is direct.
static void wifi_marauder_scan_live_select_store(WifiMarauderApp* app, int store_idx) {
    if(store_idx >= 0) submenu_set_selected_item(app->submenu, (uint32_t)store_idx);
}

// Parse one scanall line into the store. Returns true if the display changed.
static bool wifi_marauder_scan_live_process_line(WifiMarauderApp* app, const char* line) {
    MMScanLineType type = mm_scanall_classify(line);
    if(type == MMScanLineAp) {
        MMScanAp cur;
        if(!mm_scanall_parse_ap(line, &cur)) return false;
        bool is_new = false;
        int idx = mm_scan_store_upsert(app->scan_aps, &app->scan_ap_count, MM_AP_MAX, &cur, &is_new);
        if(idx >= 0 && is_new) {
            app->scan_clients[idx] = 0;
            app->scan_resolved_index[idx] = -1;
            return true;
        }
        return false; // re-sighting (rssi update) -- no structural change
    } else if(type == MMScanLineStation) {
        char bssid[MM_BSSID_LEN], sta[MM_BSSID_LEN];
        if(!mm_scanall_parse_station(line, bssid, sta)) return false;
        if(mm_mac_is_multicast(sta)) return false; // not a real client

        // Find the AP this station associates with.
        int ap_idx = -1;
        for(int i = 0; i < app->scan_ap_count; i++) {
            if(strcmp(app->scan_aps[i].bssid, bssid) == 0) {
                ap_idx = i;
                break;
            }
        }
        if(ap_idx < 0) return false; // AP not (yet) in the store

        // Dedup per (AP, station); only a new pair bumps the client count.
        for(int s = 0; s < app->scan_station_count; s++) {
            if(app->scan_stations[s].ap_index == ap_idx &&
               strcmp(app->scan_stations[s].mac, sta) == 0) {
                return false;
            }
        }
        if(app->scan_station_count < MM_STA_MAX) {
            strcpy(app->scan_stations[app->scan_station_count].mac, sta);
            app->scan_stations[app->scan_station_count].ap_index = ap_idx;
            app->scan_stations[app->scan_station_count].sel_index = -1; // set from list -c
            app->scan_station_count++;
        }
        app->scan_clients[ap_idx]++; // unique clients
        return true;
    }
    return false;
}

// Drain the stream buffer; during scanning, assemble and process complete
// lines; during listing, accumulate raw text for the list -a parse.
static void wifi_marauder_scan_live_drain(WifiMarauderApp* app) {
    uint8_t tmp[129];
    size_t got;
    while((got = furi_stream_buffer_receive(app->scan_stream, tmp, sizeof(tmp) - 1, 0)) > 0) {
        mm_sanitize_nuls(tmp, got); // hidden-AP ESSID padding is 0x00, not spaces
        tmp[got] = '\0';
        if(app->scan_live_state == MMLiveListing ||
           app->scan_live_state == MMLiveListingClients) {
            furi_string_cat_str(app->ap_scan_buffer, (const char*)tmp);
        } else {
            furi_string_cat_str(app->scan_line, (const char*)tmp);
        }
    }

    if(app->scan_live_state == MMLiveListing ||
       app->scan_live_state == MMLiveListingClients)
        return;

    uint32_t t0 = furi_get_tick();
    bool changed = false;
    int processed = 0;
    for(;;) {
        const char* cstr = furi_string_get_cstr(app->scan_line);
        const char* nl = strchr(cstr, '\n');
        if(!nl) break;
        size_t len = (size_t)(nl - cstr);
        char line[160];
        size_t cpy = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
        memcpy(line, cstr, cpy);
        line[cpy] = '\0';
        if(wifi_marauder_scan_live_process_line(app, line)) changed = true;
        furi_string_right(app->scan_line, len + 1); // drop the line + newline
        if(++processed >= 24) break; // bound GUI-thread work per tick
    }
    // Don't rebuild the whole submenu here (10x/s of full re-alloc stalls the
    // GUI thread and drops UART data); just mark dirty and let the tick throttle.
    if(changed) app->scan_dirty = true;

    uint32_t dt = furi_get_tick() - t0;
    if(dt >= MM_PERF_MS)
        FURI_LOG_D(MM_PERF_TAG, "drain lines=%d %lums", processed, (unsigned long)dt);
}

static void wifi_marauder_scan_live_start(WifiMarauderApp* app) {
    app->scan_ap_count = 0;
    app->scan_station_count = 0;
    app->scan_live_state = MMLiveScanning;
    app->scan_live_ticks = 0;
    app->scan_dirty = false;
    app->scan_aps_built = 0;
    app->scan_resolve_to_detail = false;
    app->sel_ap_prev = -1; // clearlist -a below wipes Marauder's selections
    app->sel_sta_prev = -1;
    furi_stream_buffer_reset(app->scan_stream);
    furi_string_reset(app->scan_line);
    furi_string_reset(app->ap_scan_buffer);

    submenu_reset(app->submenu);
    submenu_set_header(app->submenu, "Live scan...");

    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, wifi_marauder_scan_live_rx_cb);
    wifi_marauder_uart_set_handle_rx_pcap_cb(app->uart, NULL);

    wifi_marauder_scan_live_tx(app, "clearlist -a\n"); // clean slate: discovery order == index
    wifi_marauder_scan_live_tx(app, "scanall\n");
}

// Resume streaming without clearing the ESP's list or our store: scanall keeps
// appending. Used when returning to the live view (e.g. back from detail) so
// the list the user built up keeps growing instead of restarting.
static void wifi_marauder_scan_live_resume(WifiMarauderApp* app) {
    app->scan_live_state = MMLiveScanning;
    app->scan_live_ticks = 0;
    app->scan_dirty = false;
    furi_stream_buffer_reset(app->scan_stream);
    furi_string_reset(app->scan_line);
    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, wifi_marauder_scan_live_rx_cb);
    wifi_marauder_uart_set_handle_rx_pcap_cb(app->uart, NULL);
    wifi_marauder_scan_live_tx(app, "scanall\n");
}

// An AP was tapped: stop scanning and run the resolve phase (list -a then
// list -c). The tick handler drives the timed states; on completion it opens
// the detail scene because scan_resolve_to_detail is set.
static void wifi_marauder_scan_live_begin_resolve(WifiMarauderApp* app) {
    wifi_marauder_scan_live_tx(app, "stopscan\n");
    furi_string_reset(app->ap_scan_buffer);
    furi_string_reset(app->scan_line);
    furi_stream_buffer_reset(app->scan_stream);
    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, wifi_marauder_scan_live_rx_cb);
    wifi_marauder_scan_live_tx(app, "list -a\n");
    app->scan_live_state = MMLiveListing;
    app->scan_live_ticks = 0;
    app->scan_resolve_to_detail = true;
    submenu_set_header(app->submenu, "Resolving...");
}

// Parse the captured `list -c` output into the station store, attaching each
// client's select -c index. Authoritative: replaces the scanall estimates.
static void wifi_marauder_scan_live_parse_clients(WifiMarauderApp* app) {
    app->scan_station_count = 0;
    for(int i = 0; i < app->scan_ap_count; i++) app->scan_clients[i] = 0;

    int cur_ap = -1;
    const char* text = furi_string_get_cstr(app->ap_scan_buffer);
    char line[128];
    while(*text) {
        const char* nl = strchr(text, '\n');
        size_t len = nl ? (size_t)(nl - text) : strlen(text);
        size_t cpy = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
        memcpy(line, text, cpy);
        line[cpy] = '\0';

        int ap_i, sel;
        char mac[MM_BSSID_LEN];
        if(mm_listc_parse_ap_header(line, &ap_i)) {
            cur_ap = ap_i;
        } else if(mm_listc_parse_station(line, &sel, mac)) {
            if(cur_ap >= 0 && cur_ap < app->scan_ap_count && !mm_mac_is_multicast(mac) &&
               app->scan_station_count < MM_STA_MAX) {
                MMStation* st = &app->scan_stations[app->scan_station_count++];
                strcpy(st->mac, mac);
                st->ap_index = cur_ap;
                st->sel_index = sel;
                app->scan_clients[cur_ap]++;
            }
        }
        if(!nl) break;
        text = nl + 1;
    }
}

void wifi_marauder_scene_scan_live_on_enter(void* context) {
    WifiMarauderApp* app = context;
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiMarauderAppViewSubmenu);

    // scan_resume_pending is set only when we navigate out to the detail scene,
    // so it is true exactly when we are coming back from detail: show the
    // accumulated list and resume scanning where we left off. Any other entry
    // (from the menu) starts a fresh scan.
    if(app->scan_resume_pending) {
        app->scan_resume_pending = false;
        wifi_marauder_scan_live_rebuild(app, true);
        wifi_marauder_scan_live_select_store(app, app->scan_selected);
        wifi_marauder_scan_live_resume(app);
    } else {
        wifi_marauder_scan_live_start(app);
    }
}

bool wifi_marauder_scene_scan_live_on_event(void* context, SceneManagerEvent event) {
    WifiMarauderApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == WifiMarauderEventScanApSelected) {
            // Only act on a tap while actively scanning; ignore stray taps that
            // land during the resolve phase.
            if(app->scan_live_state == MMLiveScanning) {
                wifi_marauder_scan_live_begin_resolve(app);
            }
            consumed = true;
        } else if(event.event == WifiMarauderEventScanRescan) {
            wifi_marauder_scan_live_start(app);
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        wifi_marauder_scan_live_drain(app);

        if(app->scan_live_state == MMLiveScanning) {
            // A full submenu rebuild contends with the GUI draw thread for the
            // view-model lock (~8ms/rebuild -> ViewPort lockups), so rebuild as
            // rarely as the UX allows. New APs restructure the list and warrant
            // a prompt (throttled ~2/s) rebuild; station-count-only changes do
            // not, so they get at most a slow (~3s) refresh. list -c fills the
            // authoritative counts when the scan ends regardless.
            bool aps_grew = app->scan_ap_count > app->scan_aps_built;
            bool prompt = aps_grew && (app->scan_live_ticks % 5 == 0);
            bool slow = app->scan_dirty && (app->scan_live_ticks % 30 == 0);
            if(prompt || slow) {
                wifi_marauder_scan_live_rebuild(app, true);
                app->scan_aps_built = app->scan_ap_count;
                app->scan_dirty = false;
            }
            // scanall runs continuously the whole time the user is on this view;
            // the resolve phase is deferred to an AP tap (begin_resolve). Keep
            // counting ticks for the rebuild throttle.
            app->scan_live_ticks++;
        } else if(app->scan_live_state == MMLiveListing) {
            if(++app->scan_live_ticks >= MM_LIVE_LIST_TICKS) {
                uint32_t t0 = furi_get_tick();
                wifi_marauder_uart_set_handle_rx_data_cb(app->uart, NULL);
                wifi_marauder_scan_live_drain(app); // flush any remaining list -a text

                // Static (BSS), not stack: MM_AP_MAX MMAccessPoints is ~3KB,
                // which would overflow the app's thread stack. Safe because the
                // tick handler is single-threaded and non-reentrant.
                static MMAccessPoint list_a[MM_AP_MAX];
                int lac = (int)mm_ap_parse_buffer(
                    furi_string_get_cstr(app->ap_scan_buffer), list_a, MM_AP_MAX);
                for(int i = 0; i < app->scan_ap_count; i++) {
                    app->scan_resolved_index[i] =
                        mm_resolve_select_index(&app->scan_aps[i], i, list_a, lac);
                }

                // Now pull list -c to attach stations with their select -c index.
                furi_string_reset(app->ap_scan_buffer);
                wifi_marauder_uart_set_handle_rx_data_cb(
                    app->uart, wifi_marauder_scan_live_rx_cb);
                wifi_marauder_scan_live_tx(app, "list -c\n");
                app->scan_live_state = MMLiveListingClients;
                app->scan_live_ticks = 0;

                uint32_t dt = furi_get_tick() - t0;
                if(dt >= MM_PERF_MS)
                    FURI_LOG_D(
                        MM_PERF_TAG, "resolve n=%d %lums", app->scan_ap_count,
                        (unsigned long)dt);
            }
        } else if(app->scan_live_state == MMLiveListingClients) {
            if(++app->scan_live_ticks >= MM_LIVE_LIST_TICKS) {
                uint32_t t0 = furi_get_tick();
                wifi_marauder_uart_set_handle_rx_data_cb(app->uart, NULL);
                wifi_marauder_scan_live_drain(app); // flush remaining list -c text
                wifi_marauder_scan_live_parse_clients(app);
                app->scan_live_state = MMLiveReady;
                uint32_t dt = furi_get_tick() - t0;
                if(dt >= MM_PERF_MS)
                    FURI_LOG_D(
                        MM_PERF_TAG, "parse_clients+final %lums", (unsigned long)dt);
                if(app->scan_resolve_to_detail) {
                    // The resolve was kicked off by an AP tap: open its detail.
                    // The store must survive the round-trip so detail/sta_list
                    // can read it; mark that our next on_enter is a resume, and
                    // never wipe the store in on_exit.
                    app->scan_resolve_to_detail = false;
                    app->scan_resume_pending = true;
                    scene_manager_next_scene(app->scene_manager, WifiMarauderSceneScanDetail);
                } else {
                    wifi_marauder_scan_live_rebuild(app, true); // timed internally
                }
            }
        }
        consumed = true;
    }

    return consumed;
}

void wifi_marauder_scene_scan_live_on_exit(void* context) {
    WifiMarauderApp* app = context;
    // Stop any scan/resolve still in flight, but do NOT wipe the store here: the
    // store must survive the round-trip to detail/sta_list. Fresh-vs-resume is
    // decided by scan_resume_pending in on_enter (start() clears the store on a
    // fresh entry), so nothing here needs to reset counts.
    if(app->scan_live_state != MMLiveReady) {
        wifi_marauder_scan_live_tx(app, "stopscan\n");
    }
    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, NULL);
    submenu_reset(app->submenu);
}
