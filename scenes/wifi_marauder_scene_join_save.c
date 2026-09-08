// Marauder's Mate: "Save for later?" confirm before a typed network join.
//
// Reached only from the text-input join flow when the AP's SSID is known and a
// password was typed. On "Yes" the SSID/password is upserted into the SD
// networks file so a future Join is one tap; either choice then proceeds to the
// console to actually run the join command (already in selected_tx_string).
//
// A widget with two buttons is used, not a blocking DialogMessage: a modal
// shown from a view event handler deadlocks the fullscreen GUI event loop.
#include "../wifi_marauder_app_i.h"

static void wifi_marauder_scene_join_save_button_cb(
    GuiButtonType result, InputType type, void* context) {
    WifiMarauderApp* app = context;
    if(type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, result);
    }
}

void wifi_marauder_scene_join_save_on_enter(void* context) {
    WifiMarauderApp* app = context;

    widget_add_button_element(
        app->widget, GuiButtonTypeLeft, "No", wifi_marauder_scene_join_save_button_cb, app);
    widget_add_button_element(
        app->widget, GuiButtonTypeRight, "Yes", wifi_marauder_scene_join_save_button_cb, app);

    widget_add_string_element(
        app->widget, 64, 2, AlignCenter, AlignTop, FontPrimary, "Save for later?");
    char body[96];
    snprintf(
        body,
        sizeof(body),
        "%s\nStored in plaintext\non the SD card.",
        app->join_ssid);
    widget_add_text_box_element(
        app->widget, 0, 16, 128, 34, AlignCenter, AlignCenter, body, false);

    view_dispatcher_switch_to_view(app->view_dispatcher, WifiMarauderAppViewWidget);
}

bool wifi_marauder_scene_join_save_on_event(void* context, SceneManagerEvent event) {
    WifiMarauderApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == GuiButtonTypeRight) {
            // Yes: extract the password from "join -a <n> -p <password>" and
            // upsert it under the remembered SSID.
            const char* p =
                app->selected_tx_string ? strstr(app->selected_tx_string, " -p ") : NULL;
            if(p && p[4] != '\0' && app->join_ssid[0]) {
                mm_save_network_password(app, app->join_ssid, p + 4);
            }
        }
        // Either choice: consume the SSID and run the join on the clean screen.
        app->join_ssid[0] = '\0';
        scene_manager_next_scene(app->scene_manager, WifiMarauderSceneJoin);
        consumed = true;
    }

    return consumed;
}

void wifi_marauder_scene_join_save_on_exit(void* context) {
    WifiMarauderApp* app = context;
    widget_reset(app->widget);
}
