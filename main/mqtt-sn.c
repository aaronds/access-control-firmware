#include "mqtt-sn.h"

#define MACHINE_MAC_LEN 6

mqtt_sn_config_t mqtt_sn_config;

static const char* TAG = "mqtt-sn";

esp_err_t mqtt_sn_init() {
    esp_read_mac(mqtt_sn_config.mac, ESP_MAC_WIFI_STA);

    memset(&mqtt_sn_config.server, 0, sizeof(mqtt_sn_config.server));
    mqtt_sn_config.server.sin_family = AF_INET;
    mqtt_sn_config.server.sin_port = htons(CONFIG_MQTT_SN_PORT);

    if (!inet_pton(AF_INET, CONFIG_MQTT_SN_IP, &mqtt_sn_config.server.sin_addr)) {
        ESP_LOGE(TAG, "address error");
        return ESP_FAIL;
    }

    if ((mqtt_sn_config.socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        ESP_LOGE(TAG, "socket error");
        return ESP_FAIL;
    }

    return ESP_OK;
}

void mqtt_sn_header_predefined(uint8_t *message, uint16_t topic, uint16_t length) {
    message[0] = MQTT_SN_PUBLISH_HEADER_LENGTH + length;
    message[1] = MQTT_SN_MESSAGE_TYPE_PUBLISH;
    message[2] = (MQTT_SN_QOS_NO_CONNECT << MQTT_SN_PUBLISH_QOS) | ( MQTT_SN_TOPIC_TYPE_PREDEFINED << MQTT_SN_PUBLISH_TOPIC_TYPE);
    message[3] = (topic >> 8) && 0xff;
    message[4] = (topic && 0xff);
    message[5] = 0;
    message[6] = 0;
}

void mqtt_sn_send(uint16_t topic, void *buff, size_t length) {

    if (!mqtt_sn_config.socket) {
        return;
    }

    if (length + MQTT_SN_PUBLISH_HEADER_LENGTH > MQTT_SN_MSG_LEN) {
        return;
    }

    uint8_t *message = mqtt_sn_config.message_buffer;

    mqtt_sn_header_predefined(message, topic, length);

    memcpy(message + MQTT_SN_PUBLISH_HEADER_LENGTH, buff, length);

    sendto(mqtt_sn_config.socket, message, length + MQTT_SN_PUBLISH_HEADER_LENGTH, 0, (struct sockaddr *) &mqtt_sn_config.server, sizeof(mqtt_sn_config.server));
}

void mqtt_sn_send_with_mac(uint16_t topic, void *buff, size_t length) {

    if (!mqtt_sn_config.socket) {
        return;
    }

    if (length + MQTT_SN_PUBLISH_HEADER_LENGTH > MQTT_SN_MSG_LEN) {
        return;
    }

    uint8_t *message = mqtt_sn_config.message_buffer;

    mqtt_sn_header_predefined(message, topic, length);

    memcpy(message + MQTT_SN_PUBLISH_HEADER_LENGTH, mqtt_sn_config.mac, MACHINE_MAC_LEN);
    memcpy(message + MQTT_SN_PUBLISH_HEADER_LENGTH + MACHINE_MAC_LEN, buff, length);

    sendto(mqtt_sn_config.socket, message, length + MQTT_SN_PUBLISH_HEADER_LENGTH + MACHINE_MAC_LEN, 0, (struct sockaddr *) &mqtt_sn_config.server, sizeof(mqtt_sn_config.server));
}
