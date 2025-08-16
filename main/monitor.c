#include "monitor.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char* TAG = "monitor";

ESP_EVENT_DEFINE_BASE(MONITOR_EVENTS);

monitor_state_t monitor_event;
esp_event_loop_handle_t monitor_event_handle;

unsigned int monitor_voltage[] = {0,5,10,15,20,25,31,36,41,46,51,56,61,66,71,76,81,86,91,96,100,105,110,115,120,124,129,134,138,143,148,152,157,161,165,170,174,178,183,187,191,195,199,203,207,211,215,219,222,226,230,233,237,240,244,247,250,254,257,260,263,266,269,272,274,277,280,282,285,287,290,292,294,296,298,300,302,304,306,307,309,311,312,313,315,316,317,318,319,320,321,322,322,323,324,324,324,325,325,325,325,325,325,325,324,324,324,323,322,322,321,320,319,318,317,316,315,313,312,311,309,307,306,304,302,300,298,296,294,292,290,287,285,282,280,277,274,272,269,266,263,260,257,254,250,247,244,240,237,233,230,226,222,219,215,211,207,203,199,195,191,187,183,178,174,170,165,161,157,152,148,143,138,134,129,124,120,115,110,105,100,96,91,86,81,76,71,66,61,56,51,46,41,36,31,25,20,15,10,5,0,5,10,15,20,25,31,36,41,46,51,56,61,66,71,76,81,86,91,96,100,105,110,115,120,124,129,134,138,143,148,152,157,161,165,170,174,178,183,187,191,195,199,203,207,211,215,219,222,226,230,233,237,240,244,247,250,254,257,260,263,266,269,272,274,277,280,282,285,287,290,292,294,296,298,300,302,304,306,307,309,311,312,313,315,316,317,318,319,320,321,322,322,323,324,324,324,325,325,325,325,325,325,325,324,324,324,323,322,322,321,320,319,318,317,316,315,313,312,311,309,307,306,304,302,300,298,296,294,292,290,287,285,282,280,277,274,272,269,266,263,260,257,254,250,247,244,240,237,233,230,226,222,219,215,211,207,203,199,195,191,187,183,178,174,170,165,161,157,152,148,143,138,134,129,124,120,115,110,105,100,96,91,86,81,76,71,66,61,56,51,46,41,36,31,25,20,15,10,5,0};
unsigned int monitor_voltage_time = 0;
unsigned int monitor_voltage_time_adc_conv = 0;
unsigned int monitor_zx_count = 0;
unsigned int monitor_zx_count_last = 0;
uint32_t monitor_zx_count_total = 0;
unsigned int monitor_buffer_idx = 0;
bool monitor_started = false;

uint32_t monitor_zero_amps = MONITOR_ZERO_AMPS;
uint32_t monitor_adc_average = 0;

#define MONITOR_CONV_FRAME_SIZE 400
#define MONITOR_CONV_FRAME_BYTES sizeof(adc_digi_output_data_t) * MONITOR_CONV_FRAME_SIZE

#define ZX_GPIO_PIN (12) 

uint8_t monitor_result[MONITOR_CONV_FRAME_BYTES] = {0};

portMUX_TYPE DRAM_ATTR monitor_timer_mux = portMUX_INITIALIZER_UNLOCKED; 
TaskHandle_t monitor_task; 
gptimer_handle_t monitor_timer = NULL;
adc_continuous_handle_t monitor_adc = NULL;
adc_cali_handle_t monitor_adc_cali = NULL;

int64_t adc_last_conv_start = 0;
int64_t adc_last_conv_time = 0;
uint32_t adc_conv_total_time = 0;

int64_t monitor_zx_last = 0;

/*Width: 700-800*/

static bool IRAM_ATTR monitor_timer_alarm(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_data) {
    BaseType_t high_task_awoken = pdFALSE;

    monitor_voltage_time = (monitor_voltage_time + 1) % sizeof(monitor_voltage);

    return high_task_awoken == pdTRUE;
}

static bool IRAM_ATTR monitor_adc_conv_done(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data) {
    BaseType_t high_task_awoken = pdFALSE;

    monitor_zx_count_last = monitor_zx_count;
    monitor_zx_count = 0;

    int64_t now = esp_timer_get_time();

    if (adc_last_conv_start > 0) {
        adc_last_conv_time = now - adc_last_conv_start;
    }

    adc_last_conv_start = now;
    monitor_voltage_time_adc_conv = monitor_voltage_time;

    vTaskNotifyGiveFromISR(monitor_task, &high_task_awoken);

    return high_task_awoken == pdTRUE;
}

static void IRAM_ATTR monitor_zx_isr_handler(void* arg) { 
    int64_t now = esp_timer_get_time();

    if (monitor_zx_last > 0 && now - monitor_zx_last > 1000) {
        if (monitor_voltage_time < 100 || monitor_voltage_time > 300) {
            monitor_voltage_time = 0;
        } else {
            monitor_voltage_time = 200;
        }

        monitor_zx_count++;
    }

    monitor_zx_last = now;
}

esp_err_t monitor_init(esp_event_loop_handle_t event_handle) {

    monitor_event_handle = event_handle;
    
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 20000 * 100
    };

    gptimer_alarm_config_t alarm_config = {
        .reload_count = 0,
        .alarm_count = 100, 
        .flags.auto_reload_on_alarm = true,
    };

    gptimer_event_callbacks_t cbs = {
        .on_alarm = monitor_timer_alarm,
    };
    
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &monitor_timer)); 
    ESP_ERROR_CHECK(gptimer_set_alarm_action(monitor_timer, &alarm_config));
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(monitor_timer, &cbs, NULL));
    ESP_ERROR_CHECK(gptimer_enable(monitor_timer));

    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = MONITOR_CONV_FRAME_BYTES * 2,
        .conv_frame_size = MONITOR_CONV_FRAME_BYTES
    };

    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &monitor_adc));

    /* Sample frequency found by experimentation to give 400 samples in 50hz*/

    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = 24500,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1,
        .pattern_num = 1
    };

    adc_digi_pattern_config_t adc_pattern[1] = {0};

    adc_pattern[0].atten = ADC_ATTEN_DB_6;
    adc_pattern[0].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
    adc_pattern[0].unit = ADC_UNIT_1;
    adc_pattern[0].channel = 6 & 0x7;

    dig_cfg.adc_pattern = adc_pattern;

    ESP_ERROR_CHECK(adc_continuous_config(monitor_adc, &dig_cfg));

    adc_continuous_evt_cbs_t monitor_adc_cbs = {
        .on_conv_done = monitor_adc_conv_done,
    };

    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(monitor_adc, &monitor_adc_cbs, NULL));

    adc_cali_line_fitting_config_t adc_cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_6,
        .bitwidth = SOC_ADC_DIGI_MAX_BITWIDTH,
    };

    ESP_ERROR_CHECK(adc_cali_create_scheme_line_fitting(&adc_cali_config, &monitor_adc_cali));

    xTaskCreate(monitor_handle_buffer, "Monitor", 2048, NULL, 1, &monitor_task);


    gpio_config_t zx_conf = {0};
    zx_conf.intr_type = GPIO_INTR_NEGEDGE;
    zx_conf.pin_bit_mask = (1ULL << ZX_GPIO_PIN);
    zx_conf.mode = GPIO_MODE_INPUT;
    zx_conf.pull_down_en = 0;
    zx_conf.pull_up_en = 1;
    gpio_config(&zx_conf);
    gpio_isr_handler_add(ZX_GPIO_PIN, monitor_zx_isr_handler, NULL);

    return ESP_OK;
}

/*

bool monitor_detect() {
    int raw = 0;

    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config2 = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    if (adc_oneshot_new_unit(&init_config2, &adc1_handle) != ESP_OK) {
        return false;
    }

    adc_oneshot_chan_cfg_t adc1_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT, // Default is 12 bits (max)
        .atten = ADC_ATTEN_DB_6
    };

    adc_oneshot_config_channel(adc1_handle, 6 & 0x7, &adc1_cfg);

    adc_oneshot_read(adc1_handle, 6 & 0x7, &raw);
    adc_oneshot_del_unit(adc1_handle);

    ESP_LOGI(TAG,"Monitor Detect Raw: %d", raw);

    return raw > 900;
}
*/

bool monitor_calibrate() {
    uint32_t diff_to_spec = monitor_adc_average > MONITOR_ZERO_AMPS ? monitor_adc_average - MONITOR_ZERO_AMPS : MONITOR_ZERO_AMPS - monitor_adc_average;

    ESP_LOGI(TAG, "adc_avg: %ld, diff_to_spec: %ld", monitor_adc_average, diff_to_spec);

    if (diff_to_spec > 500) {
        /* Probably not present .:. fail. */
        return false;
    } else {
       monitor_zero_amps = monitor_adc_average;
       ESP_LOGI(TAG, "montior calibrate zero: %d", (int) monitor_zero_amps);
       return true;
    }
}

esp_err_t monitor_start() {
    gptimer_start(monitor_timer);
    ESP_ERROR_CHECK(adc_continuous_start(monitor_adc));

    monitor_started = true;

    ESP_LOGI(TAG, "Monitor start OK");
    return ESP_OK;
}

esp_err_t monitor_stop() {
    gptimer_stop(monitor_timer);
    ESP_ERROR_CHECK(adc_continuous_stop(monitor_adc));

    monitor_started = false;
    return ESP_OK;
}



void monitor_handle_buffer(){
    int voltage;
    uint32_t current;
    uint32_t current_max = 0;
    uint32_t power;
    uint32_t energy = 0;
    uint32_t energy_total = 0;
    uint16_t count = 0;
    uint32_t average_power = 0;
    uint32_t milli_watt_seconds = 0;
    uint32_t adc_results = 0;
    uint32_t adc_result_count = 0;
    unsigned int monitor_voltage_time_last = monitor_voltage_time;
    unsigned int monitor_voltage_result_time = 0;
    uint32_t adc_voltage_total = 0;
    bool current_is_on = false;
    int adc_result_value = 0;
    memset(monitor_result, 0xcc, MONITOR_CONV_FRAME_BYTES);


    while(true) {
        ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(2000)); 
        
        monitor_voltage_time_last = monitor_voltage_time_adc_conv;

        if (!monitor_started) {
            continue;
        }

        energy = 0;
        current_max = 0;

        int adc_status = adc_continuous_read(monitor_adc, monitor_result, MONITOR_CONV_FRAME_BYTES, &adc_results, 0);

        if (adc_status != ESP_OK) {
            ESP_LOGE(TAG, "ADC Status Not OK: %d", adc_status);
            continue;
        }

        adc_result_count = adc_results / (sizeof(adc_digi_output_data_t));

        /* Get the voltage time of the first frame */

        monitor_voltage_result_time = (monitor_voltage_time_last + 1 + (MONITOR_CONV_FRAME_SIZE - adc_result_count)) % MONITOR_CONV_FRAME_SIZE; 
        adc_voltage_total = 0;

        for(int i = 0;i < adc_results;i += SOC_ADC_DIGI_RESULT_BYTES) {
            adc_digi_output_data_t *p = (adc_digi_output_data_t*)&monitor_result[i];
            adc_result_value = p->type1.data;
            adc_cali_raw_to_voltage(monitor_adc_cali, adc_result_value, &voltage);
            adc_voltage_total += voltage;

            if (voltage < (monitor_zero_amps + MONITOR_ZERO_OFFSET)) {
                current = ((monitor_zero_amps + MONITOR_ZERO_OFFSET - voltage) * 100) / MONITOR_CURRENT_MV_PER_A;
                if (current > current_max) {
                    current_max = current;
                }

                power = monitor_voltage[monitor_voltage_result_time] * current;
                energy += power;
            } else {
                current = 0;
                power = 0;
            }

            monitor_voltage_result_time = (monitor_voltage_result_time + 1) % MONITOR_CONV_FRAME_SIZE; 
        }

        monitor_adc_average = adc_voltage_total / adc_result_count;

        /* Only half the waveform was visible to the ADC
         * assume the other half is the same.
         */

        energy_total += energy * 2; 
        adc_conv_total_time += adc_last_conv_time;
        monitor_zx_count_total += monitor_zx_count_last;
        count++;

        if (count >= 100 || (!monitor_event.is_on && monitor_zx_count_last > MONITOR_MIN_ZX_ON)) {
            milli_watt_seconds = ((energy_total / count) / (400 * 50));
            average_power = milli_watt_seconds / 2;
            current_is_on = monitor_event.is_on;

            memset(&monitor_event, 0, sizeof(monitor_event));
            monitor_event.voltage_type = MONITOR_VOLTAGE_AC_RMS;
            monitor_event.voltage = 23000;

            if (monitor_zx_count_last > MONITOR_MIN_ZX_ON || current_is_on) {
                monitor_event.energy = milli_watt_seconds;
                monitor_event.power = average_power;
                monitor_event.current_max = current_max;
                monitor_event.zx = monitor_zx_count_total; 
                monitor_event.time = adc_conv_total_time;
            } else {
                monitor_event.energy = 0;
                monitor_event.power = 0;
                monitor_event.current_max = 0;
                monitor_event.zx = monitor_zx_count_total; 
                monitor_event.time = adc_conv_total_time;
            }

            monitor_event.is_on = monitor_zx_count_last > MONITOR_MIN_ZX_ON;

            esp_event_post_to(monitor_event_handle, MONITOR_EVENTS, MONITOR_EVENT_STATE, &monitor_event, sizeof(monitor_event), portMAX_DELAY);

            ESP_LOGD(TAG, "Ws: %ld, W: %ld lADCr: %d ADCv: %d vTime: %d ADCrc: %ld ZXc:%d adcPeriod: %lld adcAverage:%ld", milli_watt_seconds, average_power, adc_result_value, voltage, monitor_voltage_result_time, adc_result_count, monitor_zx_count_last, adc_last_conv_time, monitor_adc_average);
            
            energy_total = 0;
            adc_conv_total_time = 0;
            monitor_zx_count_total = 0;
            count = 0;
        }
    }
}
