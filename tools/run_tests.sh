#!/bin/sh
# Host-side tests for Marauder's Mate pure logic (no Flipper hardware needed).
set -e
cd "$(dirname "$0")/.."
cc -Wall -Wextra -std=c11 tools/ap_parser_test.c marauders_mate_ap_parser.c -o /tmp/mm_test_ap_parser
/tmp/mm_test_ap_parser
cc -Wall -Wextra -std=c11 tools/scanall_parser_test.c marauders_mate_ap_parser.c -o /tmp/mm_test_scanall
/tmp/mm_test_scanall
