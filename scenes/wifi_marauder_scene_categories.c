// Marauder's Mate: top-level protocol category menu (app root).
//
// Rows: Device Info (-> its scene) and the four protocol sections (WiFi,
// Bluetooth, GPS, System) which open the per-category renderer (scene_start,
// filtered by app->menu_category).
//
// Launch capability probe (assume-absent, prove-present): on first entry we
// send `info` and parse the reply for board presence, SD card, Bluetooth and
// GPS, then grey the sections that aren't usable. On custom firmware `info`
// reports Bluetooth directly; on stock firmware it doesn't, so when BT is still
// unknown after the info window we fall back to the old `sniffbt` probe.
#include "../wifi_marauder_app_i.h"

#define CAT_VAL_DEVINFO (100u) // distinct from the MMMenuCategory row values
#define MM_SETTLE_TICKS (4) // ~0.4s to flush stale scan/boot noise before info
#define MM_INFO_PROBE_TICKS (15) // ~1.5s window to collect the info reply
#define MM_BT_PROBE_TICKS (12) // ~1.2s window to catch "Bluetooth not supported"
#define MM_PROBE_CAP (1024) // cap probe accumulation; a BT scan can stream forever

// Capabilities come from `info` when the firmware reports them (custom build).
// For Bluetooth on stock firmware there is no info line, so we fall back to
// `sniffbt -serial`: scan/attack commands are gated behind !scanning() and only
// emit to serial with -serial, so we first send `stopscan` (settle) to idle the
// ESP, then look for "Bluetooth not supported" (absent -> BT works).
typedef enum {
    MMProbeIdle,
    MMProbeSettle, // stopscan + flush, so later commands actually dispatch
    MMProbeInfo, // collecting the `info` reply
    MMProbeBt, // sniffbt -serial fallback (stock firmware, no BT line in info)
} MMProbePhase;

static MMProbePhase s_phase;
static int s_probe_ticks;

static void wifi_marauder_categories_item_cb(void* context, uint32_t index);

static void wifi_marauder_categories_rx_cb(uint8_t* buf, size_t len, void* context) {
    WifiMarauderApp* app = context;
    furi_stream_buffer_send(app->scan_stream, buf, len, 0);
}

// True when a category row is selectable given the detected capabilities.
static bool wifi_marauder_cat_enabled(WifiMarauderApp* app, MMMenuCategory cat) {
    if(app->device_state != MMDevPresent) return false; // no board -> all greyed
    if(cat == MMCatBluetooth) return app->bt_state != MMBtNo;
    if(cat == MMCatGps) return app->gps_state != MMCapNo;
    return true; // WiFi / System always usable when a board is present
}

// Row label, annotated when the section is greyed so the reason is visible.
static void
    wifi_marauder_cat_label(WifiMarauderApp* app, MMMenuCategory cat, const char* base, char* out,
                            size_t out_sz) {
    const char* suffix = "";
    if(app->device_state == MMDevUnknown) {
        suffix = " \xE2\x80\xA6"; // ellipsis: still detecting
    } else if(app->device_state == MMDevAbsent) {
        suffix = " (no device)";
    } else if(cat == MMCatBluetooth && app->bt_state == MMBtNo) {
        suffix = " (no HW)";
    } else if(cat == MMCatGps && app->gps_state == MMCapNo) {
        suffix = " (none)";
    }
    snprintf(out, out_sz, "%s%s", base, suffix);
}

static void wifi_marauder_categories_build(WifiMarauderApp* app) {
    Submenu* submenu = app->submenu;
    uint32_t sel = submenu_get_selected_item(submenu);
    submenu_reset(submenu);
    submenu_set_header(submenu, "Marauder's Mate");

    // Device Info is always live -- it's how you re-detect after a hotplug.
    submenu_add_item(
        submenu, "Device Info", CAT_VAL_DEVINFO, wifi_marauder_categories_item_cb, app);

    static const struct {
        const char* name;
        MMMenuCategory cat;
    } rows[] = {
        {"WiFi", MMCatWifi},
        {"Bluetooth", MMCatBluetooth},
        {"GPS", MMCatGps},
        {"System", MMCatSystem},
    };
    char label[32];
    for(size_t i = 0; i < COUNT_OF(rows); ++i) {
        wifi_marauder_cat_label(app, rows[i].cat, rows[i].name, label, sizeof(label));
        submenu_add_item(
            submenu, label, rows[i].cat, wifi_marauder_categories_item_cb, app);
    }
    submenu_set_selected_item(submenu, sel);
}

static void wifi_marauder_categories_item_cb(void* context, uint32_t index) {
    WifiMarauderApp* app = context;
    if(index == CAT_VAL_DEVINFO) {
        view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventStartDeviceInfo);
        return;
    }
    // Greyed sections are inert: selecting them does nothing.
    if(!wifi_marauder_cat_enabled(app, (MMMenuCategory)index)) return;
    app->menu_category = (MMMenuCategory)index;
    view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventOpenCategory);
}

static void wifi_marauder_categories_send_info(WifiMarauderApp* app) {
    s_phase = MMProbeInfo;
    s_probe_ticks = 0;
    furi_stream_buffer_reset(app->scan_stream); // drop the flushed noise
    furi_string_reset(app->scan_line);
    wifi_marauder_uart_tx(app->uart, (uint8_t*)"info\n", strlen("info\n"));
}

static void wifi_marauder_categories_start_probe(WifiMarauderApp* app) {
    // Two problems to clear before `info`: (1) boot/settling noise may sit in the
    // ESP's line buffer with no newline, so a bare "info\n" would append to it and
    // be parsed as one garbage command; (2) scan/attack commands are gated behind
    // !scanning(), so a scan left running from a prior session silently swallows
    // our probes. `stopscan` (plus the CR) fixes both: it idles the ESP and
    // terminates any dangling line. We drain/discard the reply for a few ticks.
    s_phase = MMProbeSettle;
    s_probe_ticks = 0;
    furi_stream_buffer_reset(app->scan_stream);
    furi_string_reset(app->scan_line);
    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, wifi_marauder_categories_rx_cb);
    wifi_marauder_uart_set_handle_rx_pcap_cb(app->uart, NULL);
    wifi_marauder_uart_tx(app->uart, (uint8_t*)"stopscan\r\n", strlen("stopscan\r\n"));
}

static void wifi_marauder_categories_start_bt_probe(WifiMarauderApp* app) {
    // `info` leaves currentScanMode = SHOW_INFO, so scanning() is true and the
    // scan/attack commands (gated behind !scanning()) would be skipped. Send
    // stopscan first to return to WIFI_SCAN_OFF, then sniffbt. -serial is
    // required for the reply (incl. "Bluetooth not supported") to reach the UART.
    s_phase = MMProbeBt;
    s_probe_ticks = 0;
    furi_stream_buffer_reset(app->scan_stream);
    furi_string_reset(app->scan_line);
    wifi_marauder_uart_tx(
        app->uart,
        (uint8_t*)"stopscan\r\nsniffbt -serial\n",
        strlen("stopscan\r\nsniffbt -serial\n"));
}

static void wifi_marauder_categories_end_probe(WifiMarauderApp* app) {
    if(s_phase == MMProbeBt) {
        wifi_marauder_uart_tx(app->uart, (uint8_t*)"stopscan\n", strlen("stopscan\n"));
    }
    s_phase = MMProbeIdle;
    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, NULL);
    FURI_LOG_I(
        "MM-CAP",
        "probe end: device=%d sd=%d bt=%d gps=%d upload=%d dual=%d",
        app->device_state,
        app->sd_state,
        app->bt_state,
        app->gps_state,
        app->direct_upload_state,
        app->dual_band_state);
    wifi_marauder_categories_build(app); // apply greying
}

static void wifi_marauder_categories_drain(WifiMarauderApp* app) {
    uint8_t tmp[129];
    size_t got;
    while((got = furi_stream_buffer_receive(app->scan_stream, tmp, sizeof(tmp) - 1, 0)) > 0) {
        // Always drain the stream (so it can't back up), but cap what we keep:
        // a BT scan on a capable board streams without end and would exhaust the
        // heap. The tokens we look for arrive well within the cap.
        if(furi_string_size(app->scan_line) >= MM_PROBE_CAP) continue;
        mm_sanitize_nuls(tmp, got);
        tmp[got] = '\0';
        furi_string_cat_str(app->scan_line, (const char*)tmp);
    }
}

void wifi_marauder_scene_categories_on_enter(void* context) {
    WifiMarauderApp* app = context;
    wifi_marauder_categories_build(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiMarauderAppViewSubmenu);

    // First run: prompt to pick pcap/log save settings (moved here from start).
    if(app->need_to_prompt_settings_init) {
        scene_manager_next_scene(app->scene_manager, WifiMarauderSceneSettingsInit);
        return;
    }

    // Probe capabilities once per session (assume-absent until info replies).
    if(app->uart && app->device_state == MMDevUnknown && s_phase == MMProbeIdle) {
        wifi_marauder_categories_start_probe(app);
    }
}

bool wifi_marauder_scene_categories_on_event(void* context, SceneManagerEvent event) {
    WifiMarauderApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == WifiMarauderEventOpenCategory) {
            scene_manager_next_scene(app->scene_manager, WifiMarauderSceneStart);
            consumed = true;
        } else if(event.event == WifiMarauderEventStartDeviceInfo) {
            scene_manager_next_scene(app->scene_manager, WifiMarauderSceneDeviceInfo);
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        if(s_phase == MMProbeSettle) {
            wifi_marauder_categories_drain(app); // discard boot/settling noise
            if(++s_probe_ticks >= MM_SETTLE_TICKS) {
                wifi_marauder_categories_send_info(app);
            }
        } else if(s_phase == MMProbeInfo) {
            wifi_marauder_categories_drain(app);
            if(++s_probe_ticks >= MM_INFO_PROBE_TICKS) {
                mm_apply_info_caps(app, furi_string_get_cstr(app->scan_line));
                // Custom firmware reports BT in info; stock doesn't -> fall back.
                if(app->device_state == MMDevPresent && app->bt_state == MMBtUnknown) {
                    wifi_marauder_categories_start_bt_probe(app);
                } else {
                    wifi_marauder_categories_end_probe(app);
                }
            }
        } else if(s_phase == MMProbeBt) {
            wifi_marauder_categories_drain(app);
            if(mm_bt_line_unsupported(furi_string_get_cstr(app->scan_line))) {
                app->bt_state = MMBtNo;
                wifi_marauder_categories_end_probe(app);
            } else if(++s_probe_ticks >= MM_BT_PROBE_TICKS) {
                app->bt_state = MMBtYes; // no "not supported" -> assume BT works
                wifi_marauder_categories_end_probe(app);
            }
        }
        consumed = true;
    } else if(event.type == SceneManagerEventTypeBack) {
        // Root menu: Back exits the app.
        if(s_phase != MMProbeIdle) wifi_marauder_categories_end_probe(app);
        scene_manager_stop(app->scene_manager);
        view_dispatcher_stop(app->view_dispatcher);
        consumed = true;
    }

    return consumed;
}

void wifi_marauder_scene_categories_on_exit(void* context) {
    WifiMarauderApp* app = context;
    if(s_phase != MMProbeIdle) {
        if(s_phase == MMProbeBt) {
            wifi_marauder_uart_tx(app->uart, (uint8_t*)"stopscan\n", strlen("stopscan\n"));
        }
        s_phase = MMProbeIdle;
        wifi_marauder_uart_set_handle_rx_data_cb(app->uart, NULL);
    }
    submenu_reset(app->submenu);
}
