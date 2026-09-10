#include "wifi_marauder_app_i.h"

#include <furi.h>
#include <furi_hal.h>
#include <expansion/expansion.h>
#include <flipper_format/flipper_format.h>

#define MM_NET_MAX 48 // max networks kept in the auto-join bank

bool mm_save_network_password(WifiMarauderApp* app, const char* ssid, const char* pass) {
    if(!ssid || ssid[0] == '\0' || !pass || pass[0] == '\0') return false;

    // Static (BSS), not stack: ~4.6KB would overflow the GUI thread stack.
    // Safe because this only runs on the single-threaded GUI event loop.
    static char names[MM_NET_MAX][MM_AP_NAME_MAX];
    static char passes[MM_NET_MAX][64];
    int n = 0;
    bool replaced = false;

    // Load the existing bank, updating the target SSID's password in place.
    FlipperFormat* in = flipper_format_file_alloc(app->storage);
    if(flipper_format_file_open_existing(in, MM_NETWORKS_FILEPATH)) {
        FuriString* k = furi_string_alloc();
        FuriString* v = furi_string_alloc();
        while(n < MM_NET_MAX && flipper_format_read_string(in, "SSID", k)) {
            if(!flipper_format_read_string(in, "Pass", v)) break;
            strncpy(names[n], furi_string_get_cstr(k), MM_AP_NAME_MAX - 1);
            names[n][MM_AP_NAME_MAX - 1] = '\0';
            const char* src = (strcmp(names[n], ssid) == 0) ? (replaced = true, pass) :
                                                              furi_string_get_cstr(v);
            strncpy(passes[n], src, 63);
            passes[n][63] = '\0';
            n++;
        }
        furi_string_free(k);
        furi_string_free(v);
    }
    flipper_format_free(in);

    // New network: append.
    if(!replaced && n < MM_NET_MAX) {
        strncpy(names[n], ssid, MM_AP_NAME_MAX - 1);
        names[n][MM_AP_NAME_MAX - 1] = '\0';
        strncpy(passes[n], pass, 63);
        passes[n][63] = '\0';
        n++;
    }

    // Rewrite the whole file (small bank; simplest correct upsert).
    FlipperFormat* out = flipper_format_file_alloc(app->storage);
    bool ok = false;
    if(flipper_format_file_open_always(out, MM_NETWORKS_FILEPATH)) {
        if(flipper_format_write_header_cstr(out, "Marauder Networks", 1)) {
            ok = true;
            for(int i = 0; i < n && ok; i++) {
                ok = flipper_format_write_string_cstr(out, "SSID", names[i]) &&
                     flipper_format_write_string_cstr(out, "Pass", passes[i]);
            }
        }
    }
    flipper_format_free(out);
    return ok;
}

static bool wifi_marauder_app_custom_event_callback(void* context, uint32_t event) {
    furi_assert(context);
    WifiMarauderApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool wifi_marauder_app_back_event_callback(void* context) {
    furi_assert(context);
    WifiMarauderApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static void wifi_marauder_app_tick_event_callback(void* context) {
    furi_assert(context);
    WifiMarauderApp* app = context;
    scene_manager_handle_tick_event(app->scene_manager);
}

WifiMarauderApp* wifi_marauder_app_alloc() {
    WifiMarauderApp* app = malloc(sizeof(WifiMarauderApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->capture_file = storage_file_alloc(app->storage);
    app->log_file = storage_file_alloc(app->storage);
    app->save_pcap_setting_file = storage_file_alloc(app->storage);
    app->save_logs_setting_file = storage_file_alloc(app->storage);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&wifi_marauder_scene_handlers, app);
    view_dispatcher_enable_queue(app->view_dispatcher);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);

    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, wifi_marauder_app_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, wifi_marauder_app_back_event_callback);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, wifi_marauder_app_tick_event_callback, 100);

    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->var_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        WifiMarauderAppViewVarItemList,
        variable_item_list_get_view(app->var_item_list));

    for(int i = 0; i < NUM_MENU_ITEMS; ++i) {
        app->selected_option_index[i] = 0;
    }

    // Protocol-category menu state
    app->menu_category = MMCatWifi;
    app->sub_category = MMCatSpoof;
    for(int i = 0; i < MMCatCount; ++i) app->category_cursor[i] = 0;
    app->bt_state = MMBtUnknown;
    app->device_state = MMDevUnknown;
    app->sd_state = MMCapUnknown;
    app->gps_state = MMCapUnknown;
    app->direct_upload_state = MMCapUnknown;
    app->dual_band_state = MMCapUnknown;

    app->special_case_input_step = 0;

    app->text_box = text_box_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, WifiMarauderAppViewConsoleOutput, text_box_get_view(app->text_box));
    app->text_box_store = furi_string_alloc();
    furi_string_reserve(app->text_box_store, WIFI_MARAUDER_TEXT_BOX_STORE_SIZE);

    // Marauder's Mate: shared scan scratch buffer
    app->ap_scan_buffer = furi_string_alloc();
    furi_string_reserve(app->ap_scan_buffer, 2048);
    app->host_count = 0;
    app->host_built = 0;
    app->host_dirty = false;
    app->host_selected = 0;
    app->host_state = MMHostReady; // detail scene forces a fresh scan
    app->sel_ap_prev = -1;
    app->sel_sta_prev = -1;

    // Marauder's Mate: live scanall state
    app->scan_stream = furi_stream_buffer_alloc(4096, 1);
    app->scan_line = furi_string_alloc();
    app->scan_ap_count = 0;
    app->scan_selected = 0;
    app->scan_live_state = MMLiveReady; // start scene forces a fresh scan
    app->scan_live_ticks = 0;
    app->scan_station_count = 0;
    app->scan_aps_built = 0;
    app->scan_dirty = false;
    app->scan_resolve_to_detail = false;
    app->scan_resume_pending = false;
    app->join_ssid[0] = '\0';
    app->join_target[0] = '\0';
    app->join_status[0] = '\0';
    app->join_last[0] = '\0';
    app->join_ip[0] = '\0';
    app->join_done = false;
    app->join_bssid[0] = '\0';
    app->wifi_connected = false;
    app->connected_bssid[0] = '\0';
    app->connected_ssid[0] = '\0';

    app->text_input = wifi_text_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        WifiMarauderAppViewTextInput,
        wifi_text_input_get_view(app->text_input));

    app->widget = widget_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, WifiMarauderAppViewWidget, widget_get_view(app->widget));

    app->has_saved_logs_this_session = false;

    // if user hasn't confirmed whether to save pcaps and logs to sdcard, then prompt when scene starts
    app->need_to_prompt_settings_init =
        (!storage_file_exists(app->storage, SAVE_PCAP_SETTING_FILEPATH) ||
         !storage_file_exists(app->storage, SAVE_LOGS_SETTING_FILEPATH));

    // Submenu
    app->submenu = submenu_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, WifiMarauderAppViewSubmenu, submenu_get_view(app->submenu));

    // NOTE: the first scene is launched by wifi_marauder_app() AFTER the UART is
    // initialized. The category menu probes BT over UART on enter, so it must
    // not run while app->uart is still uninitialized.
    app->uart = NULL;

    return app;
}

void wifi_marauder_make_app_folder(WifiMarauderApp* app) {
    furi_assert(app);

    if(!storage_simply_mkdir(app->storage, MARAUDER_APP_FOLDER)) {
        dialog_message_show_storage_error(app->dialogs, "Cannot create\napp folder");
    }

    if(!storage_simply_mkdir(app->storage, MARAUDER_APP_FOLDER_PCAPS)) {
        dialog_message_show_storage_error(app->dialogs, "Cannot create\npcaps folder");
    }

    if(!storage_simply_mkdir(app->storage, MARAUDER_APP_FOLDER_DUMPS)) {
        dialog_message_show_storage_error(app->dialogs, "Cannot create\ndumps folder");
    }

    if(!storage_simply_mkdir(app->storage, MARAUDER_APP_FOLDER_LOGS)) {
        dialog_message_show_storage_error(app->dialogs, "Cannot create\nlogs folder");
    }

    if(!storage_simply_mkdir(app->storage, MARAUDER_APP_FOLDER_HTML)) {
        dialog_message_show_storage_error(app->dialogs, "Cannot create\nhtml folder");
    }
}

void wifi_marauder_load_settings(WifiMarauderApp* app) {
    if(storage_file_open(
           app->save_pcap_setting_file,
           SAVE_PCAP_SETTING_FILEPATH,
           FSAM_READ,
           FSOM_OPEN_EXISTING)) {
        char ok[1];
        storage_file_read(app->save_pcap_setting_file, ok, sizeof(ok));
        app->ok_to_save_pcaps = ok[0] == 'Y';
    }
    storage_file_close(app->save_pcap_setting_file);

    if(storage_file_open(
           app->save_logs_setting_file,
           SAVE_LOGS_SETTING_FILEPATH,
           FSAM_READ,
           FSOM_OPEN_EXISTING)) {
        char ok[1];
        storage_file_read(app->save_logs_setting_file, ok, sizeof(ok));
        app->ok_to_save_logs = ok[0] == 'Y';
    }
    storage_file_close(app->save_logs_setting_file);
}

void wifi_marauder_app_free(WifiMarauderApp* app) {
    furi_assert(app);

    // Views
    view_dispatcher_remove_view(app->view_dispatcher, WifiMarauderAppViewVarItemList);
    view_dispatcher_remove_view(app->view_dispatcher, WifiMarauderAppViewConsoleOutput);
    view_dispatcher_remove_view(app->view_dispatcher, WifiMarauderAppViewTextInput);
    view_dispatcher_remove_view(app->view_dispatcher, WifiMarauderAppViewWidget);
    view_dispatcher_remove_view(app->view_dispatcher, WifiMarauderAppViewSubmenu);

    widget_free(app->widget);
    text_box_free(app->text_box);
    furi_string_free(app->text_box_store);
    furi_string_free(app->ap_scan_buffer);
    furi_stream_buffer_free(app->scan_stream);
    furi_string_free(app->scan_line);
    wifi_text_input_free(app->text_input);
    submenu_free(app->submenu);
    variable_item_list_free(app->var_item_list);
    storage_file_free(app->capture_file);
    storage_file_free(app->log_file);
    storage_file_free(app->save_pcap_setting_file);
    storage_file_free(app->save_logs_setting_file);

    // View dispatcher
    view_dispatcher_free(app->view_dispatcher);
    scene_manager_free(app->scene_manager);

    wifi_marauder_uart_free(app->uart);

    // Close records
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_DIALOGS);

    free(app);
}

void mm_select_target(WifiMarauderApp* app, int ap_idx, int sta_idx) {
    char cmd[24];
    // Deselect whatever we previously selected, back to a known-empty state.
    if(app->sel_sta_prev >= 0) {
        snprintf(cmd, sizeof(cmd), "select -c %d\n", app->sel_sta_prev);
        wifi_marauder_uart_tx(app->uart, (uint8_t*)cmd, strlen(cmd));
        app->sel_sta_prev = -1;
    }
    if(app->sel_ap_prev >= 0) {
        snprintf(cmd, sizeof(cmd), "select -a %d\n", app->sel_ap_prev);
        wifi_marauder_uart_tx(app->uart, (uint8_t*)cmd, strlen(cmd));
        app->sel_ap_prev = -1;
    }
    // Select only the requested target(s).
    if(ap_idx >= 0) {
        snprintf(cmd, sizeof(cmd), "select -a %d\n", ap_idx);
        wifi_marauder_uart_tx(app->uart, (uint8_t*)cmd, strlen(cmd));
        app->sel_ap_prev = ap_idx;
    }
    if(sta_idx >= 0) {
        snprintf(cmd, sizeof(cmd), "select -c %d\n", sta_idx);
        wifi_marauder_uart_tx(app->uart, (uint8_t*)cmd, strlen(cmd));
        app->sel_sta_prev = sta_idx;
    }
}

void mm_apply_info_caps(WifiMarauderApp* app, const char* text) {
    // Walk an `info` reply and set device presence + SD/BT/GPS capability.
    // Shared by the launch probe (categories) and Device Info's Re-detect.
    bool present = false;
    MMCap sd = MMCapUnknown, bt = MMCapUnknown, gps = MMCapUnknown;
    MMCap upload = MMCapUnknown, dual = MMCapUnknown;
    char line[96];
    while(text && *text) {
        const char* nl = strchr(text, '\n');
        size_t len = nl ? (size_t)(nl - text) : strlen(text);
        size_t cpy = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
        memcpy(line, text, cpy);
        line[cpy] = '\0';
        if(mm_info_line_is_marauder(line)) present = true;
        MMCap c;
        if((c = mm_info_line_sd(line)) != MMCapUnknown) sd = c;
        if((c = mm_info_line_bt(line)) != MMCapUnknown) bt = c;
        if((c = mm_info_line_gps(line)) != MMCapUnknown) gps = c;
        if((c = mm_info_line_direct_upload(line)) != MMCapUnknown) upload = c;
        if((c = mm_info_line_dual_band(line)) != MMCapUnknown) dual = c;
        if(!nl) break;
        text = nl + 1;
    }
    app->device_state = present ? MMDevPresent : MMDevAbsent;
    if(present) {
        app->sd_state = sd;
        app->gps_state = gps;
        app->direct_upload_state = upload;
        app->dual_band_state = dual;
        if(bt != MMCapUnknown) app->bt_state = (bt == MMCapYes) ? MMBtYes : MMBtNo;
    }
}

int32_t wifi_marauder_app(void* p) {
    UNUSED(p);
    // Disable expansion protocol to avoid interference with UART Handle
    Expansion* expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(expansion);

    uint8_t attempts = 0;
    bool otg_was_enabled = furi_hal_power_is_otg_enabled();
    while(!furi_hal_power_is_otg_enabled() && attempts++ < 5) {
        furi_hal_power_enable_otg();
        furi_delay_ms(10);
    }
    furi_delay_ms(200);

    WifiMarauderApp* wifi_marauder_app = wifi_marauder_app_alloc();

    wifi_marauder_make_app_folder(wifi_marauder_app);
    wifi_marauder_load_settings(wifi_marauder_app);

    wifi_marauder_app->uart = wifi_marauder_usart_init(wifi_marauder_app);

    // Launch the first scene now that the UART is ready (the category menu
    // probes BT over UART on enter).
    scene_manager_next_scene(wifi_marauder_app->scene_manager, WifiMarauderSceneCategories);

    view_dispatcher_run(wifi_marauder_app->view_dispatcher);

    wifi_marauder_app_free(wifi_marauder_app);

    if(furi_hal_power_is_otg_enabled() && !otg_was_enabled) {
        furi_hal_power_disable_otg();
    }

    // Return previous state of expansion
    expansion_enable(expansion);
    furi_record_close(RECORD_EXPANSION);

    return 0;
}
