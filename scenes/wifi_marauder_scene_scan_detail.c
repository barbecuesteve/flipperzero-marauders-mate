// Marauder's Mate: scanall AP detail + actions scene.
//
// A submenu: the SSID is the header, BSSID/CH/RSSI are info rows, then a
// "Stations (N)" drill-down and the AP-targeted actions. Actions target the
// exact AP via its resolved `select -a <index>`; if the index could not be
// resolved unambiguously the AP is shown but not targetable.
#include "../wifi_marauder_app_i.h"
#include <flipper_format/flipper_format.h>

// Look up a plaintext WiFi password for `ssid` in the optional networks file.
// FlipperFormat, read as sequential SSID/Pass pairs:
//   SSID: HomeNet
//   Pass: correcthorse
// Returns true and copies the password into pass_out on an exact SSID match.
// The password is handled only in RAM here; it is never logged.
static bool mm_lookup_network_password(
    WifiMarauderApp* app, const char* ssid, char* pass_out, size_t pass_sz) {
    if(!ssid || ssid[0] == '\0') return false;
    FlipperFormat* ff = flipper_format_file_alloc(app->storage);
    bool found = false;
    if(flipper_format_file_open_existing(ff, MM_NETWORKS_FILEPATH)) {
        FuriString* name = furi_string_alloc();
        FuriString* pass = furi_string_alloc();
        while(flipper_format_read_string(ff, "SSID", name)) {
            if(!flipper_format_read_string(ff, "Pass", pass)) break;
            if(furi_string_equal_str(name, ssid)) {
                strncpy(pass_out, furi_string_get_cstr(pass), pass_sz - 1);
                pass_out[pass_sz - 1] = '\0';
                found = true;
                break;
            }
        }
        furi_string_free(name);
        furi_string_free(pass);
    }
    flipper_format_free(ff);
    return found;
}

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

    if(!targetable) {
        FURI_LOG_I(
            "MM-TGT",
            "ap not targetable: scan_idx=%d bssid=%s ssid=%s hidden=%d resolved=%d",
            app->scan_selected,
            ap->bssid,
            ap->hidden ? "(hidden)" : ap->ssid,
            ap->hidden,
            app->scan_resolved_index[app->scan_selected]);
    }

    submenu_set_header(submenu, ap->hidden ? "[Hidden AP]" : ap->ssid);

    // Connected indicator (info row) when our last successful join was this AP.
    bool connected_here =
        app->wifi_connected && strcmp(app->connected_bssid, ap->bssid) == 0;
    if(connected_here) {
        submenu_add_item(
            submenu, "* Connected (joined)", MM_D_INFO, wifi_marauder_scan_detail_item_cb, app);
    }

    char line[40];
    snprintf(line, sizeof(line), "CH %d   %d dBm", ap->channel, ap->rssi);
    submenu_add_item(submenu, line, MM_D_INFO, wifi_marauder_scan_detail_item_cb, app);
    submenu_add_item(submenu, ap->bssid, MM_D_INFO, wifi_marauder_scan_detail_item_cb, app);

    char sta_label[48];
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

    // Start the cursor on the first actionable row (Stations). Selection is by
    // item VALUE, so target MM_D_STATIONS directly (independent of how many info
    // rows, incl. the optional Connected row, precede it).
    submenu_set_selected_item(submenu, MM_D_STATIONS);

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
            int resolved = app->scan_resolved_index[app->scan_selected];
            MMScanAp* jap = &app->scan_aps[app->scan_selected];

            // If the network's password is in the optional networks file, join
            // straight away without a keyboard prompt (password never shown).
            char pass[64];
            bool auto_join = !jap->hidden &&
                             mm_lookup_network_password(app, jap->ssid, pass, sizeof(pass));
            // SSID + BSSID of the AP being joined (for the result screen and the
            // connected-state indicator).
            strncpy(app->join_target, jap->hidden ? "[hidden]" : jap->ssid,
                    sizeof(app->join_target) - 1);
            app->join_target[sizeof(app->join_target) - 1] = '\0';
            strncpy(app->join_bssid, jap->bssid, sizeof(app->join_bssid) - 1);
            app->join_bssid[sizeof(app->join_bssid) - 1] = '\0';

            if(auto_join) {
                app->join_ssid[0] = '\0'; // already stored: no save prompt
                snprintf(
                    app->join_cmd, sizeof(app->join_cmd), "join -a %d -p %s", resolved, pass);
                memset(pass, 0, sizeof(pass)); // don't leave the password on the stack
                app->selected_tx_string = app->join_cmd;
                app->is_command = true;
                app->is_custom_tx_string = false;
                app->focus_console_start = false;
                app->show_stopscan_tip = false;
                app->script = NULL;
                scene_manager_next_scene(app->scene_manager, WifiMarauderSceneJoin);
            } else {
                // No stored password: reuse the text-input flow: prefill
                // "join -a <idx> -p" and let the keyboard append the password.
                // Remember the SSID so the text-input scene can offer to save it.
                strncpy(app->join_ssid, jap->ssid, sizeof(app->join_ssid) - 1);
                app->join_ssid[sizeof(app->join_ssid) - 1] = '\0';
                if(jap->hidden) app->join_ssid[0] = '\0'; // hidden: nothing to key on
                snprintf(app->ap_cmd_buf, sizeof(app->ap_cmd_buf), "join -a %d -p", resolved);
                app->selected_tx_string = app->ap_cmd_buf;
                app->is_command = true;
                app->is_custom_tx_string = false;
                app->focus_console_start = false;
                app->show_stopscan_tip = false;
                app->script = NULL;
                scene_manager_next_scene(app->scene_manager, WifiMarauderSceneTextInput);
            }
            consumed = true;
        } else if(event.event == WifiMarauderEventScanHosts) {
            app->host_state = MMHostScanning; // force a fresh sweep
            scene_manager_next_scene(app->scene_manager, WifiMarauderSceneHostScan);
            consumed = true;
        } else if(event.event == WifiMarauderEventScanAction) {
            int resolved = app->scan_resolved_index[app->scan_selected];
            if(resolved < 0) return true; // guard: not targetable

            mm_select_target(app, resolved, -1); // AP only, clearing any prior selection

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
