#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdint.h>
#include <esp_event.h>
#include "esp_log.h"

#include <string.h>
#include <soc/sens_reg.h>
#include <soc/sens_struct.h>

#define MONITOR_ADC_CHANNEL 6
#define MONITOR_MAINS_V 230
#define MONITOR_MAINS_FREQ 50
#define MONITOR_MAINS_ZX_FREQ MONITOR_MAINS_FREQ * 2
#define MONITOR_MAINS_ZX_SAMPLES MONITOR_MAINS_ZX_FREQ
#define MONITOR_TIMER_DIVIDER 80 * 500
#define MONITOR_TIMER_ALARM 20

#define MONITOR_ZERO_AMPS 1641
#define MONITOR_CURRENT_MV_PER_A 48

// Inspired by https://www.toptal.com/embedded/esp32-audio-sampling


esp_err_t monitor_init();
esp_err_t monitor_start();
esp_err_t monitor_stop();
void monitor_handle_buffer();
