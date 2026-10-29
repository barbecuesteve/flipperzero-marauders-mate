// Marauder's Mate: BLE device detail.
//
// Read-only detail for the device picked in the BLE list. The `sniffbt`
// BT_SCAN_ALL stream carries only RSSI and the advertised name (or a bare MAC
// when the device advertised no name), so that -- plus the sighting count this
// session -- is all there is to show. Richer per-device data (services, the MAC
// behind a named device) would need a targeted BLE query the firmware does not
// expose in this mode.
#include "../wifi_marauder_app_i.h"

void wifi_marauder_scene_bt_detail_on_enter(void* context) {
    WifiMarauderApp* app = context;
    Widget* widget = app->widget;
    widget_reset(widget);

    if(app->bt_selected < 0 || app->bt_selected >= app->bt_device_count) {
        widget_add_string_element(
            widget, 64, 28, AlignCenter, AlignCenter, FontSecondary, "No device");
        view_dispatcher_switch_to_view(app->view_dispatcher, WifiMarauderAppViewWidget);
        return;
    }

    const MMBtDevice* d = &app->bt_devices[app->bt_selected];

    // Title: the name, or the MAC when unnamed.
    widget_add_string_element(
        widget, 2, 2, AlignLeft, AlignTop, FontPrimary, d->is_mac ? "(unnamed)" : d->name);

    char row[64];
    // When unnamed, the "name" field IS the MAC; otherwise the MAC isn't exposed.
    snprintf(row, sizeof(row), "Addr: %s", d->is_mac ? d->name : "(hidden)");
    widget_add_string_element(widget, 2, 18, AlignLeft, AlignTop, FontSecondary, row);

    snprintf(row, sizeof(row), "RSSI: %d dBm", d->rssi);
    widget_add_string_element(widget, 2, 30, AlignLeft, AlignTop, FontSecondary, row);

    snprintf(row, sizeof(row), "Seen: %d", app->bt_seen[app->bt_selected]);
    widget_add_string_element(widget, 2, 42, AlignLeft, AlignTop, FontSecondary, row);

    widget_add_string_element(
        widget, 2, 54, AlignLeft, AlignTop, FontSecondary,
        d->is_mac ? "No advertised name" : "Named BLE device");

    view_dispatcher_switch_to_view(app->view_dispatcher, WifiMarauderAppViewWidget);
}

bool wifi_marauder_scene_bt_detail_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false; // back button handled by the scene manager
}

void wifi_marauder_scene_bt_detail_on_exit(void* context) {
    WifiMarauderApp* app = context;
    widget_reset(app->widget);
}
