// Marauder's Mate: station list scene (targeted client deauth).
//
// Lists the client MACs associated with the selected AP (from list -c, deduped,
// multicast filtered, each carrying its select -c index). Selecting a client
// deauthenticates just that client: select the AP and the station, then
// `attack -t deauth -c` (Marauder only attacks a station whose AP is also
// selected). Stations without a resolved select -c index (e.g. captured live
// before list -c ran) are shown but not actionable.
#include "../wifi_marauder_app_i.h"


static void wifi_marauder_sta_list_item_cb(void* context, uint32_t index) {
    WifiMarauderApp* app = context;
    if((int)index >= app->scan_station_count) return; // "no clients" placeholder
    app->sta_selected = (int)index;
    view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventStaSelected);
}

void wifi_marauder_scene_sta_list_on_enter(void* context) {
    WifiMarauderApp* app = context;
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);

    MMScanAp* ap = &app->scan_aps[app->scan_selected];
    char header[32];
    snprintf(header, sizeof(header), "%s clients", ap->hidden ? "[Hidden]" : ap->ssid);
    submenu_set_header(submenu, header);

    int shown = 0;
    for(int s = 0; s < app->scan_station_count; s++) {
        if(app->scan_stations[s].ap_index != app->scan_selected) continue;
        submenu_add_item(
            submenu,
            app->scan_stations[s].mac,
            (uint32_t)s,
            wifi_marauder_sta_list_item_cb,
            app);
        shown++;
    }
    if(shown == 0) {
        // Index >= scan_station_count marks a non-actionable placeholder.
        submenu_add_item(
            submenu, "No clients seen", (uint32_t)app->scan_station_count,
            wifi_marauder_sta_list_item_cb, app);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, WifiMarauderAppViewSubmenu);
}

bool wifi_marauder_scene_sta_list_on_event(void* context, SceneManagerEvent event) {
    WifiMarauderApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom && event.event == WifiMarauderEventStaSelected) {
        scene_manager_next_scene(app->scene_manager, WifiMarauderSceneStaDetail);
        consumed = true;
    }

    return consumed;
}

void wifi_marauder_scene_sta_list_on_exit(void* context) {
    WifiMarauderApp* app = context;
    submenu_reset(app->submenu);
}
