// Marauder's Mate: AP attack sub-menu.
//
// Drilled into from AP Detail (the "Attacks" row). Groups the offensive
// operations against the selected AP: Deauth / CSA / Quiet (attacks) plus
// Sniff / PMKID (capture). All target the exact AP via its resolved
// `select -a <index>`, done here before the command runs.
#include "../wifi_marauder_app_i.h"

enum {
    MM_AA_DEAUTH = 1200,
    MM_AA_CSA,
    MM_AA_QUIET,
    MM_AA_SNIFF,
    MM_AA_PMKID,
};

// The AP-targeted command chosen; run via the shared select+console path.
static const char* s_ap_cmd;

static void wifi_marauder_ap_attacks_item_cb(void* context, uint32_t index) {
    WifiMarauderApp* app = context;
    switch(index) {
    case MM_AA_DEAUTH:
        s_ap_cmd = "attack -t deauth";
        break;
    case MM_AA_CSA:
        s_ap_cmd = "attack -t csa";
        break;
    case MM_AA_QUIET:
        s_ap_cmd = "attack -t quiet";
        break;
    case MM_AA_SNIFF:
        s_ap_cmd = "sniffraw";
        break;
    case MM_AA_PMKID:
        s_ap_cmd = "sniffpmkid";
        break;
    default:
        return;
    }
    view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventScanAction);
}

void wifi_marauder_scene_ap_attacks_on_enter(void* context) {
    WifiMarauderApp* app = context;
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);

    MMScanAp* ap = &app->scan_aps[app->scan_selected];
    submenu_set_header(submenu, ap->hidden ? "[Hidden AP]" : ap->ssid);

    submenu_add_item(submenu, "Deauth", MM_AA_DEAUTH, wifi_marauder_ap_attacks_item_cb, app);
    submenu_add_item(submenu, "CSA", MM_AA_CSA, wifi_marauder_ap_attacks_item_cb, app);
    submenu_add_item(submenu, "Quiet", MM_AA_QUIET, wifi_marauder_ap_attacks_item_cb, app);
    submenu_add_item(submenu, "Sniff", MM_AA_SNIFF, wifi_marauder_ap_attacks_item_cb, app);
    submenu_add_item(submenu, "PMKID", MM_AA_PMKID, wifi_marauder_ap_attacks_item_cb, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, WifiMarauderAppViewSubmenu);
}

bool wifi_marauder_scene_ap_attacks_on_event(void* context, SceneManagerEvent event) {
    WifiMarauderApp* app = context;
    bool consumed = false;

    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event == WifiMarauderEventScanAction) {
        int resolved = app->scan_resolved_index[app->scan_selected];
        if(resolved < 0) return true; // guard: not targetable

        mm_select_target(app, resolved, -1); // AP only, clearing any prior selection

        app->selected_tx_string = s_ap_cmd ? s_ap_cmd : "attack -t deauth";
        app->is_command = true;
        app->is_custom_tx_string = false;
        app->focus_console_start = false;
        app->show_stopscan_tip = true;
        scene_manager_next_scene(app->scene_manager, WifiMarauderSceneConsoleOutput);
        consumed = true;
    }

    return consumed;
}

void wifi_marauder_scene_ap_attacks_on_exit(void* context) {
    WifiMarauderApp* app = context;
    submenu_reset(app->submenu);
}
