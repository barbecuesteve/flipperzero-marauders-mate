// Marauder's Mate: "AP Spoofing" sub-menu (reached from WiFi).
//
// A second level under WiFi grouping the rogue-AP tools: Evil Portal, Load
// Evil Portal HTML, Beacon Spam, Spoof SSIDs (the SSID list they broadcast),
// and Set AP MAC. It reuses the shared category renderer and scene_start's
// event routing, so item behavior (keyboard/console) is identical.
#include "../wifi_marauder_app_i.h"

void wifi_marauder_scene_spoof_menu_on_enter(void* context) {
    WifiMarauderApp* app = context;
    wifi_marauder_render_category(app, MMCatSpoof);
}

bool wifi_marauder_scene_spoof_menu_on_event(void* context, SceneManagerEvent event) {
    // Same routing as the per-category renderer (keyboard/console/etc.); the
    // shared enter callback and s_render_cat make this correct for the sub-menu.
    return wifi_marauder_scene_start_on_event(context, event);
}

void wifi_marauder_scene_spoof_menu_on_exit(void* context) {
    WifiMarauderApp* app = context;
    variable_item_list_reset(app->var_item_list);
}
