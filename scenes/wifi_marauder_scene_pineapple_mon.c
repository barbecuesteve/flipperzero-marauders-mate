// Marauder's Mate: WiFi Pineapple / rogue-AP monitor -- deduped by MAC, sorted
// strongest-first (closest threat on top). Thin wrapper over the engine.
#include "../wifi_marauder_app_i.h"
#include "../wifi_marauder_monitor.h"

static const char* pine_det_short(const char* det) {
    if(strncmp(det, "SUSP_OUI", 8) == 0) return "OUI";
    if(strncmp(det, "TAG", 3) == 0) return "TAG";
    return "?";
}

static bool pine_parse(const char* line, MMMonRec* out) {
    MMPineScan p;
    if(!mm_pinescan_parse_line(line, &p)) return false;
    strncpy(out->key, p.mac, sizeof(out->key) - 1);
    const char* name =
        (p.ssid[0] && strcmp(p.ssid, "[hidden]") != 0) ? p.ssid : p.mac + 9;
    strncpy(out->t1, name, sizeof(out->t1) - 1);
    strncpy(out->t2, pine_det_short(p.det), sizeof(out->t2) - 1);
    out->channel = p.channel;
    out->rssi = p.rssi;
    out->metric = p.rssi; // sort strongest-first
    return true;
}

static void pine_label(const MMMonRec* r, char* buf, size_t sz) {
    snprintf(buf, sz, "%s %s %d", r->t1, r->t2, r->rssi);
}

static const MMMonDef s_def = {
    .cmd = "sniffpinescan",
    .waiting = "Pineapple scan...",
    .header_fmt = "Pineapples: %d",
    .chanhop = true,
    .sort_by_metric = true,
    .parse = pine_parse,
    .label = pine_label,
};

void wifi_marauder_scene_pineapple_mon_on_enter(void* context) {
    mm_monitor_start(context, &s_def);
}

bool wifi_marauder_scene_pineapple_mon_on_event(void* context, SceneManagerEvent event) {
    if(event.type == SceneManagerEventTypeTick) {
        mm_monitor_tick(context, &s_def);
        return true;
    }
    return false;
}

void wifi_marauder_scene_pineapple_mon_on_exit(void* context) {
    mm_monitor_stop(context, &s_def);
}
