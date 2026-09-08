// Marauder's Mate: parsed Port Scan results.
//
// Runs a full port scan of one host (`portscan -t <ipList index> -a`, the host
// selected in Host Scan) and shows the open ports as a clean list with service
// names, instead of the raw console. Marauder streams progress lines
// ("Checking IP: <ip> Port: <n>", every 1000 ports) interleaved with open-port
// hits ("<ip>: <port>"); we surface progress in the header and the hits as rows.
#include "../wifi_marauder_app_i.h"

#define MM_PORT_MAX 64
#define MM_ITEM_INFO (0xFFFFFFFEu)

static int s_ticks; // rebuild-throttle counter

static void wifi_marauder_port_results_rx_cb(uint8_t* buf, size_t len, void* context) {
    WifiMarauderApp* app = context;
    furi_stream_buffer_send(app->scan_stream, buf, len, 0);
}

static void wifi_marauder_port_results_noop_cb(void* context, uint32_t index) {
    UNUSED(context);
    UNUSED(index);
}

static void wifi_marauder_port_results_build(WifiMarauderApp* app) {
    Submenu* submenu = app->submenu;
    uint32_t sel = submenu_get_selected_item(submenu);
    submenu_reset(submenu);

    char header[48];
    if(!app->portscan_done) {
        snprintf(
            header, sizeof(header), "%s  %d open  p%d", app->portscan_ip, app->open_port_count,
            app->port_progress);
    } else {
        snprintf(header, sizeof(header), "%s  %d open", app->portscan_ip, app->open_port_count);
    }
    submenu_set_header(submenu, header);

    char row[32];
    for(int i = 0; i < app->open_port_count; i++) {
        const char* svc = mm_port_service_name(app->open_ports[i]);
        if(svc[0]) {
            snprintf(row, sizeof(row), "%d  %s", app->open_ports[i], svc);
        } else {
            snprintf(row, sizeof(row), "%d", app->open_ports[i]);
        }
        submenu_add_item(submenu, row, (uint32_t)i, wifi_marauder_port_results_noop_cb, app);
    }
    if(app->open_port_count == 0) {
        submenu_add_item(
            submenu,
            app->portscan_done ? "No open ports" : "Scanning...",
            MM_ITEM_INFO,
            wifi_marauder_port_results_noop_cb,
            app);
    }
    if(sel != (uint32_t)-1) submenu_set_selected_item(submenu, sel);
}

void wifi_marauder_scene_port_results_on_enter(void* context) {
    WifiMarauderApp* app = context;
    app->open_port_count = 0;
    app->open_ports_built = 0;
    app->port_progress = 0;
    app->portscan_done = false;
    s_ticks = 0;
    furi_stream_buffer_reset(app->scan_stream);
    furi_string_reset(app->scan_line);

    wifi_marauder_port_results_build(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, WifiMarauderAppViewSubmenu);

    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, wifi_marauder_port_results_rx_cb);
    wifi_marauder_uart_set_handle_rx_pcap_cb(app->uart, NULL);

    char cmd[24];
    snprintf(cmd, sizeof(cmd), "portscan -t %d -a\n", app->host_selected);
    wifi_marauder_uart_tx(app->uart, (uint8_t*)cmd, strlen(cmd));
}

bool wifi_marauder_scene_port_results_on_event(void* context, SceneManagerEvent event) {
    WifiMarauderApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        uint8_t tmp[129];
        size_t got;
        while((got = furi_stream_buffer_receive(app->scan_stream, tmp, sizeof(tmp) - 1, 0)) > 0) {
            mm_sanitize_nuls(tmp, got);
            tmp[got] = '\0';
            furi_string_cat_str(app->scan_line, (const char*)tmp);
        }

        int processed = 0;
        for(;;) {
            const char* cstr = furi_string_get_cstr(app->scan_line);
            const char* nl = strchr(cstr, '\n');
            if(!nl) break;
            size_t len = (size_t)(nl - cstr);
            char line[64];
            size_t cpy = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
            memcpy(line, cstr, cpy);
            line[cpy] = '\0';
            furi_string_right(app->scan_line, len + 1);

            int port;
            if(mm_portscan_parse_open(line, &port)) {
                bool dup = false;
                for(int i = 0; i < app->open_port_count; i++)
                    if(app->open_ports[i] == port) dup = true;
                if(!dup && app->open_port_count < MM_PORT_MAX)
                    app->open_ports[app->open_port_count++] = (uint16_t)port;
            } else {
                const char* pp = strstr(line, "Port: "); // progress line
                if(pp) app->port_progress = atoi(pp + 6);
            }
            if(++processed >= 24) break;
        }

        // Throttled rebuild on growth (or ~1/s to refresh progress).
        if((app->open_port_count > app->open_ports_built) || (s_ticks % 10 == 0)) {
            wifi_marauder_port_results_build(app);
            app->open_ports_built = app->open_port_count;
        }
        s_ticks++;
        consumed = true;
    }

    return consumed;
}

void wifi_marauder_scene_port_results_on_exit(void* context) {
    WifiMarauderApp* app = context;
    wifi_marauder_uart_tx(app->uart, (uint8_t*)"stopscan\n", strlen("stopscan\n"));
    wifi_marauder_uart_set_handle_rx_data_cb(app->uart, NULL);
    submenu_reset(app->submenu);
}
