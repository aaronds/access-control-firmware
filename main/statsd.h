#pragma once

#include "esp_err.h"
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define STATSD_BUFFER_LEN 256

typedef struct {
    struct sockaddr_in server;
    int sock;
    char metric_template[STATSD_BUFFER_LEN];
    char metric_buffer[STATSD_BUFFER_LEN];
} statsd_config_t;

typedef int64_t statsd_timer_t;

esp_err_t statsd_init(void);
esp_err_t statsd_send_message(const char *msg);

esp_err_t statsd_count(char *stat, size_t val);
esp_err_t statsd_count_channel(char *stat, size_t val, uint8_t channel);
esp_err_t statsd_inc(char *stat);
esp_err_t statsd_inc_channel(char *stat, uint8_t channel);

esp_err_t statsd_value(char *stat, int val);
esp_err_t statsd_value_channel(char *stat, int val, uint8_t channel);

esp_err_t statsd_time(char *stat, int val);
esp_err_t statsd_time_channel(char *stat, int val, uint8_t channel);

uint64_t statsd_timer_start();
esp_err_t statsd_timer(char *stat, int64_t from);

