#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdint.h>
#include <esp_event.h>
#include "esp_log.h"

#include <string.h>
#include <soc/sens_reg.h>
#include <soc/sens_struct.h>

ESP_EVENT_DECLARE_BASE(MONITOR_EVENTS);

#define MONITOR_ADC_CHANNEL 6
#define MONITOR_MAINS_V 230
#define MONITOR_MAINS_FREQ 50
#define MONITOR_MAINS_ZX_FREQ MONITOR_MAINS_FREQ * 2
#define MONITOR_MAINS_ZX_SAMPLES MONITOR_MAINS_ZX_FREQ
#define MONITOR_TIMER_DIVIDER 80 * 500
#define MONITOR_TIMER_ALARM 20

#define MONITOR_ZERO_AMPS 1630
#define MONITOR_CURRENT_MV_PER_A 48

esp_err_t monitor_init();
esp_err_t monitor_start();
esp_err_t monitor_stop();
void monitor_handle_buffer();

typedef struct {
    uint32_t energy;
    uint32_t power;
    uint32_t time;
    uint32_t current_max;
    uint32_t zx;
    bool is_on;
} monitor_state_t;

typedef enum {
    MONITOR_EVENT_ANY = ESP_EVENT_ANY_ID,
    MONITOR_EVENT_NONE,
    MONITOR_EVENT_STATE
} monitor_event_t;

extern esp_event_loop_handle_t monitor_event_handle;
