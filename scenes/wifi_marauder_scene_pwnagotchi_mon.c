// Marauder's Mate: Pwnagotchi monitor -- units by name, sorted by handshakes
// captured. Thin wrapper over the engine. The firmware prints "Name:" then
// "Pwnd #:" (custom firmware adds "MAC:" before Pwnd); pwn_parse pairs the
// lines and commits a record on the "Pwnd #:" line.
#include "../wifi_marauder_app_i.h"
#include "../wifi_marauder_monitor.h"

static bool pwn_parse(const char* line, MMMonRec* out) {
    static char pend_name[33];
    static char pend_mac[18];
    char nm[33];
    char mac[18];
    int pwnd;
    if(mm_pwn_line_name(line, nm, sizeof(nm))) {
        strncpy(pend_name, nm, sizeof(pend_name) - 1);
        pend_name[sizeof(pend_name) - 1] = '\0';
        pend_mac[0] = '\0';
        return false;
    }
    if(mm_pwn_line_mac(line, mac)) {
        strncpy(pend_mac, mac, sizeof(pend_mac) - 1);
        pend_mac[sizeof(pend_mac) - 1] = '\0';
        return false;
    }
    if(mm_pwn_line_pwnd(line, &pwnd) && pend_name[0]) {
        strncpy(out->key, pend_name, sizeof(out->key) - 1);
        strncpy(out->t1, pend_name, sizeof(out->t1) - 1);
        if(pend_mac[0]) strncpy(out->t2, pend_mac + 9, sizeof(out->t2) - 1); // MAC tail
        out->metric = pwnd;
        pend_name[0] = '\0';
        pend_mac[0] = '\0';
        return true;
    }
    return false;
}

static void pwn_label(const MMMonRec* r, char* buf, size_t sz) {
    if(r->t2[0]) {
        snprintf(buf, sz, "%s Pwnd:%d %s", r->t1, r->metric, r->t2);
    } else {
        snprintf(buf, sz, "%s Pwnd:%d", r->t1, r->metric);
    }
}

static const MMMonDef s_def = {
    .cmd = "sniffpwn",
    .waiting = "Listening for Pwnagotchi...",
    .header_fmt = "Pwnagotchi: %d",
    .chanhop = true,
    .sort_by_metric = true,
    .parse = pwn_parse,
    .label = pwn_label,
};

void wifi_marauder_scene_pwnagotchi_mon_on_enter(void* context) {
    mm_monitor_start(context, &s_def);
}

bool wifi_marauder_scene_pwnagotchi_mon_on_event(void* context, SceneManagerEvent event) {
    if(event.type == SceneManagerEventTypeTick) {
        mm_monitor_tick(context, &s_def);
        return true;
    }
    return false;
}

void wifi_marauder_scene_pwnagotchi_mon_on_exit(void* context) {
    mm_monitor_stop(context, &s_def);
}
