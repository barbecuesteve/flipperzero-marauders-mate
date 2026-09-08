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
    {"Scan",
     {"all", "ping", "arp"},
     3,
     {"scanall", "pingscan", "arpscan"},
     NO_ARGS,
     FOCUS_CONSOLE_END,
     SHOW_STOPSCAN_TIP,
     MMCatWifi},
    {"SSID",
     {"add rand", "add name", "remove"},
     3,
     {"ssid -a -g", "ssid -a -n", "ssid -r"},
     INPUT_ARGS,
     FOCUS_CONSOLE_START,
     NO_TIP,
     MMCatWifi},
    {"List",
     {"ap", "ssid", "station", "airtag", "IPs", "probes", "bluetooth"},
     7,
     {"list -a", "list -s", "list -c", "list -t", "list -i", "list -p", "list -b"},
     NO_ARGS,
     FOCUS_CONSOLE_START,
     NO_TIP,
     MMCatWifi},
    {"Select",
     {"ap", "ssid", "station"},
     3,
     {"select -a", "select -s", "select -c"},
     INPUT_ARGS,
     FOCUS_CONSOLE_END,
     NO_TIP,
     MMCatWifi},
    {"Set MAC",
     {"rand ap", "rand sta", "clone ap", "clone sta"},
     4,
     {"randapmac", "randstamac", "cloneapmac -a", "clonestamac -s"},
     TOGGLE_ARGS,
     FOCUS_CONSOLE_END,
     NO_TIP,
     MMCatWifi},
    {"Join WiFi", // "new" (join -a -p) is now Live Scan > AP > Join (L3); keep saved-reconnect
     {"saved"},
     1,
     {"join -s"},
     INPUT_ARGS,
     FOCUS_CONSOLE_END,
     NO_TIP,
     MMCatWifi},
    {"Clear List",
     {"ap", "ssid", "station"},
     3,
     {"clearlist -a", "clearlist -s", "clearlist -c"},
     NO_ARGS,
     FOCUS_CONSOLE_END,
     NO_TIP,
     MMCatWifi},
    {"WiFi Attack",
     {"deauth", "probe", "rickroll", "funny", "badmsg", "sleep", "sae flood", "csa", "quiet"},
     9,
     {"attack -t deauth",
      "attack -t probe",
      "attack -t rickroll",
      "attack -t funny",
      "attack -t badmsg",
      "attack -t sleep",
      "attack -t sae",
      "attack -t csa",
      "attack -t quiet"},
     NO_ARGS,
     FOCUS_CONSOLE_END,
     SHOW_STOPSCAN_TIP,
     MMCatWifi},
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
     MMCatWifi},
    {"Load Evil Portal HTML file",
     {""},
     1,
     {"evilportal -c sethtmlstr"},
     NO_ARGS,
     FOCUS_CONSOLE_END,
     NO_TIP,
     MMCatWifi},
    {"Targeted Attacks", // client deauth -> Live Scan > AP > Stations > Deauth
     {"manual",
      "karma",
      "badmsg",
      "sleep"},
     4,
     {"attack -t deauth -s",
      "karma -p",
      "attack -t badmsg -c",
      "attack -t sleep -c"},
     INPUT_ARGS,
     FOCUS_CONSOLE_END,
     SHOW_STOPSCAN_TIP,
     MMCatWifi},
    {"Beacon Spam",
     {"ap list", "ssid list", "random"},
     3,
     {"attack -t beacon -a", "attack -t beacon -l", "attack -t beacon -r"},
     NO_ARGS,
     FOCUS_CONSOLE_END,
     SHOW_STOPSCAN_TIP,
     MMCatWifi},
    {"Port Scan",
     {"all", "ssh", "telnet", "dns", "http", "smtp", "https", "rdp"},
     8,
     {"portscan -a -t",
      "portscan -s ssh",
      "portscan -s telnet",
      "portscan -s dns",
      "portscan -s http",
      "portscan -s smtp",
      "portscan -s https",
      "portscan -s rdp"},
     INPUT_ARGS,
     FOCUS_CONSOLE_END,
     SHOW_STOPSCAN_TIP,
     MMCatWifi},
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

void wifi_marauder_scene_start_on_enter(void* context) {
    WifiMarauderApp* app = context;
    VariableItemList* var_item_list = app->var_item_list;
    MMMenuCategory cat = app->menu_category;

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
        }
        consumed = true;
    } else if(event.type == SceneManagerEventTypeTick) {
        int row = variable_item_list_get_selected_item_index(app->var_item_list);
        app->category_cursor[app->menu_category] = row;
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
