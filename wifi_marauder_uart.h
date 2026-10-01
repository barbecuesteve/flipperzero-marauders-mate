#pragma once

#include "furi_hal.h"

#define RX_BUF_SIZE (2048)

typedef struct WifiMarauderUart WifiMarauderUart;

void wifi_marauder_uart_set_handle_rx_data_cb(
    WifiMarauderUart* uart,
    void (*handle_rx_data_cb)(uint8_t* buf, size_t len, void* context));
void wifi_marauder_uart_set_handle_rx_pcap_cb(
    WifiMarauderUart* uart,
    void (*handle_rx_pcap_cb)(uint8_t* buf, size_t len, void* context));
void wifi_marauder_uart_tx(WifiMarauderUart* uart, uint8_t* data, size_t len);
WifiMarauderUart* wifi_marauder_usart_init(WifiMarauderApp* app);
// Optional direct-GPS UART on LPUART1 (pins 15/16). Returns NULL if the channel
// can't be acquired -- callers must tolerate NULL. See docs/MULTI_RADIO.md.
WifiMarauderUart* wifi_marauder_uart_gps_init(WifiMarauderApp* app);
void wifi_marauder_uart_free(WifiMarauderUart* uart);
// Free the direct-GPS UART and hand LPUART back to the serial log console.
void wifi_marauder_uart_gps_free(WifiMarauderUart* uart);
