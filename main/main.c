#include <stdio.h>

#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"

#include "wifi_manage.h"
#include "http_api.h"
#include "pn532.h"
#include "status_indicator.h"
#include "gpio_control.h"
#include "syslog.h"
#include "monitor.h"
#include "mqtt-sn.h"

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
uint64_t controller_mode_time = 0;
uint64_t controller_used_after_time = 0;
unsigned int controller_used_threshold = 10;
unsigned int controller_unlocked_timeout = 30;

typedef struct {
    uint32_t time_remaining;
    uint32_t unlocked_timeout;
    controller_mode_t mode;
    struct {
        bool is_on : 1;
        bool is_used : 1;
        bool monitor_enabled : 1;
    } flags ;
} monitor_mode_t;

bool controller_used = false;

static uint8_t inductor_tag[4];

uint32_t monitor_energy_total = 0;
uint32_t monitor_power = 0;
bool monitor_is_on = false;
bool monitor_enabled = false;

monitor_mode_t mode_message;

TaskHandle_t status_task_handle;

void status_task(void *arg) {
    char statusChar;
    int64_t now;
    uint32_t time_remaining = 0;
    uint32_t time_since_used = 0;

    while(true) {
        now = esp_timer_get_time();
        switch (controller_mode) {
            case CONTROLLER_MODE_INITIALISING:
                statusChar = 'I';
                break;

            case CONTROLLER_MODE_LOCKED:
                statusChar = 'L';
                break;

            case CONTROLLER_MODE_UNLOCKED:
                statusChar = 'U';
                break;

            case CONTROLLER_MODE_IN_USE:
                statusChar = 'A';
                break;

            case CONTROLLER_MODE_AWAIT_INDUCTOR:
                statusChar = 'D';
                break;

            case CONTROLLER_MODE_ENROLL:
                statusChar = 'E';
                break;

            default:
                statusChar = 'X';
                break;
        }

        if (controller_mode == CONTROLLER_MODE_UNLOCKED && controller_used && controller_unlocked_timeout > 0) { 
            time_since_used = ((now - controller_used_after_time) / 1000000);

            if (time_since_used > controller_unlocked_timeout) {
                time_remaining = 0;
            } else {
                time_remaining = ((uint32_t )controller_unlocked_timeout) - time_since_used;
            }
            
            ESP_LOGD(TAG, "Time Since Used: %ld > %d", time_since_used, controller_unlocked_timeout);

        } else {
            time_remaining = 9999;
        }

        ESP_LOGI(TAG, "status=%c is_on=%d energy_total=%ld power=%ld used=%d time_remaing=%ld", statusChar, monitor_is_on, monitor_energy_total, monitor_power, controller_used, time_remaining);
        monitor_energy_total = 0;
        memset(&mode_message, 0, sizeof(mode_message));
        mode_message.flags.is_on = monitor_is_on;
        mode_message.flags.is_used = controller_used;
        mode_message.flags.monitor_enabled = monitor_enabled; 
        mode_message.mode = controller_mode;
        mode_message.time_remaining = time_remaining;
        mode_message.unlocked_timeout = controller_unlocked_timeout;
        mqtt_sn_send_with_mac(MQTT_SN_MESSAGE_MODE, &mode_message, sizeof(mode_message));

        ulTaskNotifyTake(pdTRUE, 30000/portTICK_PERIOD_MS);
    }
}

void controller_mode_set(controller_mode_t mode_new) {
   controller_mode = mode_new;
   controller_mode_time = esp_timer_get_time(); 
   xTaskNotifyGive(status_task_handle);
}

void controller_lock(void)
{
    status_indicator_idle();
    gpio_set_relay(false);
    controller_mode_set(CONTROLLER_MODE_LOCKED);
    controller_used = false;
}

void controller_try_unlock(pn532_event_tag_scanned_data_t* tag)
{
    esp_err_t ret = http_api_unlock(tag->data, sizeof(tag->data));
    if (ret == ESP_OK) {
        status_indicator_output_on();
        gpio_set_relay(true);
        controller_mode_set(CONTROLLER_MODE_UNLOCKED);
        controller_used = false;
    } else {
        status_indicator_error_brief();
    }
}

void controller_await_inductor(void)
{
    status_indicator_await_inductor();
    controller_mode_set(CONTROLLER_MODE_AWAIT_INDUCTOR);
}

void controller_verify_inductor(pn532_event_tag_scanned_data_t* tag)
{
    esp_err_t ret = http_api_enroll(tag->data, sizeof(tag->data), NULL, 0);
    if (ret == ESP_OK) {
        status_indicator_enroll();
        memcpy(inductor_tag, tag->data, sizeof(tag->data));
        controller_mode_set(CONTROLLER_MODE_ENROLL);
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
        controller_mode_set(CONTROLLER_MODE_ENROLL);
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
    case CONTROLLER_MODE_IN_USE:
    case CONTROLLER_MODE_UNLOCKED:
        controller_used_after_time = esp_timer_get_time();
        break;
        
    default:
        break;
    }
}

void on_button_pressed(void* handler_arg, esp_event_base_t base, int32_t id, void* event_data)
{
    switch (controller_mode) {
    case CONTROLLER_MODE_IN_USE:
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

void on_monitor_state(void *handler_arg, esp_event_base_t base, int32_t id, void* event_data)
{
    monitor_state_t *state = (monitor_state_t *) event_data;
    int64_t now = esp_timer_get_time();
    bool error_hold = false;
    uint32_t unlocked_time = 0; 

    /* Ignore error conditions if state has just changed */

    if (now > controller_mode_time && now - controller_mode_time < 20000) {
        error_hold = true; 
    }

    if (state->energy > 0) {
        monitor_energy_total += state->energy;
        monitor_power = state->power;
    } else {
        monitor_power = 0;
    }

    monitor_is_on = state->is_on;

    switch (controller_mode) {
        case CONTROLLER_MODE_INITIALISING:
            if (!error_hold && state->is_on) {
                ESP_LOGE(TAG, "Initialising, power already on.");
            }
            break;

        case CONTROLLER_MODE_LOCKED:
            if (!error_hold && state->is_on) { 
                ESP_LOGE(TAG, "Locked but power on.");
            }
            break;

        case CONTROLLER_MODE_UNLOCKED:
            if (!error_hold && !state->is_on) {
                ESP_LOGE(TAG, "Unlocked power failed.");
            }

            if (!error_hold && controller_used_threshold > 0 && state->power >= controller_used_threshold) {
                controller_used = true;
                controller_used_after_time = now;
                controller_mode_set(CONTROLLER_MODE_IN_USE);

            } else if (controller_used) {
                unlocked_time = (now - controller_used_after_time) / 1000000;

                ESP_LOGD(TAG, "Is Unlocked time: %ld > %d", unlocked_time, controller_unlocked_timeout);

                if (unlocked_time > controller_unlocked_timeout) {
                    ESP_LOGI(TAG, "Timeout, unlocked after used. Turning Off");
                    controller_lock();
                }
            }
            break;
        
        case CONTROLLER_MODE_IN_USE:
            if (!error_hold && !state->is_on) {
                ESP_LOGE(TAG, "In use power failed.");
            }

            if (state->power < controller_used_threshold) {
                controller_used_after_time = now;
                controller_mode_set(CONTROLLER_MODE_UNLOCKED);
            }

            break;

        default:
            break;
    }

    ESP_LOGD(TAG, "energy: %ld, power: %ld, current_max: %ld, zx: %ld", state->energy, state->power, state->current_max, state->zx);
    mqtt_sn_send_with_mac(MQTT_SN_MESSAGE_POWER, state, sizeof(monitor_state_t));
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

    mqtt_sn_init();

    ret = http_api_settings(&controller_used_threshold, &controller_unlocked_timeout);

    if (ret != ESP_OK) {
        controller_used_threshold = 20;
        controller_unlocked_timeout = 120;
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
    esp_event_handler_register_with(app_events, MONITOR_EVENTS, MONITOR_EVENT_STATE, on_monitor_state, NULL); 

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

    xTaskCreate(status_task, "status_task", 2048, NULL, tskIDLE_PRIORITY, &status_task_handle); 

    controller_lock();

    if (monitor_init(app_events) == ESP_OK) {
        monitor_start();
        vTaskDelay(2000/portTICK_PERIOD_MS);
        monitor_enabled = monitor_calibrate();

        if (!monitor_enabled) {
            ESP_LOGI(TAG, "No monitor found.");
            monitor_stop();
        }
    }

    int64_t now = esp_timer_get_time();
    mqtt_sn_send_with_mac(MQTT_SN_MESSAGE_HELLO, &now, sizeof(now));

    while (1)
    {
        ret = esp_event_loop_run(app_events, portMAX_DELAY);
        if (ret != ESP_OK) {
            break;
        }
    }
}
