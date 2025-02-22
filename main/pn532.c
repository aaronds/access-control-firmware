#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_log.h"
#include <string.h>

#include "pn532.h"
#include "pn532_registers.h"

static const char* TAG = "pn532";

#define UART_BUF_SIZE (1024)

#define FREE(ptr) \
    if(ptr) { free(ptr); ptr = NULL; }

struct pn532 {
    bool running;                          /*<! Indicates whether pn532 task is running or not */
    uart_port_t port;
    uint16_t scan_interval_ms;
    int uart_rw_timeout_ms;

    TaskHandle_t task_handle;              /*<! Handle of task */
    esp_event_loop_handle_t event_handle;  /*<! Handle of event loop */
    bool scanning;                         /*<! Whether the pn532 is in scanning or idle mode */
    bool tag_was_present_last_time;
};

ESP_EVENT_DEFINE_BASE(PN532_EVENTS);

static void pn532_task(void* arg);

esp_err_t pn532_write_frame(pn532_handle_t pn532, uint8_t* data, size_t data_len);
esp_err_t pn532_send_command(pn532_handle_t pn532, uint8_t command, uint8_t* data, size_t data_len);
esp_err_t pn532_get_response(pn532_handle_t pn532, uint8_t command, uint8_t* data, size_t data_len, TickType_t rsp_timeout);
void pn532_flush_until_empty(pn532_handle_t pn532);



void pn532_wakeup(pn532_handle_t pn532)
{
    uint8_t data[] = {0x55, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uart_write_bytes(pn532->port, data, sizeof(data));
}


esp_err_t pn532_send_command(pn532_handle_t pn532, uint8_t command, uint8_t* data, size_t data_len)
{
    ESP_LOGD(TAG, "pn532_send_command %x", (unsigned int)command);
    uint8_t checksum = 0;

    uint8_t frame_len = data_len + 2; // TFI + PD0

    uint8_t frame_header[] = {
        PN532_PREAMBLE,
        PN532_STARTCODE1,
        PN532_STARTCODE2,
        frame_len,
        ~frame_len + 1,
        PN532_HOSTTOPN532,
        command,
    };

    checksum += frame_header[5];
    checksum += frame_header[6];

    uart_write_bytes(pn532->port, frame_header, sizeof(frame_header));

    for(size_t i=0; i<data_len;i++)
    {
        checksum += data[i];
    }
    checksum = ~checksum + 1;
    uart_write_bytes(pn532->port, data, data_len);

    uint8_t frame_footer[] = {
        checksum,
        PN532_POSTAMBLE
    };

    uart_write_bytes(pn532->port, frame_footer, sizeof(frame_footer));

    ESP_LOGD(TAG, "pn532_send_command wait for ack");
    uint8_t ack_frame[6] = {0};
    int rc = uart_read_bytes(pn532->port, ack_frame, sizeof(ack_frame), pn532->uart_rw_timeout_ms/portTICK_PERIOD_MS);
    if (rc < 0) {
        return rc;
    }
    return ESP_OK;
}

int pn532_get_response(pn532_handle_t pn532, uint8_t command, uint8_t* data, size_t data_len, TickType_t rsp_timeout)
{
    ESP_LOGD(TAG, "pn532_get_response %x", (unsigned int)command);
    int rc = 0;
    uint8_t header;
    while (1)
    {
        rc = uart_read_bytes(pn532->port, &header, sizeof(header), rsp_timeout);
        if (rc <= 0) {
            return rc;
        }

        if (header == PN532_PREAMBLE) {
            continue;
        }else if (header == PN532_STARTCODE2) {
            break;
        }else{
            ESP_LOGE(TAG, "Invalid preamble");
            pn532_flush_until_empty(pn532);
            return ESP_FAIL;
        }
    }

    uint8_t frame_len[2] = {0};
    rc = uart_read_bytes(pn532->port, frame_len, sizeof(frame_len), rsp_timeout);
    if (rc <= 0) {
        ESP_LOGE(TAG, "Incomplete response");
        return ESP_FAIL;
    }
    if (((frame_len[0] + frame_len[1]) & 0xff) != 0) {
        ESP_LOGE(TAG, "Length checksum error");
        pn532_flush_until_empty(pn532);
        return ESP_FAIL;
    }

    if (frame_len[0] < 2) {
        ESP_LOGE(TAG, "No data returned");
        pn532_flush_until_empty(pn532);
        return ESP_FAIL;
    }

    if (frame_len[0]-2 > data_len) {
        ESP_LOGE(TAG, "Data buffer too small");
        pn532_flush_until_empty(pn532);
        return ESP_FAIL;
    }

    uint8_t tfi = 0;
    rc = uart_read_bytes(pn532->port, &tfi, 1, rsp_timeout);
    if (rc != 1) {
        ESP_LOGE(TAG, "Incomplete response");
        return ESP_FAIL;
    }
    uint8_t checksum = tfi;
    frame_len[0] -= 1;

    uint8_t got_command = 0;
    rc = uart_read_bytes(pn532->port, &got_command, 1, rsp_timeout);
    if (rc != 1) {
        ESP_LOGE(TAG, "Incomplete response");
        return ESP_FAIL;
    }
    checksum += got_command;
    frame_len[0] -= 1;

    if (frame_len[0]) {
        rc = uart_read_bytes(pn532->port, data, frame_len[0], rsp_timeout);
        if (rc != frame_len[0]) {
            ESP_LOGE(TAG, "Incomplete response");
            return ESP_FAIL;
        }
        for (size_t i=0; i<frame_len[0]; i++)
        {
            checksum += data[i];
        }
    }


    uint8_t footer[2] = {0};
    rc = uart_read_bytes(pn532->port, footer, sizeof(footer), rsp_timeout);
    if (rc != sizeof(footer)) {
        ESP_LOGE(TAG, "Incomplete response");
        return ESP_FAIL;
    }
    checksum += footer[0];
    if (checksum) {
        ESP_LOGE(TAG, "Data checksum error");
        return ESP_FAIL;
    }

    if (tfi != PN532_PN532TOHOST) {
        ESP_LOGE(TAG, "Unexpected frame identifier");
        return ESP_FAIL;
    }

    if (got_command != command+1) {
        ESP_LOGE(TAG, "Incorrect command ID");
        return ESP_FAIL;
    }

    return frame_len[0];
}

void pn532_flush_until_empty(pn532_handle_t pn532)
{
    ESP_LOGD(TAG, "Flushing RX buffer");
    size_t data_available;
    do {
        uart_flush(pn532->port);
        vTaskDelay(5/portTICK_PERIOD_MS);
        uart_get_buffered_data_len(pn532->port, &data_available);
    } while (data_available);
}

esp_err_t pn532_firmware_version(pn532_handle_t pn532, uint8_t* ic, uint8_t* ver, uint8_t* rev, uint8_t* support)
{
    int rc = pn532_send_command(pn532, PN532_COMMAND_GETFIRMWAREVERSION, NULL, 0);
    if (rc) {
        return rc;
    }
    uint8_t buf[4] = {0};
    rc = pn532_get_response(pn532, PN532_COMMAND_GETFIRMWAREVERSION, buf, 4, pn532->uart_rw_timeout_ms/portTICK_PERIOD_MS);
    if (rc < 0) {
        return rc;
    }
    *ic = buf[0];
    *ver = buf[1];
    *rev = buf[2];
    *support = buf[3];
    return ESP_OK;
}

esp_err_t pn532_power_down(pn532_handle_t pn532) {
    int rc = pn532_send_command(pn532, PN532_COMMAND_POWERDOWN, NULL, 0);

    if (rc) {
        return rc;
    }

    return ESP_OK;
}

esp_err_t pn532_sam_config(pn532_handle_t pn532)
{
    // Send SAM configuration command with configuration for:
    // - 0x01, normal mode
    // - 0x14, timeout 50ms * 20 = 1 second
    // - 0x01, use IRQ pin
    uint8_t sam_config[3] = {0x01, 0x14, 0x01};
    int rc = pn532_send_command(pn532, PN532_COMMAND_SAMCONFIGURATION, sam_config, sizeof(sam_config));
    if (rc) {
        return rc;
    }
    rc = pn532_get_response(pn532, PN532_COMMAND_SAMCONFIGURATION, NULL, 0, pn532->uart_rw_timeout_ms/portTICK_PERIOD_MS);
    if (rc < 0) {
        return rc;
    }
    return ESP_OK;
}

esp_err_t pn532_listen_for_passive_target(pn532_handle_t pn532)
{
    uint8_t inlist_params[] = {0x01, PN532_MIFARE_ISO14443A};
    return pn532_send_command(pn532, PN532_COMMAND_INLISTPASSIVETARGET, inlist_params, sizeof(inlist_params));
}

int pn532_get_passive_target(pn532_handle_t pn532, uint8_t* data, size_t data_len, TickType_t timeout)
{
    uint8_t rsp_buffer[30] = {0};
    int rc = pn532_get_response(pn532, PN532_COMMAND_INLISTPASSIVETARGET, rsp_buffer, sizeof(rsp_buffer), timeout);
    if (rc <= 0) {
        return rc;
    }

    if (rc < 10) {
        ESP_LOGE(TAG, "Get target response too small");
        return -ESP_ERR_INVALID_SIZE;
    }

    if (rsp_buffer[0] != 0x01) {
        ESP_LOGE(TAG, "More than one tag detected");
        return -ESP_ERR_INVALID_SIZE;
    }

    uint8_t uid_len = rsp_buffer[5];

    if (uid_len > 7) {
        ESP_LOGE(TAG, "Found card with unexpectedly long UID");
        return -ESP_ERR_INVALID_SIZE;
    }

    if (uid_len > data_len) {
        ESP_LOGE(TAG, "Data buffer not large enough for tag UID");
        return -ESP_ERR_INVALID_SIZE;
    }
    memcpy(data, &rsp_buffer[6], uid_len);
    return uid_len;

}

esp_err_t pn532_create(pn532_config_t* config, pn532_handle_t* out_pn532)
{
    esp_err_t ret = ESP_OK;

    if(! config || ! out_pn532) {
        return ESP_ERR_INVALID_ARG;
    }

    pn532_handle_t pn532 = NULL;
    pn532 = calloc(1, sizeof(struct pn532));
    if (pn532 == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    pn532->port = config->uart.port;
    pn532->scan_interval_ms = config->scan_interval_ms;
    pn532->uart_rw_timeout_ms = config->uart.rw_timeout_ms;
    pn532->event_handle = config->event_handle;

    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ret = uart_driver_install(pn532->port, UART_BUF_SIZE * 2, 0, 0, NULL, 0);
    if (ret != ESP_OK) { goto error_cleanup; }
    ret = uart_param_config(pn532->port, &uart_config);
    if (ret != ESP_OK) { goto error_cleanup; }
    ret = uart_set_pin(pn532->port, config->uart.tx_gpio, config->uart.rx_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) { goto error_cleanup; }

    pn532->running = true;

    if (pdTRUE != xTaskCreate(pn532_task,
                      "pn532_task",
                      config->task_stack_size,
                      pn532,
                      config->task_priority,
                      &pn532->task_handle))
    {
        ESP_LOGE(TAG, "Failed to create task");
        ret = ESP_FAIL;
        goto error_cleanup;
    }

    *out_pn532 = pn532;
    return ESP_OK;

error_cleanup:
    if (pn532 != NULL)
    {
        pn532_destroy(pn532);
        pn532 = NULL;
    }
    return ret;

}

esp_err_t pn532_start(pn532_handle_t pn532)
{
    esp_err_t ret = ESP_OK;

    if(! pn532) {
        return ESP_ERR_INVALID_ARG;
    }

    if(pn532->scanning) { // Already in scan mode
        return ESP_OK;
    }

    pn532_wakeup(pn532);
    ret = pn532_sam_config(pn532);
    if (ret) {
        ESP_LOGE(TAG, "Error configuring pn532");
        return ret;
    }

    pn532->scanning = true;

    return ESP_OK;
}

esp_err_t pn532_pause(pn532_handle_t pn532)
{
    esp_err_t ret = ESP_OK;

    if(! pn532) {
        return ESP_ERR_INVALID_ARG;
    }

    if(! pn532->scanning) {
        return ESP_OK;
    }

    ret = pn532_power_down(pn532);

    if (ret) {
        ESP_LOGE(TAG, "Error powering down.");
    }

    pn532->scanning = false;

    return ESP_OK;
}

esp_err_t pn532_destroy(pn532_handle_t pn532)
{
    esp_err_t ret = ESP_OK;

    if(! pn532) {
        return ESP_ERR_INVALID_ARG;
    }

    if(xTaskGetCurrentTaskHandle() == pn532->task_handle) {
        ESP_LOGE(TAG, "Cannot destroy rc522 from event handler");

        return ESP_ERR_INVALID_STATE;
    }

    pn532->running = false;

    free(pn532);

    return ret;
}

static void pn532_task(void* arg)
{
    pn532_handle_t pn532 = (pn532_handle_t) arg;

    TickType_t last_wake_time = xTaskGetTickCount();
    int delay_interval_ms = pn532->scan_interval_ms;

    while(pn532->running) {
        esp_err_t ret = ESP_OK;

        if (!pn532->scanning) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            pn532->tag_was_present_last_time = false;
            continue;
        } else if(!xTaskDelayUntil(&last_wake_time, delay_interval_ms/portTICK_PERIOD_MS)) {
            last_wake_time = xTaskGetTickCount();
        }

        delay_interval_ms = pn532->scan_interval_ms;

        ESP_LOGI(TAG, "Scanning for card");
        ret = pn532_listen_for_passive_target(pn532);
        if (ret < 0) {
            ESP_LOGE(TAG, "Error iistening for targets");
            pn532->tag_was_present_last_time = false;
            continue;
        }

        pn532_event_tag_scanned_data_t tag_scanned_evt = {
            .pn532 = pn532,
            .data = {0},
        };

        int num_read = pn532_get_passive_target(pn532, tag_scanned_evt.data, sizeof(tag_scanned_evt.data), delay_interval_ms/portTICK_PERIOD_MS);
        if (num_read == 0) {
            ESP_LOGI(TAG, "No card found");
            pn532->tag_was_present_last_time = false;
            continue;
        }

        if (num_read < 0) {
            ESP_LOGE(TAG, "Error reading card");
            pn532->tag_was_present_last_time = false;
            continue;
        }

        if (num_read != 4) {
            ESP_LOGE(TAG, "Card UID not 4 bytes");
            pn532->tag_was_present_last_time = false;
            continue;
        }

        if (!pn532->tag_was_present_last_time)
        {
            esp_event_post_to(pn532->event_handle, PN532_EVENTS, PN532_EVENT_TAG_SCANNED, &tag_scanned_evt, sizeof(tag_scanned_evt), portMAX_DELAY);
            pn532->tag_was_present_last_time = true;
        }

        delay_interval_ms *= 2; // Rate limit next scan
    }
}
