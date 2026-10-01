#include "wifi_marauder_app_i.h"
#include "wifi_marauder_uart.h"

#define UART_CH (FuriHalSerialIdUsart)
#define BAUDRATE (115200)
// Direct GPS: the Flipper's LPUART1 on header pins 15/16. On the dual-radio
// bench unit this pair is wired to the GPS receiver (NMEA @115200), independent
// of the C5 Marauder on USART1/13-14. See docs/MULTI_RADIO.md (M0).
#define MM_GPS_UART_CH (FuriHalSerialIdLpuart)
#define GPS_BAUDRATE (115200)
// LPUART1 is the Flipper's DEFAULT serial log console (FuriHalRtcLogDeviceLpuart).
// The log owns the channel, so acquiring it faults unless we move the console off
// the serial line first. 230400 is the stock log baud; used to restore on exit.
#define MM_GPS_LOG_BAUD (230400)

struct WifiMarauderUart {
    WifiMarauderApp* app;
    FuriThread* rx_thread;
    FuriStreamBuffer* rx_stream;
    FuriStreamBuffer* pcap_stream;
    FuriHalSerialHandle* serial_handle;
    bool pcap;
    uint8_t mark_test_buf[11];
    uint8_t mark_test_idx;
    uint8_t rx_buf[RX_BUF_SIZE + 1];
    void (*handle_rx_data_cb)(uint8_t* buf, size_t len, void* context);
    void (*handle_rx_pcap_cb)(uint8_t* buf, size_t len, void* context);
};

typedef enum {
    WorkerEvtStop = (1 << 0),
    WorkerEvtRxDone = (1 << 1),
    WorkerEvtPcapDone = (1 << 2),
} WorkerEvtFlags;

void wifi_marauder_uart_set_handle_rx_data_cb(
    WifiMarauderUart* uart,
    void (*handle_rx_data_cb)(uint8_t* buf, size_t len, void* context)) {
    furi_assert(uart);
    uart->handle_rx_data_cb = handle_rx_data_cb;
}

void wifi_marauder_uart_set_handle_rx_pcap_cb(
    WifiMarauderUart* uart,
    void (*handle_rx_pcap_cb)(uint8_t* buf, size_t len, void* context)) {
    furi_assert(uart);
    uart->handle_rx_pcap_cb = handle_rx_pcap_cb;
}

#define WORKER_ALL_RX_EVENTS (WorkerEvtStop | WorkerEvtRxDone | WorkerEvtPcapDone)

// Shared RX ISR body. The firmware's async_rx_start furi_check()s that the USART
// and LPUART have DIFFERENT rx_byte_callback pointers, so the two channels must
// register two distinct function addresses -- hence the thin wrappers below,
// both forwarding here. (context distinguishes which UART instance fired.)
static void wifi_marauder_uart_on_irq_impl(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    void* context) {
    WifiMarauderUart* uart = (WifiMarauderUart*)context;

    if(event == FuriHalSerialRxEventData) {
        uint8_t data = furi_hal_serial_async_rx(handle);
        const char* mark_begin = "[BUF/BEGIN]";
        const char* mark_close = "[BUF/CLOSE]";
        if(uart->mark_test_idx != 0) {
            // We are trying to match a marker
            if(data == mark_begin[uart->mark_test_idx] ||
               data == mark_close[uart->mark_test_idx]) {
                // Received char matches next char in a marker, append to test buffer
                uart->mark_test_buf[uart->mark_test_idx++] = data;
                if(uart->mark_test_idx == sizeof(uart->mark_test_buf)) {
                    // Test buffer reached max length, parse what marker this is and discard buffer
                    if(!memcmp(
                           uart->mark_test_buf, (void*)mark_begin, sizeof(uart->mark_test_buf))) {
                        uart->pcap = true;
                    } else if(!memcmp(
                                  uart->mark_test_buf,
                                  (void*)mark_close,
                                  sizeof(uart->mark_test_buf))) {
                        uart->pcap = false;
                    }
                    uart->mark_test_idx = 0;
                }
                // Don't pass to stream
                return;
            } else {
                // Received char doesn't match any expected next char, send current test buffer
                if(uart->pcap) {
                    furi_stream_buffer_send(
                        uart->pcap_stream, uart->mark_test_buf, uart->mark_test_idx, 0);
                    furi_thread_flags_set(furi_thread_get_id(uart->rx_thread), WorkerEvtPcapDone);
                } else {
                    furi_stream_buffer_send(
                        uart->rx_stream, uart->mark_test_buf, uart->mark_test_idx, 0);
                    furi_thread_flags_set(furi_thread_get_id(uart->rx_thread), WorkerEvtRxDone);
                }
                // Reset test buffer and try parsing this char from scratch
                uart->mark_test_idx = 0;
            }
        }
        // If we reach here the buffer is empty
        if(data == mark_begin[0]) {
            // Received marker start, append to test buffer
            uart->mark_test_buf[uart->mark_test_idx++] = data;
        } else {
            // Not a marker start and we aren't matching a marker, this is just data
            if(uart->pcap) {
                furi_stream_buffer_send(uart->pcap_stream, &data, 1, 0);
                furi_thread_flags_set(furi_thread_get_id(uart->rx_thread), WorkerEvtPcapDone);
            } else {
                furi_stream_buffer_send(uart->rx_stream, &data, 1, 0);
                furi_thread_flags_set(furi_thread_get_id(uart->rx_thread), WorkerEvtRxDone);
            }
        }
    }
}

// Distinct callback addresses for the two channels (see impl comment above).
static void wifi_marauder_uart_on_irq_cb(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    void* context) {
    wifi_marauder_uart_on_irq_impl(handle, event, context);
}
static void wifi_marauder_gps_on_irq_cb(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    void* context) {
    wifi_marauder_uart_on_irq_impl(handle, event, context);
}

static int32_t uart_worker(void* context) {
    WifiMarauderUart* uart = (void*)context;

    while(1) {
        uint32_t events =
            furi_thread_flags_wait(WORKER_ALL_RX_EVENTS, FuriFlagWaitAny, FuriWaitForever);
        furi_check((events & FuriFlagError) == 0);
        if(events & WorkerEvtStop) break;
        if(events & WorkerEvtRxDone) {
            size_t len = furi_stream_buffer_receive(uart->rx_stream, uart->rx_buf, RX_BUF_SIZE, 0);
            if(len > 0) {
                if(uart->handle_rx_data_cb) uart->handle_rx_data_cb(uart->rx_buf, len, uart->app);
            }
        }
        if(events & WorkerEvtPcapDone) {
            size_t len =
                furi_stream_buffer_receive(uart->pcap_stream, uart->rx_buf, RX_BUF_SIZE, 0);
            if(len > 0) {
                if(uart->handle_rx_pcap_cb) uart->handle_rx_pcap_cb(uart->rx_buf, len, uart->app);
            }
        }
    }

    return 0;
}

void wifi_marauder_uart_tx(WifiMarauderUart* uart, uint8_t* data, size_t len) {
    furi_hal_serial_tx(uart->serial_handle, data, len);
}

// Core UART bring-up, shared by the required Marauder USART and the optional
// direct-GPS LPUART. `optional`: if the channel can't be acquired (the expansion
// service or another app holds it), return NULL instead of the furi_check()
// reboot -- a second UART is a bonus, not a requirement.
static WifiMarauderUart* wifi_marauder_uart_init_ch(
    WifiMarauderApp* app,
    FuriHalSerialId channel,
    const char* thread_name,
    uint32_t baudrate,
    bool optional) {
    // Acquire first so an optional channel can bail before allocating anything.
    FuriHalSerialHandle* handle = furi_hal_serial_control_acquire(channel);
    if(!handle) {
        if(optional) return NULL;
        furi_check(handle); // required channel keeps the original hard-fail
    }

    WifiMarauderUart* uart = malloc(sizeof(WifiMarauderUart));
    // pvPortMalloc does not zero memory; the RX callbacks are read by the IRQ
    // that we arm below, possibly before a scene installs them.
    memset(uart, 0, sizeof(WifiMarauderUart));

    uart->app = app;
    uart->serial_handle = handle;
    uart->rx_stream = furi_stream_buffer_alloc(RX_BUF_SIZE, 1);
    uart->pcap_stream = furi_stream_buffer_alloc(RX_BUF_SIZE, 1);
    uart->rx_thread = furi_thread_alloc();
    furi_thread_set_name(uart->rx_thread, thread_name);
    furi_thread_set_stack_size(uart->rx_thread, 1024);
    furi_thread_set_context(uart->rx_thread, uart);
    furi_thread_set_callback(uart->rx_thread, uart_worker);
    furi_thread_start(uart->rx_thread);
    furi_hal_serial_init(handle, baudrate);
    // The two channels MUST use different callback pointers (firmware asserts it).
    furi_hal_serial_async_rx_start(
        handle,
        channel == FuriHalSerialIdLpuart ? wifi_marauder_gps_on_irq_cb :
                                           wifi_marauder_uart_on_irq_cb,
        uart,
        false);

    return uart;
}

WifiMarauderUart* wifi_marauder_uart_init(
    WifiMarauderApp* app,
    FuriHalSerialId channel,
    const char* thread_name) {
    return wifi_marauder_uart_init_ch(app, channel, thread_name, BAUDRATE, false);
}

WifiMarauderUart* wifi_marauder_usart_init(WifiMarauderApp* app) {
    return wifi_marauder_uart_init(app, UART_CH, "WifiMarauderUartRxThread");
}

WifiMarauderUart* wifi_marauder_uart_gps_init(WifiMarauderApp* app) {
    // Direct GPS on LPUART1 (pins 15/16). Optional: returns NULL if the channel
    // is busy. Same IRQ/marker path as the Marauder UART -- NMEA never contains
    // the [BUF/BEGIN] pcap markers, so every byte flows to the data stream.
    //
    // LPUART is the Flipper's default log console, but we do NOT disable logging
    // ourselves: furi_hal_serial_control_acquire() detaches the log handler from
    // LPUART automatically when it grants the handle. (Calling
    // set_logging_config(FuriHalSerialIdMax, ...) to "disable logging" is a trap:
    // it furi_check()s is_baud_rate_supported() against the invalid handles[Max]
    // and reboots -- confirmed against the firmware HAL. Restore is in _gps_free.)
    return wifi_marauder_uart_init_ch(
        app, MM_GPS_UART_CH, "MarauderGpsRxThread", GPS_BAUDRATE, true);
}

void wifi_marauder_uart_gps_free(WifiMarauderUart* uart) {
    wifi_marauder_uart_free(uart);
    // Release() left LPUART free; reattach the serial log console to it (stock
    // 230400). This id/baud pair is valid, so set_logging_config won't fault.
    furi_hal_serial_control_set_logging_config(MM_GPS_UART_CH, MM_GPS_LOG_BAUD);
}

void wifi_marauder_uart_free(WifiMarauderUart* uart) {
    furi_assert(uart);

    // Disarm the RX IRQ first so no byte arriving mid-teardown can touch the
    // thread or stream buffers we are about to free (no MMU: a stray callback
    // reboots the Flipper). Order: stop-IRQ -> join-thread -> free-streams.
    furi_hal_serial_async_rx_stop(uart->serial_handle);
    furi_hal_serial_deinit(uart->serial_handle);
    furi_hal_serial_control_release(uart->serial_handle);

    furi_thread_flags_set(furi_thread_get_id(uart->rx_thread), WorkerEvtStop);
    furi_thread_join(uart->rx_thread);
    furi_thread_free(uart->rx_thread);

    furi_stream_buffer_free(uart->rx_stream);
    furi_stream_buffer_free(uart->pcap_stream);

    free(uart);
}
