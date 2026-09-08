//** Includes sniffbt and sniffskim for compatible ESP32-WROOM hardware.
// wifi_marauder_scene_start.c also changed **//
#pragma once

#include "wifi_marauder_app.h"
#include "scenes/wifi_marauder_scene.h"
#include "wifi_marauder_custom_event.h"
#include "wifi_marauder_uart.h"
#include "marauders_mate_ap_parser.h"
#include "wifi_marauder_ep.h"
#include "file/sequential_file.h"
#include "script/wifi_marauder_script.h"
#include "script/wifi_marauder_script_worker.h"
#include "script/wifi_marauder_script_executor.h"
#include "script/menu/wifi_marauder_script_stage_menu.h"

#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/text_box.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include "wifi_marauder_text_input.h"

#include <marauders_mate_icons.h>
#include <storage/storage.h>
#include <lib/toolbox/path.h>
#include <dialogs/dialogs.h>

#define NUM_MENU_ITEMS (35)

// Marauder's Mate: parsed AP list feature
#define MM_AP_MAX (64)
#define MM_STA_MAX (96) // total unique (AP, station) pairs kept across a scan

typedef struct {
    char mac[MM_BSSID_LEN]; // station MAC
    int ap_index; // store index of the AP it associates with
    int sel_index; // `select -c` index (from list -c); -1 if not resolved yet
} MMStation;

typedef enum {
    MMApActionDeauth,
    MMApActionSniff,
    MMApActionPmkid,
} MMApAction;

typedef enum {
    MMLiveScanning, // clearlist + scanall streaming, live dedup
    MMLiveListing, // sent list -a, capturing text to resolve AP indices
    MMLiveListingClients, // sent list -c, capturing text to resolve stations
    MMLiveReady, // resolved, submenu populated (cached)
} MMLiveScanState;

// Layer-3 host discovery (after `join`)
#define MM_HOST_MAX (128)

typedef enum {
    MMHostScanning, // pingscan sweeping the joined subnet
    MMHostListing, // sent list -i, collecting host IPs
    MMHostReady, // parsed, submenu populated (cached)
} MMHostState;

#define WIFI_MARAUDER_TEXT_BOX_STORE_SIZE (4096)
#define WIFI_MARAUDER_TEXT_INPUT_STORE_SIZE (512)

#define MARAUDER_APP_FOLDER_USER "apps_data/marauder"
#define MARAUDER_APP_FOLDER EXT_PATH(MARAUDER_APP_FOLDER_USER)
#define MARAUDER_APP_FOLDER_HTML MARAUDER_APP_FOLDER "/html"
#define MARAUDER_APP_FOLDER_PCAPS MARAUDER_APP_FOLDER "/pcaps"
#define MARAUDER_APP_FOLDER_DUMPS MARAUDER_APP_FOLDER "/dumps"
#define MARAUDER_APP_FOLDER_LOGS MARAUDER_APP_FOLDER "/logs"
#define MARAUDER_APP_FOLDER_USER_PCAPS MARAUDER_APP_FOLDER_USER "/pcaps"
#define MARAUDER_APP_FOLDER_USER_LOGS MARAUDER_APP_FOLDER_USER "/logs"
#define MARAUDER_APP_FOLDER_SCRIPTS MARAUDER_APP_FOLDER "/scripts"
#define MARAUDER_APP_SCRIPT_PATH(file_name) MARAUDER_APP_FOLDER_SCRIPTS "/" file_name ".json"
#define SAVE_PCAP_SETTING_FILEPATH MARAUDER_APP_FOLDER "/save_pcaps_here.setting"
#define SAVE_LOGS_SETTING_FILEPATH MARAUDER_APP_FOLDER "/save_logs_here.setting"
// Optional user-supplied WiFi passwords for auto-join. Plaintext on the SD
// card (unencrypted, removable) -- user's own networks, user's own risk. Never
// committed; a networks.txt.example ships with placeholders only.
#define MM_NETWORKS_FILEPATH MARAUDER_APP_FOLDER "/networks.txt"

typedef enum WifiMarauderUserInputType {
    WifiMarauderUserInputTypeString,
    WifiMarauderUserInputTypeNumber,
    WifiMarauderUserInputTypeFileName
} WifiMarauderUserInputType;

struct WifiMarauderApp {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;

    char text_input_store[WIFI_MARAUDER_TEXT_INPUT_STORE_SIZE + 1];
    FuriString* text_box_store;
    size_t text_box_store_strlen;
    TextBox* text_box;
    WIFI_TextInput* text_input;
    Storage* storage;
    File* capture_file;
    File* log_file;
    char log_file_path[100];
    File* save_pcap_setting_file;
    File* save_logs_setting_file;
    bool need_to_prompt_settings_init;
    int which_prompt;
    bool ok_to_save_pcaps;
    bool ok_to_save_logs;
    bool has_saved_logs_this_session;
    DialogsApp* dialogs;

    VariableItemList* var_item_list;
    Widget* widget;
    Submenu* submenu;
    int open_log_file_page;
    int open_log_file_num_pages;

    WifiMarauderUart* uart;
    int selected_menu_index;
    int selected_option_index[NUM_MENU_ITEMS];
    const char* selected_tx_string;
    bool is_command;
    bool is_custom_tx_string;
    bool focus_console_start;
    bool show_stopscan_tip;
    bool is_writing_pcap;
    bool is_writing_log;

    // User input
    WifiMarauderUserInputType user_input_type;
    char** user_input_string_reference;
    int* user_input_number_reference;
    char* user_input_file_dir;
    char* user_input_file_extension;

    // Automation script
    WifiMarauderScript* script;
    WifiMarauderScriptWorker* script_worker;
    FuriString** script_list;
    int script_list_count;
    WifiMarauderScriptStage* script_edit_selected_stage;
    WifiMarauderScriptStageMenu* script_stage_menu;
    WifiMarauderScriptStageListItem* script_stage_edit_first_item;
    char*** script_stage_edit_strings_reference;
    int* script_stage_edit_string_count_reference;
    int** script_stage_edit_numbers_reference;
    int* script_stage_edit_number_count_reference;

    // For input source and destination MAC in targeted deauth attack
    int special_case_input_step;
    char special_case_input_src_addr[20];
    char special_case_input_dst_addr[20];

    // Marauder's Mate: shared command/parse scratch (used by the scan scenes)
    FuriString* ap_scan_buffer; // list -a / list -c text accumulator
    int ap_action; // MMApAction chosen on the detail screen
    char ap_cmd_buf[32]; // scratch for "select -a/-c <n>"
    char join_cmd[96]; // scratch for "join -a <n> -p <password>" (auto-join)
    char join_ssid[MM_AP_NAME_MAX]; // SSID of a typed join, for the "Save for later?" prompt
    // Join result scene: parses join output into a clean status, hiding the
    // settings dump Marauder prints (which includes the plaintext password).
    char join_target[MM_AP_NAME_MAX]; // SSID shown on the join screen
    char join_status[24]; // "Connecting...", "Connected", "Failed"
    char join_last[40]; // last meaningful (non-settings) line from Marauder
    char join_ip[16]; // assigned IP, if seen
    bool join_done; // a terminal status (connected/failed) was detected
    int join_ticks;
    char join_bssid[MM_BSSID_LEN]; // BSSID of the AP the current join targets
    // Believed connection state: set when a join reports Connected, cleared on a
    // failed join. The link lives on the ESP and can drop on its own, so this is
    // "our last join to this BSSID succeeded", not a polled live status.
    bool wifi_connected;
    char connected_bssid[MM_BSSID_LEN];
    char connected_ssid[MM_AP_NAME_MAX];
    int sel_ap_prev; // last AP index WE selected (-1 none) -- Marauder select toggles
    int sel_sta_prev; // last station index WE selected (-1 none)

    // Marauder's Mate: live scanall feature
    MMScanAp scan_aps[MM_AP_MAX]; // BSSID-keyed, array index == discovery order
    int scan_clients[MM_AP_MAX]; // associated stations seen per AP
    int scan_resolved_index[MM_AP_MAX]; // list -a index for select, -1 if none
    int scan_ap_count;
    int scan_selected; // discovery index of the AP being inspected
    FuriStreamBuffer* scan_stream; // thread-safe rx handoff (worker -> gui)
    FuriString* scan_line; // main-thread line assembler
    MMLiveScanState scan_live_state;
    int scan_live_ticks;
    bool scan_dirty; // a new AP/client arrived; submenu needs a (throttled) rebuild
    int scan_aps_built; // scan_ap_count at the last live rebuild (rebuild only when it grows)
    bool scan_resolve_to_detail; // resolve phase was kicked off by an AP tap -> open detail when done
    bool scan_resume_pending; // re-entering the live view from detail -> resume scan, keep store
    MMStation scan_stations[MM_STA_MAX]; // stations seen, deduped per (AP, MAC)
    int scan_station_count;
    int sta_selected; // scan_stations[] index of the client being acted on

    // Marauder's Mate: Fox Hunt RSSI meter
    int fox_rssi; // latest raw RSSI reading
    int fox_smoothed; // EMA-smoothed RSSI (what the meter shows)
    int fox_best; // strongest (least negative) smoothed RSSI seen
    bool fox_have; // whether any reading has arrived yet
    int fox_ticks; // redraw throttle counter
    int fox_last_rx_tick; // fox_ticks value at the last RSSI sample (staleness)
    bool fox_is_station; // hunt a station (foxhunt -s) vs an AP (foxhunt -w)
    int fox_ap_arg; // AP select index for the foxhunt command
    int fox_sta_arg; // station select index (station hunt only)
    char fox_title[MM_AP_NAME_MAX]; // label shown on the meter

    // Marauder's Mate: L3 host discovery
    char hosts[MM_HOST_MAX][16]; // discovered host IPs (dotted quad)
    int host_count;
    int host_selected;
    MMHostState host_state;
    int host_ticks;

    // Marauder's Mate: beacon activity monitor (sniffbeacon)
    MMScanAp beacon_aps[MM_AP_MAX]; // deduped by BSSID
    int beacon_hits[MM_AP_MAX]; // beacon frames counted per AP
    int beacon_ap_count;
    int beacon_ticks; // rebuild throttle

    // Marauder's Mate: probe-request monitor (sniffprobe)
    MMScanAp probe_clients[MM_AP_MAX]; // deduped by client MAC (in .bssid), .ssid = requested
    int probe_hits[MM_AP_MAX]; // probe frames counted per client
    int probe_client_count;
    int probe_ticks;
};

// Supported commands:
// https://github.com/justcallmekoko/ESP32Marauder/wiki/cli
//   Scan
//    -> If list is empty, then start a new scanap. (Tap any button to stop.)
//    -> If there's a list, provide option to rescan and dump list of targets to select.
//    -> Press BACK to go back to top-level.
//   Attack
//    -> Beacon
//    -> Deauth
//    -> Probe
//    -> Rickroll
//   Sniff
//    -> Beacon
//    -> Deauth
//    -> ESP
//    -> PMKID
//    -> Pwnagotchi
//   Channel
//   Update
//   Reboot

typedef enum {
    WifiMarauderAppViewVarItemList,
    WifiMarauderAppViewConsoleOutput,
    WifiMarauderAppViewTextInput,
    WifiMarauderAppViewWidget,
    WifiMarauderAppViewSubmenu,
} WifiMarauderAppView;

// Select exactly one AP (and optionally one station) as the attack target,
// first deselecting whatever we previously selected. Marauder's `select`
// toggles and accumulates, so without this a later attack would also hit
// earlier targets. sta_idx < 0 selects the AP only. Tracks only selections
// made through the app; selections made via the raw Select menu are the user's.
void mm_select_target(WifiMarauderApp* app, int ap_idx, int sta_idx);

// Upsert a plaintext SSID/password into the auto-join networks file on the SD.
// Replaces an existing entry for the SSID or appends a new one. Returns true on
// a successful write. Password is written verbatim; caller has user consent.
bool mm_save_network_password(WifiMarauderApp* app, const char* ssid, const char* pass);
