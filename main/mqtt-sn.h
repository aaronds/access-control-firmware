#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define MQTT_MSG_LEN 255
#define MQTT_MSG_COUNT 5

typedef struct {
    struct sockaddr_in server;
    int socket;
    uint8_t message_buffer[MQTT_MSG_COUNT][MQTT_MSG_LEN];
    uint8_t read;
    uint8_t write;
    uint8_t size;
    uint64_t last_ping;
    TaskHandle_t task;
} mqtt_sn_config_t;

esp_err_t mqtt_sn_init();
void mqtt_sn_send(uint16_t topic, uint8_t *buff,size_t length);
void mqtt_sn_task();

