#include "syslog.h"

#define MACHINE_MAC_LEN 6

static const char* TAG = "syslog";

syslog_config_t syslog_config;

esp_err_t syslog_udp_init() {

    uint8_t mac[MACHINE_MAC_LEN];
    char mac_str[(MACHINE_MAC_LEN*2)+1] = {0};

    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char* mac_str_end = mac_str;
    for (int i=0; i<MACHINE_MAC_LEN; i++)
    {
        sprintf(mac_str_end, "%02hhx", mac[i]);
        mac_str_end += 2;
    }

    syslog_config.header_length = snprintf(syslog_config.syslog_message, SYSLOG_BUFFER_LEN, "<%d>%d - %s %s %d - ", SYSLOG_PRIORITY, SYSLOG_VERSION, mac_str, SYSLOG_APP_NAME, SYSLOG_PROC_ID);  
    syslog_config.msg_start = syslog_config.syslog_message + syslog_config.header_length;

    ESP_LOGI(TAG, "syslog header: %s", syslog_config.syslog_message);

    memset(&syslog_config.server, 0, sizeof(syslog_config.server));
    syslog_config.server.sin_family = AF_INET;
    syslog_config.server.sin_port = htons(CONFIG_SYSLOG_PORT);

    if(!inet_pton(AF_INET, CONFIG_SYSLOG_IP, &syslog_config.server.sin_addr)) {
        ESP_LOGE(TAG, "syslog address error");
        return ESP_FAIL;
    }

    if ((syslog_config.socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        ESP_LOGE(TAG, "syslog socket error");
        return ESP_FAIL;
    }

    struct sockaddr_storage source_addr;
    socklen_t socklen = sizeof(source_addr);

    recvfrom(syslog_config.socket, syslog_config.syslog_message, SYSLOG_BUFFER_LEN - 1, 0, (struct sockaddr *)&source_addr, &socklen);

    return ESP_OK;
}

int syslog_vprintf(const char* format, va_list arg) {
    int rv = 0;


    rv = vsnprintf(syslog_config.msg_start, SYSLOG_BUFFER_LEN - syslog_config.header_length - 1, format, arg);
    
    if (!syslog_config.socket) {
        return rv;
    }

    sendto(syslog_config.socket, syslog_config.syslog_message, strlen(syslog_config.syslog_message), 0, (struct sockaddr *) &syslog_config.server, sizeof(syslog_config.server));

    return rv;
}
