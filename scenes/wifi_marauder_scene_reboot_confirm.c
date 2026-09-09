// Marauder's Mate: confirm before rebooting the ESP.
//
// A two-button widget (not a blocking DialogMessage, which would deadlock the
// GUI loop from an event handler). "Reboot" sends `reboot` and resets the
// capability state so the menu re-probes when the board comes back; "Cancel"
// just returns to the System menu.
#include "../wifi_marauder_app_i.h"

static void wifi_marauder_scene_reboot_confirm_button_cb(
    GuiButtonType result, InputType type, void* context) {
    WifiMarauderApp* app = context;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, result);
    }
}

void wifi_marauder_scene_reboot_confirm_on_enter(void* context) {
    WifiMarauderApp* app = context;

    widget_add_button_element(
        app->widget, GuiButtonTypeLeft, "Cancel", wifi_marauder_scene_reboot_confirm_button_cb,
        app);
    widget_add_button_element(
        app->widget, GuiButtonTypeRight, "Reboot", wifi_marauder_scene_reboot_confirm_button_cb,
        app);

    widget_add_string_element(
        app->widget, 64, 2, AlignCenter, AlignTop, FontPrimary, "Reboot ESP?");
    widget_add_text_box_element(
        app->widget,
        0,
        16,
        128,
        34,
        AlignCenter,
        AlignCenter,
        "The Marauder will restart.\nScans and any connection\nwill drop.",
        false);

    view_dispatcher_switch_to_view(app->view_dispatcher, WifiMarauderAppViewWidget);
}

bool wifi_marauder_scene_reboot_confirm_on_event(void* context, SceneManagerEvent event) {
    WifiMarauderApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == GuiButtonTypeRight) {
            if(app->uart) wifi_marauder_uart_tx(app->uart, (uint8_t*)"reboot\n", strlen("reboot\n"));
            // The board is restarting; also drop any joined state and force a
            // fresh capability probe next time the category menu is shown.
            app->device_state = MMDevUnknown;
            app->wifi_connected = false;
            app->connected_bssid[0] = '\0';
            app->connected_ssid[0] = '\0';
        }
        scene_manager_previous_scene(app->scene_manager); // back to System either way
        consumed = true;
    }

    return consumed;
}

void wifi_marauder_scene_reboot_confirm_on_exit(void* context) {
    WifiMarauderApp* app = context;
    widget_reset(app->widget);
}
