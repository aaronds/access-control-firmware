#include "monitor.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_adc/adc_oneshot.h"
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

unsigned int monitor_voltage[] = {0,4,7,11,14,18,22,25,29,32,36,40,43,47,50,54,57,61,64,68,71,75,78,81,85,88,91,95,98,101,104,108,111,114,117,120,123,126,129,132,135,138,141,144,147,149,152,155,157,160,163,165,168,170,173,175,177,179,182,184,186,188,190,192,194,196,198,200,202,203,205,207,208,210,211,212,214,215,216,218,219,220,221,222,223,224,224,225,226,227,227,228,228,229,229,229,230,230,230,230,230,230,230,230,230,229,229,229,228,228,227,227,226,225,224,224,223,222,221,220,219,218,216,215,214,212,211,210,208,207,205,203,202,200,198,196,194,192,190,188,186,184,182,179,177,175,173,170,168,165,163,160,157,155,152,149,147,144,141,138,135,132,129,126,123,120,117,114,111,108,104,101,98,95,91,88,85,81,78,75,71,68,64,61,57,54,50,47,43,40,36,32,29,25,22,18,14,11,7,4,0,4,7,11,14,18,22,25,29,32,36,40,43,47,50,54,57,61,64,68,71,75,78,81,85,88,91,95,98,101,104,108,111,114,117,120,123,126,129,132,135,138,141,144,147,149,152,155,157,160,163,165,168,170,173,175,177,179,182,184,186,188,190,192,194,196,198,200,202,203,205,207,208,210,211,212,214,215,216,218,219,220,221,222,223,224,224,225,226,227,227,228,228,229,229,229,230,230,230,230,230,230,230,230,230,229,229,229,228,228,227,227,226,225,224,224,223,222,221,220,219,218,216,215,214,212,211,210,208,207,205,203,202,200,198,196,194,192,190,188,186,184,182,179,177,175,173,170,168,165,163,160,157,155,152,149,147,144,141,138,135,132,129,126,123,120,117,114,111,108,104,101,98,95,91,88,85,81,78,75,71,68,64,61,57,54,50,47,43,40,36,32,29,25,22,18,14,11,7,4,0};
unsigned int monitor_voltage_time = 0;
unsigned int monitor_voltage_time_adc_conv = 0;
unsigned int monitor_zx_count = 0;
unsigned int monitor_zx_count_last = 0;
uint32_t monitor_zx_count_total = 0;
unsigned int monitor_buffer_idx = 0;
bool monitor_started = false;

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

esp_err_t monitor_start() {
    gptimer_start(monitor_timer);
    ESP_ERROR_CHECK(adc_continuous_start(monitor_adc));

    monitor_started = true;

    ESP_LOGI(TAG, "Monitor start OK");
    return ESP_OK;
}

esp_err_t monitor_pause() {
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

        for(int i = 0;i < adc_results;i += SOC_ADC_DIGI_RESULT_BYTES) {
            adc_digi_output_data_t *p = (adc_digi_output_data_t*)&monitor_result[i];
            adc_result_value = p->type1.data;
            adc_cali_raw_to_voltage(monitor_adc_cali, adc_result_value, &voltage);

            if (voltage < MONITOR_ZERO_AMPS) {
                current = ((MONITOR_ZERO_AMPS - voltage) * 100) / MONITOR_CURRENT_MV_PER_A;
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

        /* Only half the waveform was visible to the ADC
         * assume the other half is the same.
         */

        energy_total += energy * 2; 
        adc_conv_total_time += adc_last_conv_time;
        monitor_zx_count_total += monitor_zx_count_last;
        count++;

        if (count >= 100 || (!monitor_event.is_on && monitor_zx_count_last > 0)) {
            milli_watt_seconds = ((energy_total / count) / (400 * 50));
            average_power = milli_watt_seconds / 2;
            current_is_on = monitor_event.is_on;

            memset(&monitor_event, 0, sizeof(monitor_event));
            monitor_event.voltage_type = MONITOR_VOLTAGE_AC_RMS;
            monitor_event.voltage = 23000;

            if (monitor_zx_count_last > 0 || current_is_on) {
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

            monitor_event.is_on = monitor_zx_count_last > 0;

            esp_event_post_to(monitor_event_handle, MONITOR_EVENTS, MONITOR_EVENT_STATE, &monitor_event, sizeof(monitor_event), portMAX_DELAY);

            ESP_LOGD(TAG, "mWs: %ld, mW: %ld lADCr: %d ADCv: %d vTime: %d ADCrc: %ld ZXc:%d adcPeriod: %lld", milli_watt_seconds, average_power, adc_result_value, voltage, monitor_voltage_result_time, adc_result_count, monitor_zx_count_last, adc_last_conv_time);
            
            energy_total = 0;
            adc_conv_total_time = 0;
            monitor_zx_count_total = 0;
            count = 0;
        }
    }
}
