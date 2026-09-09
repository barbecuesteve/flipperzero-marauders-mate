// Marauder's Mate: generic WiFi sub-menu (AP Spoofing / Air Attacks).
//
// A second level under WiFi. app->sub_category selects which group to render
// (MMCatSpoof: Evil Portal / beacon / SSID list / AP MAC; MMCatAir: broadcast
// attacks). Reuses the shared category renderer and scene_start's event
// routing, so item behavior (keyboard/console) is identical.
#include "../wifi_marauder_app_i.h"

void wifi_marauder_scene_spoof_menu_on_enter(void* context) {
    WifiMarauderApp* app = context;
    wifi_marauder_render_category(app, app->sub_category);
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
