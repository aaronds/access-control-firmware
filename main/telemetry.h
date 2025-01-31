#pragma once

#include "esp_log.h"
#include <inttypes.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TELEMETRY_DELAY 8000
#define TELEMETRY_RAND 2000

typedef struct {
    int metric;
    TaskHandle_t tel_handle;
    TaskHandle_t main_handle;
    TaskHandle_t pn532_handle;
    uint16_t *controller_mode;  
} telemetry_config_t;

extern telemetry_config_t telemetry_config;

esp_err_t telemetry_start();


