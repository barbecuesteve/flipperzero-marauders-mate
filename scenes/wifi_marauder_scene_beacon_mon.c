// Marauder's Mate: beacon activity monitor (a beacon-spam detector: floods
// rocket to the top by count). Thin wrapper over the shared monitor engine.
#include "../wifi_marauder_app_i.h"
#include "../wifi_marauder_monitor.h"

static bool beacon_parse(const char* line, MMMonRec* out) {
    MMScanAp ap;
    if(!mm_beacon_parse_line(line, &ap)) return false;
    strncpy(out->key, ap.bssid, sizeof(out->key) - 1);
    strncpy(out->t1, ap.hidden ? "[hidden]" : ap.ssid, sizeof(out->t1) - 1);
    out->channel = ap.channel;
    return true;
}

static void beacon_label(const MMMonRec* r, char* buf, size_t sz) {
    snprintf(buf, sz, "%lux %s C%d", (unsigned long)r->hits, r->t1, r->channel);
}

static const MMMonDef s_def = {
    .cmd = "sniffbeacon",
    .waiting = "Listening for beacons...",
    .header_fmt = "Beacons: %d APs",
    .chanhop = true,
    .sort_by_metric = false,
    .parse = beacon_parse,
    .label = beacon_label,
};

void wifi_marauder_scene_beacon_mon_on_enter(void* context) {
    mm_monitor_start(context, &s_def);
}

bool wifi_marauder_scene_beacon_mon_on_event(void* context, SceneManagerEvent event) {
    if(event.type == SceneManagerEventTypeTick) {
        mm_monitor_tick(context, &s_def);
        return true;
    }
    return false;
}

void wifi_marauder_scene_beacon_mon_on_exit(void* context) {
    mm_monitor_stop(context, &s_def);
}
