#include "wifi_marauder_ep.h"

// Bound the buffer we allocate for a portal page. The ESP truncates anything
// beyond its own MAX_HTML_SIZE (configs.h: 30000 with PSRAM) in
// EvilPortal::setHtmlFromSerial, so a larger file is both a needless allocation
// and pointless to send. Rejecting keeps us from mallocing an uncapped size and
// aborting (no MMU -> reboot) on a giant file.
#define MARAUDER_EP_MAX_HTML 30000

// returns success (if true, then caller needs to free(the_html))
bool wifi_marauder_ep_read_html_file(WifiMarauderApp* app, uint8_t** the_html, size_t* html_size) {
    // All handles declared up front so the single cleanup path frees whatever
    // was allocated, on any early return.
    bool success = false;
    FuriString* predefined_filepath = furi_string_alloc_set_str(MARAUDER_APP_FOLDER_HTML);
    FuriString* selected_filepath = furi_string_alloc();
    File* index_html = NULL;
    uint8_t* buf = NULL;

    DialogsFileBrowserOptions browser_options;
    dialog_file_browser_set_basic_options(&browser_options, ".html", &I_Text_10x10);
    if(!dialog_file_browser_show(
           app->dialogs, selected_filepath, predefined_filepath, &browser_options)) {
        goto cleanup; // user cancelled the picker
    }

    index_html = storage_file_alloc(app->storage);
    if(!storage_file_open(
           index_html, furi_string_get_cstr(selected_filepath), FSAM_READ, FSOM_OPEN_EXISTING)) {
        dialog_message_show_storage_error(app->dialogs, "Cannot open file");
        goto cleanup;
    }

    uint64_t size = storage_file_size(index_html);
    if(size == 0) {
        dialog_message_show_storage_error(app->dialogs, "HTML file is empty");
        goto cleanup;
    }
    if(size > MARAUDER_EP_MAX_HTML) {
        dialog_message_show_storage_error(app->dialogs, "HTML file too large");
        goto cleanup;
    }

    buf = malloc(size);
    if(!buf) {
        dialog_message_show_storage_error(app->dialogs, "Out of memory");
        goto cleanup;
    }

    uint8_t* buf_ptr = buf;
    size_t read = 0;
    while(read < size) {
        size_t to_read = size - read;
        if(to_read > UINT16_MAX) to_read = UINT16_MAX;
        uint16_t now_read = storage_file_read(index_html, buf_ptr, (uint16_t)to_read);
        if(now_read == 0) break; // read error / short file: don't spin forever
        read += now_read;
        buf_ptr += now_read;
    }
    if(read == 0) {
        dialog_message_show_storage_error(app->dialogs, "Cannot read file");
        goto cleanup;
    }

    *the_html = buf; // handed to caller, who frees it
    *html_size = read;
    buf = NULL; // ownership transferred; don't free below
    success = true;

cleanup:
    free(buf);
    if(index_html) {
        storage_file_close(index_html);
        storage_file_free(index_html);
    }
    furi_string_free(selected_filepath);
    furi_string_free(predefined_filepath);
    return success;
}