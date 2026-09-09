// Marauder's Mate: friendly Settings screen.
//
// Reads the ESP's `settings` on entry (Name:/Value: triples) and shows the six
// boolean settings as live On/Off toggles -- flipping one sends
// `settings -s <name> enable|disable`. Two Flipper-side capture prefs (save
// pcaps / logs to the Flipper SD) are folded in as toggles too. Action rows:
// Save profile -> Flipper, Load profile <- Flipper (re-applies to the ESP), and
// Restore defaults (`settings -r`).
#include "../wifi_marauder_app_i.h"
#include <flipper_format/flipper_format.h>

#define MM_SET_COLLECT_TICKS 15 // ~1.5s to collect the settings reply
#define MM_N_ESP 6

static const char* const ESP_NAMES[MM_N_ESP] =
    {"ForcePMKID", "ForceProbe", "SavePCAP", "EnableLED", "EPDeauth", "ChanHop"};

// Row layout after build: 0..5 ESP toggles, then the two prefs and three actions.
enum {
    MM_ROW_PCAP = MM_N_ESP,
    MM_ROW_LOGS,
    MM_ROW_SAVE,
    MM_ROW_LOAD,
    MM_ROW_RESTORE,
};

static const char* const s_onoff[] = {"Off", "On"};

static bool s_esp[MM_N_ESP]; // current ESP values
static VariableItem* s_toggle_item[MM_N_ESP + 2]; // 6 ESP + pcap + logs
static int s_ticks;
static bool s_built;

static void wifi_marauder_settings_menu_build(WifiMarauderApp* app);
static void wifi_marauder_settings_menu_start_query(WifiMarauderApp* app);

static void wifi_marauder_settings_menu_rx_cb(uint8_t* buf, size_t len, void* context) {
    WifiMarauderApp* app = context;
    furi_stream_buffer_send(app->scan_stream, buf, len, 0);
}

// Walk a `settings` reply, setting s_esp[] from each Name:/Value: pair.
static void wifi_marauder_settings_menu_parse(const char* text) {
    int cur = -1;
    char line[96];
    while(text && *text) {
        const char* nl = strchr(text, '\n');
        size_t len = nl ? (size_t)(nl - text) : strlen(text);
        size_t cpy = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
        memcpy(line, text, cpy);
        line[cpy] = '\0';
        const char* p = line;
        while(*p == ' ' || *p == '>') p++;
        if(strncmp(p, "Name: ", 6) == 0) {
            cur = -1;
            const char* name = p + 6;
            for(int i = 0; i < MM_N_ESP; i++)
                if(strncmp(name, ESP_NAMES[i], strlen(ESP_NAMES[i])) == 0) {
                    cur = i;
                    break;
                }
        } else if(strncmp(p, "Value: ", 7) == 0 && cur >= 0) {
            s_esp[cur] = strstr(p, "true") != NULL;
            cur = -1;
        }
        if(!nl) break;
        text = nl + 1;
    }
}

static void wifi_marauder_settings_menu_apply_esp(WifiMarauderApp* app, int i, bool on) {
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "settings -s %s %s\n", ESP_NAMES[i], on ? "enable" : "disable");
    if(app->uart) wifi_marauder_uart_tx(app->uart, (uint8_t*)cmd, strlen(cmd));
}

// Persist a Flipper-side capture pref as a one-char Y/N file (matches the
// format the first-run settings-init flow reads back).
static void wifi_marauder_settings_menu_write_pref(
    WifiMarauderApp* app, const char* path, bool on) {
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(f, on ? "Y" : "N", 1);
    }
    storage_file_close(f);
    storage_file_free(f);
}

static void wifi_marauder_settings_menu_save_profile(WifiMarauderApp* app) {
    FlipperFormat* ff = flipper_format_file_alloc(app->storage);
    if(flipper_format_file_open_always(ff, MM_SETTINGS_PROFILE_FILEPATH)) {
        flipper_format_write_header_cstr(ff, "Marauders Mate settings profile", 1);
        for(int i = 0; i < MM_N_ESP; i++) {
            uint32_t v = s_esp[i] ? 1 : 0;
            flipper_format_write_uint32(ff, ESP_NAMES[i], &v, 1);
        }
    }
    flipper_format_free(ff);
}

static void wifi_marauder_settings_menu_load_profile(WifiMarauderApp* app) {
    FlipperFormat* ff = flipper_format_file_alloc(app->storage);
    if(flipper_format_file_open_existing(ff, MM_SETTINGS_PROFILE_FILEPATH)) {
        for(int i = 0; i < MM_N_ESP; i++) {
            uint32_t v = 0;
            flipper_format_rewind(ff); // keys may be in any order
            if(flipper_format_read_uint32(ff, ESP_NAMES[i], &v, 1)) {
                s_esp[i] = v != 0;
                wifi_marauder_settings_menu_apply_esp(app, i, s_esp[i]);
            }
        }
    }
    flipper_format_free(ff);
}

static void wifi_marauder_settings_menu_change_cb(VariableItem* item) {
    WifiMarauderApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, s_onoff[idx ? 1 : 0]);
    for(int i = 0; i < MM_N_ESP; i++) {
        if(item == s_toggle_item[i]) {
            s_esp[i] = idx;
            wifi_marauder_settings_menu_apply_esp(app, i, idx);
            return;
        }
    }
    if(item == s_toggle_item[MM_N_ESP]) {
        app->ok_to_save_pcaps = idx;
        wifi_marauder_settings_menu_write_pref(app, SAVE_PCAP_SETTING_FILEPATH, idx);
    } else if(item == s_toggle_item[MM_N_ESP + 1]) {
        app->ok_to_save_logs = idx;
        wifi_marauder_settings_menu_write_pref(app, SAVE_LOGS_SETTING_FILEPATH, idx);
    }
}

static void wifi_marauder_settings_menu_enter_cb(void* context, uint32_t index) {
    WifiMarauderApp* app = context;
    if(index == MM_ROW_SAVE) {
        wifi_marauder_settings_menu_save_profile(app);
    } else if(index == MM_ROW_LOAD) {
        wifi_marauder_settings_menu_load_profile(app);
        wifi_marauder_settings_menu_build(app); // reflect loaded values
    } else if(index == MM_ROW_RESTORE) {
        if(app->uart)
            wifi_marauder_uart_tx(app->uart, (uint8_t*)"settings -r\n", strlen("settings -r\n"));
        wifi_marauder_settings_menu_start_query(app); // re-read the defaults
    }
    // Toggle rows use left/right (change_cb); OK on them is a no-op.
}

static void wifi_marauder_settings_menu_add_toggle(
    WifiMarauderApp* app, const char* name, bool on, int slot) {
    VariableItem* it = variable_item_list_add(
        app->var_item_list, name, 2, wifi_marauder_settings_menu_change_cb, app);
    variable_item_set_current_value_index(it, on ? 1 : 0);
    variable_item_set_current_value_text(it, s_onoff[on ? 1 : 0]);
    s_toggle_item[slot] = it;
}

static void wifi_marauder_settings_menu_build(WifiMarauderApp* app) {
    VariableItemList* vil = app->var_item_list;
    variable_item_list_reset(vil);
    variable_item_list_set_enter_callback(vil, wifi_marauder_settings_menu_enter_cb, app);

    for(int i = 0; i < MM_N_ESP; i++)
        wifi_marauder_settings_menu_add_toggle(app, ESP_NAMES[i], s_esp[i], i);
    wifi_marauder_settings_menu_add_toggle(
        app, "Save pcaps->Flipper", app->ok_to_save_pcaps, MM_N_ESP);
    wifi_marauder_settings_menu_add_toggle(
        app, "Save logs->Flipper", app->ok_to_save_logs, MM_N_ESP + 1);

    variable_item_list_add(vil, "Save profile->Flipper", 1, NULL, app);
    variable_item_list_add(vil, "Load profile<-Flipper", 1, NULL, app);
    variable_item_list_add(vil, "Restore defaults", 1, NULL, app);
}

static void wifi_marauder_settings_menu_start_query(WifiMarauderApp* app) {
    s_ticks = 0;
    s_built = false;
    furi_stream_buffer_reset(app->scan_stream);
    furi_string_reset(app->scan_line);
    variable_item_list_reset(app->var_item_list);
    variable_item_list_add(app->var_item_list, "Querying...", 1, NULL, app);
    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, wifi_marauder_settings_menu_rx_cb);
    wifi_marauder_uart_set_handle_rx_pcap_cb(app->uart, NULL);
    wifi_marauder_uart_tx(app->uart, (uint8_t*)"settings\n", strlen("settings\n"));
}

void wifi_marauder_scene_settings_menu_on_enter(void* context) {
    WifiMarauderApp* app = context;
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiMarauderAppViewVarItemList);
    wifi_marauder_settings_menu_start_query(app);
}

bool wifi_marauder_scene_settings_menu_on_event(void* context, SceneManagerEvent event) {
    WifiMarauderApp* app = context;
    if(event.type == SceneManagerEventTypeTick) {
        if(!s_built) {
            uint8_t tmp[129];
            size_t got;
            while((got = furi_stream_buffer_receive(app->scan_stream, tmp, sizeof(tmp) - 1, 0)) >
                  0) {
                mm_sanitize_nuls(tmp, got);
                tmp[got] = '\0';
                furi_string_cat_str(app->scan_line, (const char*)tmp);
            }
            if(++s_ticks >= MM_SET_COLLECT_TICKS) {
                wifi_marauder_uart_set_handle_rx_data_cb(app->uart, NULL);
                wifi_marauder_settings_menu_parse(furi_string_get_cstr(app->scan_line));
                wifi_marauder_settings_menu_build(app);
                s_built = true;
            }
        }
        return true;
    }
    return false;
}

void wifi_marauder_scene_settings_menu_on_exit(void* context) {
    WifiMarauderApp* app = context;
    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, NULL);
    variable_item_list_reset(app->var_item_list);
}
