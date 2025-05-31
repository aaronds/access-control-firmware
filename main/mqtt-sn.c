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

    struct timeval timeout;
    timeout.tv_sec = 30;
    timeout.tv_usec = 0;
    setsockopt(mqtt_sn_config.socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);

    mqtt_sn_config.state = MQTT_SN_STATE_OFF;

    xTaskCreate(mqtt_sn_task, "MQTT-SN", 2048, NULL, 1, &mqtt_sn_config.task);

    mqtt_sn_connect();
    return ESP_OK;
}

bool mqtt_sn_is_ready() {
    return (mqtt_sn_config.socket && (mqtt_sn_config.state == MQTT_SN_STATE_READY || mqtt_sn_config.state == MQTT_SN_STATE_PING));
}

void mqtt_sn_connect() {
    uint8_t *message = mqtt_sn_config.message_buffer;

    message[0] = 6 + (MACHINE_MAC_LEN * 2) + 1;
    message[1] = MQTT_SN_MESSAGE_TYPE_CONNECT;
    message[2] = 0;
    message[3] = 0x01;
    message[4] = 0xfe;
    message[5] = 0xff;

    char* mac_str_end = (char *) message + 6;

    for (int i=0; i<MACHINE_MAC_LEN; i++)
    {
        sprintf(mac_str_end, "%02hhx", mqtt_sn_config.mac[i]);
        mac_str_end += 2;
    }


    sendto(mqtt_sn_config.socket, message, 5 + (MACHINE_MAC_LEN * 2) + 1, 0, (struct sockaddr *) &mqtt_sn_config.server, sizeof(mqtt_sn_config.server)); 

    mqtt_sn_config.state = MQTT_SN_STATE_CONNECTING;
}

void mqtt_sn_ping() { 
    uint8_t *message = mqtt_sn_config.task_buffer;

    message[0] = 2;
    message[1] = MQTT_SN_MESSAGE_TYPE_PINGREQ;

    sendto(mqtt_sn_config.socket, message, 2, 0, (struct sockaddr *) &mqtt_sn_config.server, sizeof(mqtt_sn_config.server));
}

void mqtt_sn_ping_resp() { 
    uint8_t *message = mqtt_sn_config.task_buffer;

    message[0] = 2;
    message[1] = MQTT_SN_MESSAGE_TYPE_PINGRESP;

    sendto(mqtt_sn_config.socket, message, 2, 0, (struct sockaddr *) &mqtt_sn_config.server, sizeof(mqtt_sn_config.server));
}

void mqtt_sn_header_predefined(uint8_t *message, uint16_t topic, uint16_t length) {
    ESP_LOGD(TAG, "total length: %d data length: %d", MQTT_SN_PUBLISH_HEADER_LENGTH + length, length);
    message[0] = MQTT_SN_PUBLISH_HEADER_LENGTH + length;
    message[1] = MQTT_SN_MESSAGE_TYPE_PUBLISH;
    message[2] = (MQTT_SN_QOS_ONCE << MQTT_SN_PUBLISH_QOS) | ( MQTT_SN_TOPIC_TYPE_PREDEFINED << MQTT_SN_PUBLISH_TOPIC_TYPE);
    message[3] = (topic >> 8) & 0xff;
    message[4] = (topic & 0xff);
    message[5] = 0;
    message[6] = 0;
}

void mqtt_sn_send(uint16_t topic, void *buff, size_t length) {

    if (!mqtt_sn_is_ready()) {
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

    if (!mqtt_sn_is_ready()) {
        return;
    }

    if (length + MQTT_SN_PUBLISH_HEADER_LENGTH + MACHINE_MAC_LEN> MQTT_SN_MSG_LEN) {
        return;
    }

    uint8_t *message = mqtt_sn_config.message_buffer;

    mqtt_sn_header_predefined(message, topic, length + MACHINE_MAC_LEN);

    memcpy(message + MQTT_SN_PUBLISH_HEADER_LENGTH, mqtt_sn_config.mac, MACHINE_MAC_LEN);
    memcpy(message + MQTT_SN_PUBLISH_HEADER_LENGTH + MACHINE_MAC_LEN, buff, length);

    sendto(mqtt_sn_config.socket, message, length + MQTT_SN_PUBLISH_HEADER_LENGTH + MACHINE_MAC_LEN, 0, (struct sockaddr *) &mqtt_sn_config.server, sizeof(mqtt_sn_config.server));
}

void mqtt_sn_task() {
    int length = 0;

    while(true) {
        if (!mqtt_sn_config.socket) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        length = recv(mqtt_sn_config.socket, mqtt_sn_config.task_buffer, MQTT_SN_MSG_LEN, 0);

        ESP_LOGI(TAG, "recv %d", length);

        if (length < 0) {
            switch (mqtt_sn_config.state) {
                case MQTT_SN_STATE_CONNECTING:
                case MQTT_SN_STATE_PING:
                    /* Timeout waiting for response */
                    mqtt_sn_connect();
                    mqtt_sn_config.state = MQTT_SN_STATE_CONNECTING;
                    break;
                case MQTT_SN_STATE_READY:
                    mqtt_sn_ping();
                    mqtt_sn_config.state = MQTT_SN_STATE_PING;
                    break;

                default:
                    break;
            }
            continue;
        }

        if (length < 2) {
            continue;
        }

        uint8_t mqtt_sn_message_type = mqtt_sn_config.task_buffer[1];

        switch(mqtt_sn_message_type) {
            case MQTT_SN_MESSAGE_TYPE_CONNACK:
            case MQTT_SN_MESSAGE_TYPE_PINGRESP:
                mqtt_sn_config.state = MQTT_SN_STATE_READY;
                break;
            
            case MQTT_SN_MESSAGE_TYPE_PINGREQ:
                mqtt_sn_ping_resp();
                break;
                
            case MQTT_SN_MESSAGE_TYPE_DISCONNECT:
                mqtt_sn_config.state = MQTT_SN_STATE_OFF;
                vTaskDelay(pdMS_TO_TICKS(2000));
                mqtt_sn_connect();
                mqtt_sn_config.state = MQTT_SN_STATE_CONNECTING;
                break;

            default:
                break;
        }
    }
}
