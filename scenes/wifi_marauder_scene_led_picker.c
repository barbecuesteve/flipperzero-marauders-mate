// Marauder's Mate: LED colour picker.
//
// Friendlier replacement for the raw `led -s <hex>` keyboard entry. Named
// presets send `led -s #RRGGBB` (the firmware skips the leading char, so the
// '#' is required); Rainbow sends `led -p rainbow`. Selecting a colour fires it
// straight over the UART and stays on the picker so you can try several.
// (Needs HAS_NEOPIXEL_LED on the board; a no-op otherwise.)
#include "../wifi_marauder_app_i.h"

typedef struct {
    const char* name;
    const char* cmd; // full command, newline-terminated
} MMLedPreset;

static const MMLedPreset s_presets[] = {
    {"Off", "led -s #000000\n"},
    {"White", "led -s #FFFFFF\n"},
    {"Red", "led -s #FF0000\n"},
    {"Green", "led -s #00FF00\n"},
    {"Blue", "led -s #0000FF\n"},
    {"Yellow", "led -s #FFFF00\n"},
    {"Cyan", "led -s #00FFFF\n"},
    {"Magenta", "led -s #FF00FF\n"},
    {"Orange", "led -s #FF7F00\n"},
    {"Rainbow", "led -p rainbow\n"},
};

static void wifi_marauder_led_picker_item_cb(void* context, uint32_t index) {
    WifiMarauderApp* app = context;
    if(index >= COUNT_OF(s_presets)) return;
    const char* cmd = s_presets[index].cmd;
    if(app->uart) wifi_marauder_uart_tx(app->uart, (uint8_t*)cmd, strlen(cmd));
}

void wifi_marauder_scene_led_picker_on_enter(void* context) {
    WifiMarauderApp* app = context;
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    submenu_set_header(submenu, "LED");
    for(size_t i = 0; i < COUNT_OF(s_presets); ++i) {
        submenu_add_item(
            submenu, s_presets[i].name, i, wifi_marauder_led_picker_item_cb, app);
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiMarauderAppViewSubmenu);
}

bool wifi_marauder_scene_led_picker_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false; // Back returns to the System menu
}

void wifi_marauder_scene_led_picker_on_exit(void* context) {
    WifiMarauderApp* app = context;
    submenu_reset(app->submenu);
}
