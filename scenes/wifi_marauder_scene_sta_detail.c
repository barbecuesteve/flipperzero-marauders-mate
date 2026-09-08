// Marauder's Mate: station detail + actions scene.
//
// Actions for the client selected in sta_list:
// - Deauth: select the AP and the station, then `attack -t deauth -c` (Marauder
//   only deauths a station whose AP is also selected).
// - Fox Hunt: `foxhunt -s <ap> <station>` -> the RSSI meter, to physically home
//   in on that specific client device.
// Shown only when both the AP and station indices resolved.
#include "../wifi_marauder_app_i.h"

enum {
    MM_SD_INFO = 900,
    MM_SD_DEAUTH,
    MM_SD_FOXHUNT,
};

static const char* const MM_CMD_DEAUTH_C = "attack -t deauth -c";

static void wifi_marauder_sta_detail_item_cb(void* context, uint32_t index) {
    WifiMarauderApp* app = context;
    if(index == MM_SD_DEAUTH) {
        view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventStaDeauth);
    } else if(index == MM_SD_FOXHUNT) {
        view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventStaFoxHunt);
    }
}

void wifi_marauder_scene_sta_detail_on_enter(void* context) {
    WifiMarauderApp* app = context;
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);

    MMStation* st = &app->scan_stations[app->sta_selected];
    int ap_resolved = app->scan_resolved_index[st->ap_index];
    bool targetable = st->sel_index >= 0 && ap_resolved >= 0;

    if(!targetable) {
        FURI_LOG_I(
            "MM-TGT",
            "sta not targetable: mac=%s ap_index=%d ap_resolved=%d sel_index=%d",
            st->mac,
            st->ap_index,
            ap_resolved,
            st->sel_index);
    }

    submenu_set_header(submenu, st->mac);
    if(targetable) {
        submenu_add_item(submenu, "Deauth", MM_SD_DEAUTH, wifi_marauder_sta_detail_item_cb, app);
        submenu_add_item(
            submenu, "Fox Hunt", MM_SD_FOXHUNT, wifi_marauder_sta_detail_item_cb, app);
    } else {
        submenu_add_item(
            submenu, "(not targetable)", MM_SD_INFO, wifi_marauder_sta_detail_item_cb, app);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, WifiMarauderAppViewSubmenu);
}

bool wifi_marauder_scene_sta_detail_on_event(void* context, SceneManagerEvent event) {
    WifiMarauderApp* app = context;
    bool consumed = false;

    if(event.type != SceneManagerEventTypeCustom) return false;

    MMStation* st = &app->scan_stations[app->sta_selected];
    int ap_resolved = app->scan_resolved_index[st->ap_index];
    if(st->sel_index < 0 || ap_resolved < 0) return true; // can't target cleanly

    if(event.event == WifiMarauderEventStaDeauth) {
        // Targeted client deauth needs both the AP and the station selected;
        // clear any prior selection so only this client is hit.
        mm_select_target(app, ap_resolved, st->sel_index);

        app->selected_tx_string = MM_CMD_DEAUTH_C;
        app->is_command = true;
        app->is_custom_tx_string = false;
        app->focus_console_start = false;
        app->show_stopscan_tip = true;
        app->script = NULL;
        scene_manager_next_scene(app->scene_manager, WifiMarauderSceneConsoleOutput);
        consumed = true;
    } else if(event.event == WifiMarauderEventStaFoxHunt) {
        app->fox_is_station = true;
        app->fox_ap_arg = ap_resolved;
        app->fox_sta_arg = st->sel_index;
        strncpy(app->fox_title, st->mac, sizeof(app->fox_title) - 1);
        app->fox_title[sizeof(app->fox_title) - 1] = '\0';
        scene_manager_next_scene(app->scene_manager, WifiMarauderSceneFoxHunt);
        consumed = true;
    }

    return consumed;
}

void wifi_marauder_scene_sta_detail_on_exit(void* context) {
    WifiMarauderApp* app = context;
    submenu_reset(app->submenu);
}
