// Marauder's Mate: deauth/disassoc monitor -- attack flows by (src->dst) pair,
// "who's deauthing whom". Thin wrapper over the shared monitor engine.
#include "../wifi_marauder_app_i.h"
#include "../wifi_marauder_monitor.h"

static bool deauth_parse(const char* line, MMMonRec* out) {
    MMDeauthFrame f;
    if(!mm_deauth_parse_line(line, &f)) return false;
    snprintf(out->key, sizeof(out->key), "%s>%s", f.src, f.dst);
    strncpy(out->t1, f.src + 9, sizeof(out->t1) - 1); // src last 3 octets
    if(mm_mac_is_broadcast(f.dst)) {
        strncpy(out->t2, "bcast", sizeof(out->t2) - 1);
    } else {
        strncpy(out->t2, f.dst + 9, sizeof(out->t2) - 1);
    }
    out->channel = f.channel;
    out->rssi = f.rssi;
    return true;
}

static void deauth_label(const MMMonRec* r, char* buf, size_t sz) {
    snprintf(buf, sz, "%lux %s>%s", (unsigned long)r->hits, r->t1, r->t2);
}

static const MMMonDef s_def = {
    .cmd = "sniffdeauth",
    .waiting = "Listening for deauths...",
    .header_fmt = "Deauth: %d flows",
    .chanhop = true,
    .sort_by_metric = false,
    .parse = deauth_parse,
    .label = deauth_label,
};

void wifi_marauder_scene_deauth_mon_on_enter(void* context) {
    mm_monitor_start(context, &s_def);
}

bool wifi_marauder_scene_deauth_mon_on_event(void* context, SceneManagerEvent event) {
    if(event.type == SceneManagerEventTypeTick) {
        mm_monitor_tick(context, &s_def);
        return true;
    }
    return false;
}

void wifi_marauder_scene_deauth_mon_on_exit(void* context) {
    mm_monitor_stop(context, &s_def);
}
