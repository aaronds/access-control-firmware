
#include "wifi_manage.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"

#include "lwip/err.h"
#include "lwip/sys.h"

static const char *TAG = "wifi station";

static int reconnect_timeout = CONFIG_MIN_RECONNECT_INTERVAL;
static StaticTimer_t reconnect_timer_buffer;
static TimerHandle_t reconnect_timer;

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        //TODO: Timer restart
        ESP_LOGI(TAG, "AP disconnected");
        if (xTimerIsTimerActive(reconnect_timer)) { 
            ESP_LOGI(TAG, "Reconnect in progress");
        } else {
            xTimerChangePeriod(reconnect_timer, (reconnect_timeout)/portTICK_PERIOD_MS, 100);
            xTimerStart(reconnect_timer, 100);

            reconnect_timeout *= 2;
            if (reconnect_timeout > CONFIG_MAX_RECONNECT_INTERVAL) {
                reconnect_timeout = CONFIG_MAX_RECONNECT_INTERVAL;
            }
            ESP_LOGI(TAG, "Reconnect timer started");
        }

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        reconnect_timeout = CONFIG_MIN_RECONNECT_INTERVAL;
    }
}

static void reconnect_callback(TimerHandle_t xExpiredTimer)
{
    ESP_LOGI(TAG, "retry to connect to the AP");
    esp_wifi_connect();
}

void wifi_init_sta(void)
{
    reconnect_timer = xTimerCreateStatic("Reconnect",
                       (reconnect_timeout*1000)/portTICK_PERIOD_MS,
                       pdFALSE,
                       NULL,
                       reconnect_callback,
                       &reconnect_timer_buffer);

    ESP_ERROR_CHECK(esp_netif_init());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");
}
