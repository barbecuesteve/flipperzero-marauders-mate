//** Includes sniffbt and sniffskim for compatible ESP32-WROOM hardware.
//wifi_marauder_app_i.h also changed **//
#include "../wifi_marauder_app_i.h"

// For each command, define whether additional arguments are needed
// (enabling text input to fill them out), and whether the console
// text box should focus at the start of the output or the end
typedef enum { NO_ARGS = 0, INPUT_ARGS, TOGGLE_ARGS } InputArgs;

typedef enum { FOCUS_CONSOLE_END = 0, FOCUS_CONSOLE_START, FOCUS_CONSOLE_TOGGLE } FocusConsole;

#define SHOW_STOPSCAN_TIP (true)
#define NO_TIP (false)

#define MAX_OPTIONS (17)
typedef struct {
    const char* item_string;
    const char* options_menu[MAX_OPTIONS];
    int num_options_menu;
    const char* actual_commands[MAX_OPTIONS];
    InputArgs needs_keyboard;
    FocusConsole focus_console;
    bool show_stopscan_tip;
    MMMenuCategory category; // which protocol section this item lives in
} WifiMarauderItem;

// NUM_MENU_ITEMS defined in wifi_marauder_app_i.h - if you add an entry here, increment it!
const WifiMarauderItem items[NUM_MENU_ITEMS] = {
    {"Live Scan (Mate)", {""}, 1, {"scanlive"}, NO_ARGS, FOCUS_CONSOLE_END, NO_TIP, MMCatWifi},
    {"Beacon Mon (Mate)", {""}, 1, {"beaconmon"}, NO_ARGS, FOCUS_CONSOLE_END, NO_TIP, MMCatWifi},
    {"Probe Mon (Mate)", {""}, 1, {"probemon"}, NO_ARGS, FOCUS_CONSOLE_END, NO_TIP, MMCatWifi},
    {"Spoof SSIDs", // the SSID list broadcast by Beacon Spam / Evil Portal
     {"add rand", "add name", "remove"},
     3,
     {"ssid -a -g", "ssid -a -n", "ssid -r"},
     INPUT_ARGS,
     FOCUS_CONSOLE_START,
     NO_TIP,
     MMCatSpoof},
    {"View SSIDs", {""}, 1, {"list -s"}, NO_ARGS, FOCUS_CONSOLE_START, NO_TIP, MMCatSpoof},
    {"Clear SSIDs", {""}, 1, {"clearlist -s"}, NO_ARGS, FOCUS_CONSOLE_END, NO_TIP, MMCatSpoof},
    {"AirTags", {""}, 1, {"list -t"}, NO_ARGS, FOCUS_CONSOLE_START, NO_TIP, MMCatBluetooth},
    {"BT Devices", {""}, 1, {"list -b"}, NO_ARGS, FOCUS_CONSOLE_START, NO_TIP, MMCatBluetooth},
    {"Select",
     {"ap", "ssid", "station"},
     3,
     {"select -a", "select -s", "select -c"},
     INPUT_ARGS,
     FOCUS_CONSOLE_END,
     NO_TIP,
     MMCatWifi},
    {"Set STA MAC",
     {"rand", "clone"},
     2,
     {"randstamac", "clonestamac -s"},
     TOGGLE_ARGS,
     FOCUS_CONSOLE_END,
     NO_TIP,
     MMCatWifi},
    {"AP Spoofing", {""}, 1, {"spoofmenu"}, NO_ARGS, FOCUS_CONSOLE_END, NO_TIP, MMCatWifi},
    {"Set AP MAC",
     {"rand", "clone"},
     2,
     {"randapmac", "cloneapmac -a"},
     TOGGLE_ARGS,
     FOCUS_CONSOLE_END,
     NO_TIP,
     MMCatSpoof},
    // AP-targeted attacks (deauth/CSA/quiet) live on the AP detail screen;
    // client-targeted (deauth/badmsg/sleep -c) on the station detail screen.
    // These are the untargeted/broadcast attacks -- their own sub-menu.
    {"Air Attacks", {""}, 1, {"airmenu"}, NO_ARGS, FOCUS_CONSOLE_END, NO_TIP, MMCatWifi},
    {"Probe Spam", {""}, 1, {"attack -t probe"}, NO_ARGS, FOCUS_CONSOLE_END, SHOW_STOPSCAN_TIP,
     MMCatAir},
    {"Rickroll Beacons", {""}, 1, {"attack -t rickroll"}, NO_ARGS, FOCUS_CONSOLE_END,
     SHOW_STOPSCAN_TIP, MMCatAir},
    {"Funny SSIDs", {""}, 1, {"attack -t funny"}, NO_ARGS, FOCUS_CONSOLE_END, SHOW_STOPSCAN_TIP,
     MMCatAir},
    {"SAE Flood", {""}, 1, {"attack -t sae"}, NO_ARGS, FOCUS_CONSOLE_END, SHOW_STOPSCAN_TIP,
     MMCatAir},
    {"Karma", {""}, 1, {"karma -p"}, INPUT_ARGS, FOCUS_CONSOLE_END, SHOW_STOPSCAN_TIP, MMCatAir},
    {"Manual Deauth", {""}, 1, {"attack -t deauth -s"}, INPUT_ARGS, FOCUS_CONSOLE_END,
     SHOW_STOPSCAN_TIP, MMCatAir},
    {"BT Spam",
     {"sour apple",
      "apple juice",
      "swiftpair spam",
      "samsung spam",
      "google spam",
      "flipper spam",
      "bt spam all"},
     7,
     {"blespam -t sourapple",
      "blespam -t applejuice",
      "blespam -t windows",
      "blespam -t samsung",
      "blespam -t google",
      "blespam -t flipper",
      "blespam -t all"},
     NO_ARGS,
     FOCUS_CONSOLE_END,
     SHOW_STOPSCAN_TIP,
     MMCatBluetooth},
    {"Airtag",
     {"spoof", "sound"},
     2,
     {"spoofat -t", "findmy -t"},
     INPUT_ARGS,
     FOCUS_CONSOLE_END,
     NO_TIP,
     MMCatBluetooth},
    {"Wardrive",
     {""},
     1,
     {"wardrive"},
     NO_ARGS,
     FOCUS_CONSOLE_END,
     SHOW_STOPSCAN_TIP,
     MMCatGps},
    {"Upload Wardrive",
     {"wdg", "wigle", "both"},
     3,
     {"upload -d wdg", "upload -d wigle", "upload -d both"},
     NO_ARGS,
     FOCUS_CONSOLE_END,
     SHOW_STOPSCAN_TIP,
     MMCatGps},
    {"Evil Portal",
     {"start", "set html", "set AP"},
     3,
     {"evilportal -c start", "evilportal -c sethtml", "evilportal -c setap"},
     TOGGLE_ARGS,
     FOCUS_CONSOLE_END,
     SHOW_STOPSCAN_TIP,
     MMCatSpoof},
    {"Load Evil Portal HTML file",
     {""},
     1,
     {"evilportal -c sethtmlstr"},
     NO_ARGS,
     FOCUS_CONSOLE_END,
     NO_TIP,
     MMCatSpoof},
    {"Beacon Spam",
     {"ap list", "ssid list", "random"},
     3,
     {"attack -t beacon -a", "attack -t beacon -l", "attack -t beacon -r"},
     NO_ARGS,
     FOCUS_CONSOLE_END,
     SHOW_STOPSCAN_TIP,
     MMCatSpoof},
    {"WiFi Sniff",
     {"deauth", "pmkid", "pwn", "raw", "mactrack", "packetcount", "pineapple", "multissid", "sae"},
     9,
     {"sniffdeauth",
      "sniffpmkid",
      "sniffpwn",
      "sniffraw",
      "mactrack",
      "packetcount",
      "sniffpinescan",
      "sniffmultissid",
      "sniffsae"},
     NO_ARGS,
     FOCUS_CONSOLE_END,
     SHOW_STOPSCAN_TIP,
     MMCatWifi},
    {"BT Sniff",
     {"bt", "skim", "airtag", "flipper", "flock", "meta"},
     6,
     {"sniffbt",
      "sniffskim",
      "sniffbt -t airtag",
      "sniffbt -t flipper",
      "sniffbt -t flock",
      "sniffbt -t meta"},
     NO_ARGS,
     FOCUS_CONSOLE_END,
     SHOW_STOPSCAN_TIP,
     MMCatBluetooth},
    {"Channel",
     {"get", "set"},
     2,
     {"channel", "channel -s"},
     TOGGLE_ARGS,
     FOCUS_CONSOLE_END,
     NO_TIP,
     MMCatWifi},
    {"LED",
     {"hex", "pattern"},
     2,
     {"led -s", "led -p"},
     INPUT_ARGS,
     FOCUS_CONSOLE_END,
     NO_TIP,
     MMCatSystem},
    {"GPS Data",
     {"tracker", "stream", "fix", "sats", "lat", "lon", "alt", "date", "accuracy", "text", "nmea"},
     11,
     {"gps -t",
      "gpsdata",
      "gps -g fix",
      "gps -g sat",
      "gps -g lat",
      "gps -g lon",
      "gps -g alt",
      "gps -g date",
      "gps -g accuracy",
      "gps -g text",
      "gps -g nmea"},
     NO_ARGS,
     FOCUS_CONSOLE_END,
     SHOW_STOPSCAN_TIP,
     MMCatGps},
    {"NMEA Stream", {""}, 1, {"nmea"}, NO_ARGS, FOCUS_CONSOLE_END, NO_TIP, MMCatGps},
    {"GPS POI",
     {"start", "mark", "end"},
     3,
     {"gpspoi -s", "gpspoi -m", "gpspoi -e"},
     NO_ARGS,
     FOCUS_CONSOLE_END,
     NO_TIP,
     MMCatGps},
    {"Settings",
     {"display", "restore", "ForcePMKID", "ForceProbe", "SavePCAP", "EnableLED", "EPDeauth", "other"},
     8,
     {"settings",
      "settings -r",
      "settings -s ForcePMKID enable",
      "settings -s ForceProbe enable",
      "settings -s SavePCAP enable",
      "settings -s EnableLED enable",
      "settings -s EPDeauth enable",
      "settings -s"},
     TOGGLE_ARGS,
     FOCUS_CONSOLE_START,
     NO_TIP,
     MMCatSystem},
    {"Shutdown WiFi", {""}, 1, {"stopscan -f"}, NO_ARGS, FOCUS_CONSOLE_START, NO_TIP, MMCatWifi},
    {"List SD", {""}, 1, {"ls /"}, INPUT_ARGS, FOCUS_CONSOLE_END, NO_TIP, MMCatSystem},
    {"Update", {"sd"}, 1, {"update -s"}, NO_ARGS, FOCUS_CONSOLE_END, NO_TIP, MMCatSystem},
    {"Reboot", {""}, 1, {"reboot"}, NO_ARGS, FOCUS_CONSOLE_END, NO_TIP, MMCatSystem},
    {"Help", {""}, 1, {"help"}, NO_ARGS, FOCUS_CONSOLE_START, SHOW_STOPSCAN_TIP, MMCatSystem},
    {"Info", {""}, 1, {"info"}, NO_ARGS, FOCUS_CONSOLE_START, NO_TIP, MMCatSystem},
    {"View Log from",
     {"start", "end"},
     2,
     {"", ""},
     NO_ARGS,
     FOCUS_CONSOLE_TOGGLE,
     NO_TIP,
     MMCatSystem},
    {"Scripts", {""}, 1, {""}, NO_ARGS, FOCUS_CONSOLE_END, NO_TIP, MMCatSystem},
    {"Save to flipper sdcard",
     {""},
     1,
     {""},
     NO_ARGS,
     FOCUS_CONSOLE_START,
     NO_TIP,
     MMCatSystem},
};

// Category renderer: the var list shows only items whose category matches
// app->menu_category. s_row_to_flat maps a displayed row back to its flat
// items[] index; s_row_count is how many rows are shown.
static int s_row_to_flat[NUM_MENU_ITEMS];
static int s_row_count;
static MMMenuCategory s_render_cat; // category currently shown (WiFi..System or Spoof)

static void wifi_marauder_scene_start_var_list_enter_callback(void* context, uint32_t row) {
    furi_assert(context);
    WifiMarauderApp* app = context;

    if((int)row >= s_row_count) return; // e.g. the "no BT radio" placeholder
    const int index = s_row_to_flat[row]; // flat items[] index for this row
    const WifiMarauderItem* item = &items[index];

    const int selected_option_index = app->selected_option_index[index];
    furi_assert(selected_option_index < item->num_options_menu);
    app->selected_tx_string = item->actual_commands[selected_option_index];
    // "View Log from" is the only non-command entry (it opens the log viewer /
    // console instead of sending serial). Detect it by name so it can live
    // anywhere in the menu, not just at index 0.
    app->is_command = (strcmp(item->item_string, "View Log from") != 0);
    app->is_custom_tx_string = false;
    app->selected_menu_index = index;
    app->focus_console_start = (item->focus_console == FOCUS_CONSOLE_TOGGLE) ?
                                   (selected_option_index == 0) :
                                   item->focus_console;
    app->show_stopscan_tip = item->show_stopscan_tip;

    if(!app->is_command && selected_option_index == 0) {
        // View Log from start
        view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventStartLogViewer);
        return;
    }

    // Marauder's Mate: live scanall entry
    if(app->selected_tx_string && strcmp(app->selected_tx_string, "scanlive") == 0) {
        view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventStartScanLive);
        return;
    }

    // Marauder's Mate: beacon activity monitor entry
    if(app->selected_tx_string && strcmp(app->selected_tx_string, "beaconmon") == 0) {
        view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventStartBeaconMon);
        return;
    }

    // Marauder's Mate: probe-request monitor entry
    if(app->selected_tx_string && strcmp(app->selected_tx_string, "probemon") == 0) {
        view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventStartProbeMon);
        return;
    }

    // Marauder's Mate: drill into a WiFi sub-menu (AP Spoofing / Air Attacks)
    if(app->selected_tx_string && strcmp(app->selected_tx_string, "spoofmenu") == 0) {
        app->sub_category = MMCatSpoof;
        view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventOpenSpoof);
        return;
    }
    if(app->selected_tx_string && strcmp(app->selected_tx_string, "airmenu") == 0) {
        app->sub_category = MMCatAir;
        view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventOpenSpoof);
        return;
    }

    if(app->selected_tx_string &&
       strncmp("sniffpmkid", app->selected_tx_string, strlen("sniffpmkid")) == 0) {
        // sniffpmkid submenu
        view_dispatcher_send_custom_event(
            app->view_dispatcher, WifiMarauderEventStartSniffPmkidOptions);
        return;
    }

    // Select automation script (detect by name, not position)
    if(strcmp(item->item_string, "Scripts") == 0) {
        view_dispatcher_send_custom_event(
            app->view_dispatcher, WifiMarauderEventStartScriptSelect);
        return;
    }

    if(strcmp(item->item_string, "Save to flipper sdcard") == 0) {
        // start SettingsInit widget
        view_dispatcher_send_custom_event(
            app->view_dispatcher, WifiMarauderEventStartSettingsInit);
        return;
    }

    bool needs_keyboard = (item->needs_keyboard == TOGGLE_ARGS) ? (selected_option_index != 0) :
                                                                  item->needs_keyboard;
    if(needs_keyboard) {
        view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventStartKeyboard);
    } else {
        view_dispatcher_send_custom_event(app->view_dispatcher, WifiMarauderEventStartConsole);
    }
}

static void wifi_marauder_scene_start_var_list_change_callback(VariableItem* item) {
    furi_assert(item);

    WifiMarauderApp* app = variable_item_get_context(item);
    furi_assert(app);

    const WifiMarauderItem* menu_item = &items[app->selected_menu_index];
    uint8_t item_index = variable_item_get_current_value_index(item);
    furi_assert(item_index < menu_item->num_options_menu);
    variable_item_set_current_value_text(item, menu_item->options_menu[item_index]);
    app->selected_option_index[app->selected_menu_index] = item_index;
}

void wifi_marauder_render_category(WifiMarauderApp* app, MMMenuCategory cat) {
    VariableItemList* var_item_list = app->var_item_list;
    s_render_cat = cat; // what the enter/change/tick callbacks act on

    variable_item_list_reset(var_item_list);
    variable_item_list_set_enter_callback(
        var_item_list, wifi_marauder_scene_start_var_list_enter_callback, app);

    // Bluetooth on a board with no BT radio: show a single non-actionable note.
    if(cat == MMCatBluetooth && app->bt_state == MMBtNo) {
        variable_item_list_add(var_item_list, "No BT radio on board", 1, NULL, app);
        s_row_count = 0; // no real rows; enter callback ignores the placeholder
        view_dispatcher_switch_to_view(app->view_dispatcher, WifiMarauderAppViewVarItemList);
        return;
    }

    // Build only the rows for this category; remember each row's flat index.
    s_row_count = 0;
    VariableItem* item;
    for(int i = 0; i < NUM_MENU_ITEMS; ++i) {
        if(items[i].category != cat) continue;
        s_row_to_flat[s_row_count++] = i;
        item = variable_item_list_add(
            var_item_list,
            items[i].item_string,
            items[i].num_options_menu,
            wifi_marauder_scene_start_var_list_change_callback,
            app);
        variable_item_set_current_value_index(item, app->selected_option_index[i]);
        variable_item_set_current_value_text(
            item, items[i].options_menu[app->selected_option_index[i]]);
    }

    // Restore the per-category cursor (a display row).
    if(app->category_cursor[cat] < s_row_count)
        variable_item_list_set_selected_item(var_item_list, app->category_cursor[cat]);

    view_dispatcher_switch_to_view(app->view_dispatcher, WifiMarauderAppViewVarItemList);
}

void wifi_marauder_scene_start_on_enter(void* context) {
    WifiMarauderApp* app = context;
    wifi_marauder_render_category(app, app->menu_category);
}

bool wifi_marauder_scene_start_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    WifiMarauderApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == WifiMarauderEventStartKeyboard) {
            scene_manager_set_scene_state(
                app->scene_manager, WifiMarauderSceneStart, app->selected_menu_index);
            scene_manager_next_scene(app->scene_manager, WifiMarauderSceneTextInput);
        } else if(event.event == WifiMarauderEventStartConsole) {
            scene_manager_set_scene_state(
                app->scene_manager, WifiMarauderSceneStart, app->selected_menu_index);
            scene_manager_next_scene(app->scene_manager, WifiMarauderSceneConsoleOutput);
        } else if(event.event == WifiMarauderEventStartSettingsInit) {
            scene_manager_set_scene_state(
                app->scene_manager, WifiMarauderSceneStart, app->selected_menu_index);
            scene_manager_next_scene(app->scene_manager, WifiMarauderSceneSettingsInit);
        } else if(event.event == WifiMarauderEventStartLogViewer) {
            scene_manager_set_scene_state(
                app->scene_manager, WifiMarauderSceneStart, app->selected_menu_index);
            scene_manager_next_scene(app->scene_manager, WifiMarauderSceneLogViewer);
        } else if(event.event == WifiMarauderEventStartScriptSelect) {
            scene_manager_set_scene_state(
                app->scene_manager, WifiMarauderSceneStart, app->selected_menu_index);
            scene_manager_next_scene(app->scene_manager, WifiMarauderSceneScriptSelect);
        } else if(event.event == WifiMarauderEventStartSniffPmkidOptions) {
            scene_manager_set_scene_state(
                app->scene_manager, WifiMarauderSceneStart, app->selected_menu_index);
            scene_manager_next_scene(app->scene_manager, WifiMarauderSceneSniffPmkidOptions);
        } else if(event.event == WifiMarauderEventStartScanLive) {
            scene_manager_set_scene_state(
                app->scene_manager, WifiMarauderSceneStart, app->selected_menu_index);
            app->scan_live_state = MMLiveScanning; // force a fresh scan
            scene_manager_next_scene(app->scene_manager, WifiMarauderSceneScanLive);
        } else if(event.event == WifiMarauderEventStartBeaconMon) {
            scene_manager_set_scene_state(
                app->scene_manager, WifiMarauderSceneStart, app->selected_menu_index);
            scene_manager_next_scene(app->scene_manager, WifiMarauderSceneBeaconMon);
        } else if(event.event == WifiMarauderEventStartProbeMon) {
            scene_manager_set_scene_state(
                app->scene_manager, WifiMarauderSceneStart, app->selected_menu_index);
            scene_manager_next_scene(app->scene_manager, WifiMarauderSceneProbeMon);
        } else if(event.event == WifiMarauderEventStartDeviceInfo) {
            scene_manager_set_scene_state(
                app->scene_manager, WifiMarauderSceneStart, app->selected_menu_index);
            scene_manager_next_scene(app->scene_manager, WifiMarauderSceneDeviceInfo);
        } else if(event.event == WifiMarauderEventOpenSpoof) {
            scene_manager_next_scene(app->scene_manager, WifiMarauderSceneSpoofMenu);
        }
        consumed = true;
    } else if(event.type == SceneManagerEventTypeTick) {
        int row = variable_item_list_get_selected_item_index(app->var_item_list);
        app->category_cursor[s_render_cat] = row; // s_render_cat: works for spoof sub-menu too
        app->selected_menu_index = (row < s_row_count) ? s_row_to_flat[row] : 0;
        consumed = true;
    }
    // Back is NOT consumed here: let the scene manager pop back to the category
    // menu (this scene is now the per-category renderer, not the app root).

    return consumed;
}

void wifi_marauder_scene_start_on_exit(void* context) {
    WifiMarauderApp* app = context;
    variable_item_list_reset(app->var_item_list);
}
