// Host-side unit test for the `join` output parsers. The security-critical
// property is that no line carrying the plaintext password is ever eligible to
// display -- mm_join_line_is_noise() must reject every such line.
// Builds+runs via tools/run_tests.sh.
#include "../marauders_mate_ap_parser.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, ...)                               \
    do {                                               \
        if(!(cond)) {                                  \
            printf("FAIL: ");                          \
            printf(__VA_ARGS__);                       \
            printf("  (%s:%d)\n", __FILE__, __LINE__); \
            failures++;                                \
        }                                              \
    } while(0)

static const char* SECRET = "hunter2secretpw";

int main(void) {
    // --- password-bearing lines MUST be suppressed --------------------------
    char cmd_echo[64];
    snprintf(cmd_echo, sizeof(cmd_echo), "> #join -a 15 -p %s", SECRET);
    CHECK(mm_join_line_is_noise(cmd_echo), "command echo with password must be noise");

    char using[80];
    snprintf(using, sizeof(using), "Using SSID: HomeNet Password: %s", SECRET);
    CHECK(mm_join_line_is_noise(using), "'Using SSID ... Password:' must be noise");

    char pwval[40];
    snprintf(pwval, sizeof(pwval), "Value: %s", SECRET);
    CHECK(mm_join_line_is_noise(pwval), "settings 'Value:' line (ClientPW) must be noise");

    // settings-dump scaffolding suppressed too
    CHECK(mm_join_line_is_noise("Name: ClientPW"), "'Name:' is noise");
    CHECK(mm_join_line_is_noise("Type: String"), "'Type:' is noise");
    CHECK(mm_join_line_is_noise("Settings"), "'Settings' is noise");
    CHECK(mm_join_line_is_noise("        ----------------------------------------"),
          "separator is noise");
    CHECK(mm_join_line_is_noise(""), "blank is noise");
    CHECK(mm_join_line_is_noise("   "), "whitespace-only is noise");

    // --- genuine status lines must be shown (not noise) ----------------------
    CHECK(!mm_join_line_is_noise("Connecting to WiFi...."), "status line must display");
    CHECK(!mm_join_line_is_noise("Connected! IP: 192.168.0.42"), "success line must display");

    // --- classification ------------------------------------------------------
    CHECK(mm_join_classify("Connecting to WiFi....") == MMJoinLineConnecting, "connecting");
    CHECK(mm_join_classify("WiFi connected") == MMJoinLineConnected, "connected");
    CHECK(mm_join_classify("Got IP 192.168.0.42") == MMJoinLineConnected, "got ip -> connected");
    CHECK(mm_join_classify("Connection failed") == MMJoinLineFailed, "failed");
    CHECK(mm_join_classify("Could not connect to WiFi network") == MMJoinLineFailed,
          "'Could not connect' (real Marauder failure line) -> failed");
    CHECK(mm_join_classify("Disconnected") == MMJoinLineFailed, "disconnect -> failed");
    // The real failure line must also be displayable (not suppressed as noise).
    CHECK(!mm_join_line_is_noise("Could not connect to WiFi network"),
          "failure line must display");
    CHECK(mm_join_classify("random noise") == MMJoinLineNone, "none");

    // --- IP extraction -------------------------------------------------------
    char ip[16];
    CHECK(mm_join_parse_ip("Connected! IP: 192.168.0.42", ip) && strcmp(ip, "192.168.0.42") == 0,
          "extracts a real IP (got '%s')", ip);
    CHECK(!mm_join_parse_ip("IP: 0.0.0.0", ip), "0.0.0.0 is not a real assignment");
    CHECK(!mm_join_parse_ip("Connecting to WiFi....", ip), "no IP where there is none");
    CHECK(!mm_join_parse_ip("Value: 999.1.1.1", ip), "rejects out-of-range octet");

    // --- defense in depth: a shown line never contains the secret ------------
    const char* lines[] = {cmd_echo, using, pwval, "Connecting to WiFi....", "Connected"};
    for(size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
        if(!mm_join_line_is_noise(lines[i])) {
            CHECK(strstr(lines[i], SECRET) == NULL,
                  "a displayable line must not contain the password: '%s'", lines[i]);
        }
    }

    // --- pingscan streamed host lines ---------------------------------------
    char pip[16];
    CHECK(mm_hostscan_parse_ip("> 192.168.0.101", pip) && strcmp(pip, "192.168.0.101") == 0,
          "bare IP with '> ' prompt (got '%s')", pip);
    CHECK(mm_hostscan_parse_ip("192.168.0.143", pip) && strcmp(pip, "192.168.0.143") == 0,
          "bare IP");
    CHECK(!mm_hostscan_parse_ip("IP address: 192.168.0.139", pip),
          "header 'IP address:' must be rejected (not a bare IP)");
    CHECK(!mm_hostscan_parse_ip("Gateway: 192.168.0.1", pip), "header 'Gateway:' rejected");
    CHECK(!mm_hostscan_parse_ip("MAC: DE:AD:BE:EF:FE:ED", pip), "header 'MAC:' rejected");
    CHECK(!mm_hostscan_parse_ip("Starting Ping Scan with...", pip), "status line rejected");
    CHECK(!mm_hostscan_parse_ip("[0] 192.168.0.101", pip),
          "list -i row (has [n]) is not a bare pingscan line");

    // --- portscan open-port lines -------------------------------------------
    int prt;
    CHECK(mm_portscan_parse_open("192.168.0.101: 22", &prt) && prt == 22, "open port (got %d)", prt);
    CHECK(mm_portscan_parse_open("> 192.168.0.101: 443", &prt) && prt == 443, "with prompt");
    CHECK(!mm_portscan_parse_open("Checking IP: 192.168.0.101 Port: 1000", &prt),
          "progress line is not an open port");
    CHECK(!mm_portscan_parse_open("IP address: 192.168.0.139", &prt), "header is not an open port");
    CHECK(!mm_portscan_parse_open("192.168.0.101: 99999", &prt), "out-of-range port rejected");
    CHECK(strcmp(mm_port_service_name(22), "SSH") == 0, "22 -> SSH");
    CHECK(strcmp(mm_port_service_name(443), "HTTPS") == 0, "443 -> HTTPS");
    CHECK(mm_port_service_name(12345)[0] == '\0', "unknown port -> empty");

    // --- Bluetooth support probe --------------------------------------------
    CHECK(mm_bt_line_unsupported("Bluetooth not supported"), "S2 no-BT reply detected");
    CHECK(mm_bt_line_unsupported("> Bluetooth not supported"), "with prompt prefix");
    CHECK(!mm_bt_line_unsupported("Starting Bluetooth sniff"), "BT-capable start line is not 'no'");
    CHECK(!mm_bt_line_unsupported("Sniffing..."), "unrelated line is not 'no'");

    if(failures == 0) {
        printf("join_parser_test: OK\n");
        return 0;
    }
    printf("join_parser_test: %d failure(s)\n", failures);
    return 1;
}
