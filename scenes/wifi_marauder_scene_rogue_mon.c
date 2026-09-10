// Marauder's Mate: rogue-AP monitor (karma/mana multi-SSID beaconing). Runs
// `sniffmultissid` and lists BSSIDs beaconing many SSIDs, sorted by SSID count
// (the more names one radio claims, the more suspicious). Thin wrapper over the
// shared monitor engine.
#include "../wifi_marauder_app_i.h"
#include "../wifi_marauder_monitor.h"

static bool rogue_parse(const char* line, MMMonRec* out) {
    MMRogueAp r;
    if(!mm_multissid_parse_line(line, &r)) return false;
    strncpy(out->key, r.mac, sizeof(out->key) - 1);
    const char* name = (r.ssid[0] && strcmp(r.ssid, "[hidden]") != 0) ? r.ssid : r.mac + 9;
    strncpy(out->t1, name, sizeof(out->t1) - 1);
    out->channel = r.channel;
    out->rssi = r.rssi;
    out->metric = r.ssid_count; // sort by how many SSIDs it claims
    return true;
}

static void rogue_label(const MMMonRec* r, char* buf, size_t sz) {
    snprintf(buf, sz, "%s %dssid %d", r->t1, r->metric, r->rssi);
}

static const MMMonDef s_def = {
    .cmd = "sniffmultissid",
    .waiting = "Rogue AP scan...",
    .header_fmt = "Rogue APs: %d",
    .chanhop = true,
    .sort_by_metric = true,
    .parse = rogue_parse,
    .label = rogue_label,
};

void wifi_marauder_scene_rogue_mon_on_enter(void* context) {
    mm_monitor_start(context, &s_def);
}

bool wifi_marauder_scene_rogue_mon_on_event(void* context, SceneManagerEvent event) {
    if(event.type == SceneManagerEventTypeTick) {
        mm_monitor_tick(context, &s_def);
        return true;
    }
    return false;
}

void wifi_marauder_scene_rogue_mon_on_exit(void* context) {
    mm_monitor_stop(context, &s_def);
}
