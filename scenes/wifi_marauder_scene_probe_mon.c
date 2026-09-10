// Marauder's Mate: probe-request monitor -- client devices by probe count,
// showing the last SSID each device sought. Thin wrapper over the engine.
#include "../wifi_marauder_app_i.h"
#include "../wifi_marauder_monitor.h"

static bool probe_parse(const char* line, MMMonRec* out) {
    MMScanAp c; // .bssid holds the client MAC; .ssid the requested network
    if(!mm_probe_parse_line(line, &c)) return false;
    strncpy(out->key, c.bssid, sizeof(out->key) - 1);
    strncpy(out->t1, c.bssid, sizeof(out->t1) - 1);
    if(!c.hidden && c.ssid[0]) strncpy(out->t2, c.ssid, sizeof(out->t2) - 1);
    return true;
}

static void probe_label(const MMMonRec* r, char* buf, size_t sz) {
    if(r->t2[0]) {
        snprintf(buf, sz, "%lux %s ->%s", (unsigned long)r->hits, r->t1, r->t2);
    } else {
        snprintf(buf, sz, "%lux %s", (unsigned long)r->hits, r->t1);
    }
}

static const MMMonDef s_def = {
    .cmd = "sniffprobe",
    .waiting = "Probe scan...",
    .header_fmt = "Probes: %d clients",
    .chanhop = true,
    .sort_by_metric = false,
    .parse = probe_parse,
    .label = probe_label,
};

void wifi_marauder_scene_probe_mon_on_enter(void* context) {
    mm_monitor_start(context, &s_def);
}

bool wifi_marauder_scene_probe_mon_on_event(void* context, SceneManagerEvent event) {
    if(event.type == SceneManagerEventTypeTick) {
        mm_monitor_tick(context, &s_def);
        return true;
    }
    return false;
}

void wifi_marauder_scene_probe_mon_on_exit(void* context) {
    mm_monitor_stop(context, &s_def);
}
