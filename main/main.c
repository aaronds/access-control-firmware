#include <stdio.h>

#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_ota_ops.h"

#include "wifi_manage.h"
#include "http_api.h"
#include "pn532.h"
#include "status_indicator.h"
#include "gpio_control.h"
#include "syslog.h"
#include "monitor.h"

static const char* TAG = "main";

typedef enum {
    CONTROLLER_MODE_INITIALISING,
    CONTROLLER_MODE_LOCKED,
    CONTROLLER_MODE_UNLOCKED,
    CONTROLLER_MODE_IN_USE,
    CONTROLLER_MODE_AWAIT_INDUCTOR,
    CONTROLLER_MODE_ENROLL,
} controller_mode_t;

static controller_mode_t controller_mode = CONTROLLER_MODE_INITIALISING;
static uint8_t inductor_tag[4];

void controller_lock(void)
{
    status_indicator_idle();
    gpio_set_relay(false);
    controller_mode = CONTROLLER_MODE_LOCKED;
}

void controller_try_unlock(pn532_event_tag_scanned_data_t* tag)
{
    esp_err_t ret = http_api_unlock(tag->data, sizeof(tag->data));
    if (ret == ESP_OK) {
        status_indicator_output_on();
        gpio_set_relay(true);
        controller_mode = CONTROLLER_MODE_UNLOCKED;
    } else {
        status_indicator_error_brief();
    }
}

void controller_await_inductor(void)
{
    status_indicator_await_inductor();
    controller_mode = CONTROLLER_MODE_AWAIT_INDUCTOR;
}

void controller_verify_inductor(pn532_event_tag_scanned_data_t* tag)
{
    esp_err_t ret = http_api_enroll(tag->data, sizeof(tag->data), NULL, 0);
    if (ret == ESP_OK) {
        status_indicator_enroll();
        memcpy(inductor_tag, tag->data, sizeof(tag->data));
        controller_mode = CONTROLLER_MODE_ENROLL;
    } else {
        status_indicator_error_brief();
    }
}

void controller_enroll_member(pn532_event_tag_scanned_data_t* tag)
{
    // Ignore inductor tag
    if (memcmp(inductor_tag, tag->data, sizeof(tag->data)) == 0)
    {
        return;
    }

    esp_err_t ret = http_api_enroll(inductor_tag, sizeof(inductor_tag), tag->data, sizeof(tag->data));
    if (ret == ESP_OK) {
        status_indicator_enroll_success();
        controller_mode = CONTROLLER_MODE_ENROLL;
    } else {
        status_indicator_error_brief();
    }
}

void on_tag_scanned(void* handler_arg, esp_event_base_t base, int32_t id, void* event_data)
{
    pn532_event_tag_scanned_data_t* tag = (pn532_event_tag_scanned_data_t*)event_data;

    switch (controller_mode) {
    case CONTROLLER_MODE_LOCKED:
        controller_try_unlock(tag);
        break;
    case CONTROLLER_MODE_AWAIT_INDUCTOR:
        controller_verify_inductor(tag);
        break;
    case CONTROLLER_MODE_ENROLL:
        controller_enroll_member(tag);
        break;
    default:
        break;
    }
}

void on_button_pressed(void* handler_arg, esp_event_base_t base, int32_t id, void* event_data)
{
    switch (controller_mode) {
    case CONTROLLER_MODE_UNLOCKED:
        controller_lock();
        /* fallthrough */
    case CONTROLLER_MODE_LOCKED:
    case CONTROLLER_MODE_ENROLL:
        controller_await_inductor();
        break;
    default:
        break;
    }
}

void on_button_released(void* handler_arg, esp_event_base_t base, int32_t id, void* event_data)
{
    switch (controller_mode) {
    case CONTROLLER_MODE_AWAIT_INDUCTOR:
        controller_lock();
        break;
    default:
        break;
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_sta();

    ESP_ERROR_CHECK(http_api_init());

    status_indicator_init();

    syslog_udp_init();
    esp_log_set_vprintf(&syslog_vprintf);
    esp_log_level_set("*", ESP_LOG_INFO);

    for (int ota_attempts = 0; ota_attempts < 5; ota_attempts++) {
        char update_url[128] = {0};
        ret = http_api_has_update(update_url, sizeof(update_url));
        if (ret) {
            vTaskDelay(2000/portTICK_PERIOD_MS);
            continue;
        }

        if(strlen(update_url)) {
            ESP_LOGI(TAG, "Update required from: %s", update_url);
            ret = http_api_ota(update_url);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "OTA failed");
            }
        } else {
            esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
            const esp_partition_t* running_partition = esp_ota_get_running_partition();
            esp_ota_get_state_partition(running_partition, &state);
            if (state == ESP_OTA_IMG_PENDING_VERIFY) {
                esp_ota_mark_app_valid_cancel_rollback();
                ESP_LOGI(TAG, "Update successful, cancelling rollback");
            }
        }
        break;
    }

    esp_event_loop_args_t app_event_config = {
        .queue_size = 32,
        .task_name = NULL
    };
    esp_event_loop_handle_t app_events;
    esp_event_loop_create(&app_event_config, &app_events);

    esp_event_handler_register_with(app_events, PN532_EVENTS, PN532_EVENT_TAG_SCANNED, on_tag_scanned, NULL);
    esp_event_handler_register_with(app_events, GPIO_EVENTS, GPIO_EVENT_BUTTON_PRESS, on_button_pressed, NULL);
    esp_event_handler_register_with(app_events, GPIO_EVENTS, GPIO_EVENT_BUTTON_RELEASE, on_button_released, NULL);

    pn532_config_t pn532_config = {
        .task_priority = tskIDLE_PRIORITY,
        .task_stack_size = CONFIG_NFC_TASK_STACK_SIZE,
        .scan_interval_ms = 2000,
        .event_handle = app_events,
        .uart = {
            .rw_timeout_ms = 500,
            .port = UART_NUM_0,
            .rx_gpio = 21,
            .tx_gpio = 22,
        }
    };

    pn532_handle_t pn532 = NULL;
    ESP_ERROR_CHECK(pn532_create(&pn532_config, &pn532));
    ESP_ERROR_CHECK(pn532_start(pn532));

    ESP_ERROR_CHECK(gpio_control_init(app_events));

    controller_lock();

    if (monitor_init() == ESP_OK) {
        ESP_LOGI(TAG, "Monitor Init OK");
    } else {
        ESP_LOGI(TAG, "Monitor Init Failed");
    }

    monitor_start();

    while (1)
    {
        ret = esp_event_loop_run(app_events, portMAX_DELAY);
        if (ret != ESP_OK) {
            break;
        }
    }
}
