#pragma once

#include "esp_log.h"
#include "esp_err.h"
#include "esp_mac.h"
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdarg.h>

#define SYSLOG_BUFFER_LEN 1024
#define SYSLOG_PRIORITY 14
#define SYSLOG_VERSION 1
#define SYSLOG_APP_NAME "acs"
#define SYSLOG_PROC_ID 1

typedef struct {
    struct sockaddr_in server;
    int socket;
    char syslog_message[SYSLOG_BUFFER_LEN];
    char *msg_start;
    int header_length;
} syslog_config_t;

esp_err_t syslog_udp_init();
int syslog_vprintf(const char* format, va_list arg);

