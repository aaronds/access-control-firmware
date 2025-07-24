#pragma once

#include <esp_event.h>
#include <driver/uart.h>

ESP_EVENT_DECLARE_BASE(PN532_EVENTS);

typedef struct pn532* pn532_handle_t;

typedef struct {
    uint16_t scan_interval_ms;         /*<! How fast will ESP32 scan for nearby tags, in miliseconds */
    size_t task_stack_size;            /*<! Stack size of rc522 task */
    uint8_t task_priority;             /*<! Priority of rc522 task */
    esp_event_loop_handle_t event_handle;
    struct {
        uart_port_t port;
        int tx_gpio;
        int rx_gpio;
        int rw_timeout_ms;
    } uart;
} pn532_config_t;

typedef enum {
    PN532_EVENT_ANY = ESP_EVENT_ANY_ID,
    PN532_EVENT_NONE,
    PN532_EVENT_TAG_SCANNED,             /*<! Tag scanned */
} pn532_event_t;

typedef struct {
    pn532_handle_t pn532;
    uint8_t data[4];
} pn532_event_tag_scanned_data_t;

esp_err_t pn532_create(pn532_config_t* config, pn532_handle_t* out_pn532);

esp_err_t pn532_start(pn532_handle_t pn532);

esp_err_t pn532_firmware_version(pn532_handle_t pn532, uint8_t* ic, uint8_t* ver, uint8_t* rev, uint8_t* support);

esp_err_t pn532_pause(pn532_handle_t pn532);

esp_err_t pn532_restart(pn532_handle_t pn532);

esp_err_t pn532_destroy(pn532_handle_t pn532);
