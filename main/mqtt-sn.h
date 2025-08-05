#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "esp_mac.h"
#include "esp_log.h"

#include "mqtt-sn-constants.h"

#define MQTT_SN_MSG_LEN 250
#define MQTT_SN_MSG_COUNT 5

#define MACHINE_MAC_LEN 6

typedef enum {
    MQTT_SN_STATE_OFF,
    MQTT_SN_STATE_CONNECTING,
    MQTT_SN_STATE_READY,
    MQTT_SN_STATE_PING
} mqtt_sn_state_t;

typedef struct {
    struct sockaddr_in server;
    int socket;
    uint8_t message_buffer[MQTT_SN_MSG_LEN];
    uint8_t task_buffer[MQTT_SN_MSG_LEN];
    uint8_t mac[MACHINE_MAC_LEN];
    mqtt_sn_state_t state;
    TaskHandle_t task;
} mqtt_sn_config_t;

typedef struct mqtt_acs_error_t {
    uint16_t tag;
    uint16_t error;
} mqtt_acs_error_t;

esp_err_t mqtt_sn_init();

void mqtt_sn_connect();
bool mqtt_sn_is_ready();
void mqtt_sn_header_predefined(uint8_t *message, uint16_t topic,uint16_t length);
void mqtt_sn_send(uint16_t topic, void *buff,size_t length);
void mqtt_sn_send_with_mac(uint16_t topic, void *buff, size_t length);
void mqtt_sn_task();

/* Valid error codes defined in main/errors.json by root CMakeLists.txt */
void mqtt_sn_send_error(uint16_t tag, uint16_t error);
//void mqtt_sn_send_error_no_PII(uint16_t tag, uint16_t error, void *arg, uint16_t length);

