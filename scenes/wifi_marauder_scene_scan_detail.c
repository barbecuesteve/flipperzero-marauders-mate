// Marauder's Mate: scanall AP detail + actions scene.
//
// A submenu: the SSID is the header, BSSID/CH/RSSI are info rows, then a
// "Stations (N)" drill-down and the AP-targeted actions. Actions target the
// exact AP via its resolved `select -a <index>`; if the index could not be
// resolved unambiguously the AP is shown but not targetable.
#include "../wifi_marauder_app_i.h"

// Submenu item ids (arbitrary, distinct from any store index).
enum {
    MM_D_INFO = 1000, // non-actionable info rows
    MM_D_STATIONS,
    MM_D_FOXHUNT,
    MM_D_JOIN,
    MM_D_HOSTS,
    MM_D_DEAUTH,
    MM_D_SNIFF,
    MM_D_PMKID,
};

static const char* const MM_CMD_DEAUTH = "attack -t deauth";
static const char* const MM_CMD_SNIFF = "sniffraw";
static const char* const MM_CMD_PMKID = "sniffpmkid";

static void wifi_marauder_scan_detail_item_cb(void* context, uint32_t index) {
    WifiMarauderApp* app = context;
    switch(index) {
    case MM_D_STATIONS:
        view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventScanStations);
        break;
    case MM_D_FOXHUNT:
        view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventScanFoxHunt);
        break;
    case MM_D_JOIN:
        view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventScanJoin);
        break;
    case MM_D_HOSTS:
        view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventScanHosts);
        break;
    case MM_D_DEAUTH:
        app->ap_action = MMApActionDeauth;
        view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventScanAction);
        break;
    case MM_D_SNIFF:
        app->ap_action = MMApActionSniff;
        view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventScanAction);
        break;
    case MM_D_PMKID:
        app->ap_action = MMApActionPmkid;
        view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventScanAction);
        break;
    default:
        break; // info rows: no-op
    }
}

void wifi_marauder_scene_scan_detail_on_enter(void* context) {
    WifiMarauderApp* app = context;
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);

    MMScanAp* ap = &app->scan_aps[app->scan_selected];
    int clients = app->scan_clients[app->scan_selected];
    bool targetable = app->scan_resolved_index[app->scan_selected] >= 0;

    submenu_set_header(submenu, ap->hidden ? "[Hidden AP]" : ap->ssid);

    char line[40];
    snprintf(line, sizeof(line), "CH %d   %d dBm", ap->channel, ap->rssi);
    submenu_add_item(submenu, line, MM_D_INFO, wifi_marauder_scan_detail_item_cb, app);
    submenu_add_item(submenu, ap->bssid, MM_D_INFO, wifi_marauder_scan_detail_item_cb, app);

    char sta_label[24];
    snprintf(sta_label, sizeof(sta_label), "Stations (%d)", clients);
    submenu_add_item(submenu, sta_label, MM_D_STATIONS, wifi_marauder_scan_detail_item_cb, app);

    if(targetable) {
        submenu_add_item(
            submenu, "Fox Hunt", MM_D_FOXHUNT, wifi_marauder_scan_detail_item_cb, app);
        submenu_add_item(submenu, "Join (L3)", MM_D_JOIN, wifi_marauder_scan_detail_item_cb, app);
        submenu_add_item(
            submenu, "Host Scan (L3)", MM_D_HOSTS, wifi_marauder_scan_detail_item_cb, app);
        submenu_add_item(submenu, "Deauth", MM_D_DEAUTH, wifi_marauder_scan_detail_item_cb, app);
        submenu_add_item(submenu, "Sniff", MM_D_SNIFF, wifi_marauder_scan_detail_item_cb, app);
        submenu_add_item(submenu, "PMKID", MM_D_PMKID, wifi_marauder_scan_detail_item_cb, app);
    } else {
        submenu_add_item(
            submenu, "(not targetable)", MM_D_INFO, wifi_marauder_scan_detail_item_cb, app);
    }

    // Start the cursor on the first actionable row (Stations), not an info row.
    submenu_set_selected_item(submenu, 2);

    view_dispatcher_switch_to_view(app->view_dispatcher, WifiMarauderAppViewSubmenu);
}

bool wifi_marauder_scene_scan_detail_on_event(void* context, SceneManagerEvent event) {
    WifiMarauderApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == WifiMarauderEventScanStations) {
            scene_manager_next_scene(app->scene_manager, WifiMarauderSceneStaList);
            consumed = true;
        } else if(event.event == WifiMarauderEventScanFoxHunt) {
            MMScanAp* ap = &app->scan_aps[app->scan_selected];
            app->fox_is_station = false;
            app->fox_ap_arg = app->scan_resolved_index[app->scan_selected];
            strncpy(
                app->fox_title, ap->hidden ? "[Hidden]" : ap->ssid, sizeof(app->fox_title) - 1);
            app->fox_title[sizeof(app->fox_title) - 1] = '\0';
            scene_manager_next_scene(app->scene_manager, WifiMarauderSceneFoxHunt);
            consumed = true;
        } else if(event.event == WifiMarauderEventScanJoin) {
            // Reuse the generic text-input flow: prefill "join -a <idx> -p" and
            // let the keyboard append the password (typed on the Flipper).
            int resolved = app->scan_resolved_index[app->scan_selected];
            snprintf(app->ap_cmd_buf, sizeof(app->ap_cmd_buf), "join -a %d -p", resolved);
            app->selected_tx_string = app->ap_cmd_buf;
            app->is_command = true;
            app->is_custom_tx_string = false;
            app->focus_console_start = false;
            app->show_stopscan_tip = false;
            app->script = NULL;
            scene_manager_next_scene(app->scene_manager, WifiMarauderSceneTextInput);
            consumed = true;
        } else if(event.event == WifiMarauderEventScanHosts) {
            app->host_state = MMHostScanning; // force a fresh sweep
            scene_manager_next_scene(app->scene_manager, WifiMarauderSceneHostScan);
            consumed = true;
        } else if(event.event == WifiMarauderEventScanAction) {
            int resolved = app->scan_resolved_index[app->scan_selected];
            if(resolved < 0) return true; // guard: not targetable

            snprintf(app->ap_cmd_buf, sizeof(app->ap_cmd_buf), "select -a %d\n", resolved);
            wifi_marauder_uart_tx(app->uart, (uint8_t*)app->ap_cmd_buf, strlen(app->ap_cmd_buf));

            switch(app->ap_action) {
            case MMApActionDeauth:
                app->selected_tx_string = MM_CMD_DEAUTH;
                break;
            case MMApActionSniff:
                app->selected_tx_string = MM_CMD_SNIFF;
                break;
            case MMApActionPmkid:
                app->selected_tx_string = MM_CMD_PMKID;
                break;
            default:
                app->selected_tx_string = MM_CMD_DEAUTH;
                break;
            }

            app->is_command = true;
            app->is_custom_tx_string = false;
            app->focus_console_start = false;
            app->show_stopscan_tip = true;
            app->script = NULL;

            scene_manager_next_scene(app->scene_manager, WifiMarauderSceneConsoleOutput);
            consumed = true;
        }
    }

    return consumed;
}

void wifi_marauder_scene_scan_detail_on_exit(void* context) {
    WifiMarauderApp* app = context;
    submenu_reset(app->submenu);
}
