#include "statsd.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "esp_timer.h"

#define MACHINE_MAC_LEN 6

static const char* TAG = "statsd";

statsd_config_t statsd_config;

esp_err_t statsd_init() {
    statsd_config.sock = 0;

    static char mac_str[(MACHINE_MAC_LEN*2)+1] = {0};
    uint8_t mac[MACHINE_MAC_LEN];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char* mac_str_end = mac_str;
    for (int i=0; i<MACHINE_MAC_LEN; i++)
    {
        sprintf(mac_str_end, "%02hhx", mac[i]);
        mac_str_end += 2;
    }

    snprintf(statsd_config.metric_template, STATSD_BUFFER_LEN, "acs.%%s,m=%s,c=%%d:%%d|%%s", mac_str);

    ESP_LOGI(TAG, "statsd metric template: %s", statsd_config.metric_template);
    
    memset(&statsd_config.server, 0, sizeof(statsd_config.server));
    statsd_config.server.sin_family = AF_INET;
    statsd_config.server.sin_port = htons(CONFIG_STATSD_PORT);

    if(!inet_pton(AF_INET, CONFIG_STATSD_IP, &statsd_config.server.sin_addr)) {
        ESP_LOGE(TAG, "statsd address error");
        return ESP_FAIL;
    }

    if ((statsd_config.sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        ESP_LOGE(TAG, "statsd socket error");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t statsd_send_message(const char *msg) {

    if (statsd_config.sock < 1) {
        ESP_LOGE(TAG, "statsd no socket");
        return ESP_FAIL;
    }

    if (sendto(statsd_config.sock, msg, strlen(msg), 0, (struct sockaddr *) &statsd_config.server, sizeof(statsd_config.server)) == -1) {
        
        ESP_LOGE(TAG, "statsd send failed: '%s'", msg);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t statsd_count(char *stat, size_t val) {
    return statsd_count_channel(stat, val, 0);
}

esp_err_t statsd_count_channel(char *stat, size_t val, uint8_t channel) { 
    if (snprintf(statsd_config.metric_buffer, STATSD_BUFFER_LEN, statsd_config.metric_template, stat, channel, val, "c") < 0) {
        ESP_LOGE(TAG, "statsd count metric string error");
        return ESP_FAIL;
    }

    return statsd_send_message(statsd_config.metric_buffer);
}

esp_err_t statsd_inc(char *stat) {
    return statsd_inc_channel(stat, 0);
}

esp_err_t statsd_inc_channel(char *stat, uint8_t channel) {
    return statsd_count_channel(stat, 1, channel);
}

esp_err_t statsd_value(char *stat, int val) {
    return statsd_value_channel(stat, val, 0);
}

esp_err_t statsd_value_channel(char *stat, int val, uint8_t channel) {
    if (snprintf(statsd_config.metric_buffer, STATSD_BUFFER_LEN, statsd_config.metric_template, stat, channel, val, "g") < 0) {
        ESP_LOGE(TAG, "statsd value metric string error");
        return ESP_FAIL;
    }

    return statsd_send_message(statsd_config.metric_buffer);
}

esp_err_t statsd_time(char *stat, int val) {
    return statsd_time_channel(stat, val, 0);
}

esp_err_t statsd_time_channel(char *stat, int val, uint8_t channel) {
    if (snprintf(statsd_config.metric_buffer, STATSD_BUFFER_LEN, statsd_config.metric_template, stat, channel, val, "g") < 0) {
        ESP_LOGE(TAG, "statsd time metric string error");
        return ESP_FAIL;
    }

    return statsd_send_message(statsd_config.metric_buffer);
}

uint64_t statsd_timer_start() {
    return esp_timer_get_time();
}

esp_err_t statsd_timer(char *stat, int64_t start) {
    int64_t end = esp_timer_get_time();
    int time = 0;

    if (end < start) {
        return ESP_FAIL;
    }

    time = (end - start) / 1000;

    if (time < 0) {
        return ESP_FAIL;
    }

    return statsd_time(stat, time);
}
