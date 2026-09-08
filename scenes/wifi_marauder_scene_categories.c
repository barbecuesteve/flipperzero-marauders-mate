// Marauder's Mate: top-level protocol category menu (app root).
//
// Rows: Device Info (-> its scene) and the four protocol sections (WiFi,
// Bluetooth, GPS, System) which open the per-category renderer (scene_start,
// filtered by app->menu_category). On first entry, briefly probes `sniffbt` to
// learn whether the board has a BT radio, and greys the Bluetooth row if not.
#include "../wifi_marauder_app_i.h"

#define CAT_VAL_DEVINFO (100u) // distinct from the MMMenuCategory row values
#define MM_BT_PROBE_TICKS (12) // ~1.2s window to catch "Bluetooth not supported"

static int s_bt_probe_ticks;
static bool s_bt_probing;

static void wifi_marauder_categories_rx_cb(uint8_t* buf, size_t len, void* context) {
    WifiMarauderApp* app = context;
    furi_stream_buffer_send(app->scan_stream, buf, len, 0);
}

static void wifi_marauder_categories_item_cb(void* context, uint32_t index) {
    WifiMarauderApp* app = context;
    if(index == CAT_VAL_DEVINFO) {
        view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventStartDeviceInfo);
    } else {
        app->menu_category = (MMMenuCategory)index;
        view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventOpenCategory);
    }
}

static void wifi_marauder_categories_build(WifiMarauderApp* app) {
    Submenu* submenu = app->submenu;
    uint32_t sel = submenu_get_selected_item(submenu);
    submenu_reset(submenu);
    submenu_set_header(submenu, "Marauder's Mate");
    submenu_add_item(
        submenu, "Device Info", CAT_VAL_DEVINFO, wifi_marauder_categories_item_cb, app);
    submenu_add_item(submenu, "WiFi", MMCatWifi, wifi_marauder_categories_item_cb, app);
    submenu_add_item(
        submenu,
        app->bt_state == MMBtNo ? "Bluetooth (no HW)" : "Bluetooth",
        MMCatBluetooth,
        wifi_marauder_categories_item_cb,
        app);
    submenu_add_item(submenu, "GPS", MMCatGps, wifi_marauder_categories_item_cb, app);
    submenu_add_item(submenu, "System", MMCatSystem, wifi_marauder_categories_item_cb, app);
    submenu_set_selected_item(submenu, sel);
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

    // Probe BT support once per session so the Bluetooth row can be greyed.
    if(app->uart && app->bt_state == MMBtUnknown && !s_bt_probing) {
        s_bt_probing = true;
        s_bt_probe_ticks = 0;
        furi_stream_buffer_reset(app->scan_stream);
        furi_string_reset(app->scan_line);
        wifi_marauder_uart_set_handle_rx_data_cb(app->uart, wifi_marauder_categories_rx_cb);
        wifi_marauder_uart_set_handle_rx_pcap_cb(app->uart, NULL);
        wifi_marauder_uart_tx(app->uart, (uint8_t*)"sniffbt\n", strlen("sniffbt\n"));
    }
}

static void wifi_marauder_categories_finish_probe(WifiMarauderApp* app, MMBtState result) {
    app->bt_state = result;
    s_bt_probing = false;
    wifi_marauder_uart_tx(app->uart, (uint8_t*)"stopscan\n", strlen("stopscan\n"));
    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, NULL);
    wifi_marauder_categories_build(app); // refresh the Bluetooth label
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
        if(s_bt_probing) {
            uint8_t tmp[129];
            size_t got;
            while((got = furi_stream_buffer_receive(app->scan_stream, tmp, sizeof(tmp) - 1, 0)) >
                  0) {
                mm_sanitize_nuls(tmp, got);
                tmp[got] = '\0';
                furi_string_cat_str(app->scan_line, (const char*)tmp);
            }
            if(mm_bt_line_unsupported(furi_string_get_cstr(app->scan_line))) {
                wifi_marauder_categories_finish_probe(app, MMBtNo);
            } else if(++s_bt_probe_ticks >= MM_BT_PROBE_TICKS) {
                // No "not supported" within the window -> assume BT works.
                wifi_marauder_categories_finish_probe(app, MMBtYes);
            }
        }
        consumed = true;
    } else if(event.type == SceneManagerEventTypeBack) {
        // Root menu: Back exits the app.
        if(s_bt_probing) wifi_marauder_categories_finish_probe(app, app->bt_state);
        scene_manager_stop(app->scene_manager);
        view_dispatcher_stop(app->view_dispatcher);
        consumed = true;
    }

    return consumed;
}

void wifi_marauder_scene_categories_on_exit(void* context) {
    WifiMarauderApp* app = context;
    if(s_bt_probing) {
        s_bt_probing = false;
        wifi_marauder_uart_tx(app->uart, (uint8_t*)"stopscan\n", strlen("stopscan\n"));
        wifi_marauder_uart_set_handle_rx_data_cb(app->uart, NULL);
    }
    submenu_reset(app->submenu);
}
