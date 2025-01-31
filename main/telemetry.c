#include "telemetry.h"
#include "statsd.h"
#include "driver/temperature_sensor.h"
#include "esp_timer.h"
#include "esp_wifi.h"


static void telemetry_task();

static void telemetry_metric_heap_free();
//static void telemetry_metric_temperature();
static void telemetry_metric_rssi();
static void telemetry_metric_task_tel();
static void telemetry_metric_task_main();
static void telemetry_metric_task_pn532();

static const char* TAG = "telemetry";

telemetry_config_t telemetry_config = {
    .metric = 0,
    .main_handle = NULL,
    .pn532_handle = NULL,
};

/*
temperature_sensor_handle_t temp_handle = NULL;
temperature_sensor_config_t temp_sensor_config = {
    .range_min = -10,
    .range_max = 60,
};
*/

esp_err_t telemetry_start() {
    if (xTaskCreate(telemetry_task,
        "telemetry_task",
        2048,
        &telemetry_config,
        tskIDLE_PRIORITY,
        &telemetry_config.tel_handle
    ) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to create task");
        return ESP_FAIL;
    }

    /*
    ESP_ERROR_CHECK(temperature_sensor_install(&temp_sensor_config, &temp_handle));
    ESP_ERROR_CHECK(temperature_sensor_enable(temp_handle));
    */
    
    return ESP_OK;
}

void telemetry_task() {
    while (true) {
        switch (telemetry_config.metric) {
            case 0:
                telemetry_metric_heap_free();
                break;

            case 1:
                telemetry_metric_rssi();
                break;
            
            case 2:
                telemetry_metric_task_tel();
                break;

            case 3:
                telemetry_metric_task_main();
                break;
            
            case 4:
                telemetry_metric_task_pn532();
                break;

            default:
                telemetry_config.metric = -1;
                break;
        }

        telemetry_config.metric++;
        statsd_inc("tel_ping");

        switch(*telemetry_config.controller_mode) {
            case 2:
            case 3:
                statsd_value("relay_state", 1);
                break;
            default:
                statsd_value("relay_state", 0);
                break;
        }

        vTaskDelay((TELEMETRY_DELAY + (rand() % TELEMETRY_RAND)) / portTICK_PERIOD_MS);
    }
}
                
void telemetry_metric_heap_free() {
    size_t heap_size = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    statsd_value("tel_metric_heapFree", heap_size);
}

/*
void telemetry_metric_temperature() {
    float tsens_out = 0;
    int temp;
    ESP_ERROR_CHECK(temperature_sensor_get_celsius(temp_handle, &tsens_out));
    temp = (int) tsens_out * 100;

    statsd_value("tel_metric_temp", temp);
}
*/

void telemetry_metric_rssi() {
    uint16_t aid;

    ESP_ERROR_CHECK(esp_wifi_sta_get_aid(&aid));

    if (aid < 1) {
        return;
    }

    int rssi;

    ESP_ERROR_CHECK(esp_wifi_sta_get_rssi(&rssi));

    statsd_value("tel_metric_rssi",rssi);
}

void telemetry_metric_task_tel() {
    int depth = (int) uxTaskGetStackHighWaterMark( NULL );
    statsd_value("tel_metric_task_tel", depth);
}

void telemetry_metric_task_main() {
    if (!telemetry_config.main_handle) {
        return;
    }

    int depth = (int) uxTaskGetStackHighWaterMark(telemetry_config.main_handle);
    statsd_value("tel_metric_task_main", depth);
}

void telemetry_metric_task_pn532() {
    if (!telemetry_config.pn532_handle) {
        return;
    }

    int depth = (int) uxTaskGetStackHighWaterMark(telemetry_config.pn532_handle);
    statsd_value("tel_metric_task_main", depth);
}
