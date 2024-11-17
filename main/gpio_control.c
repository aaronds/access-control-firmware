
#include <driver/gpio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_log.h"

#include "gpio_control.h"

ESP_EVENT_DEFINE_BASE(GPIO_EVENTS);

static const char* TAG = "gpio";

#define RELAY_GPIO_PIN (26)
#define LED_GPIO_PIN (19)
#define BUTTON_GPIO_PIN (4)
#define DEBOUNCE_INTERVAL_MS (100)

static esp_event_loop_handle_t gpio_event_handle;

static StaticTimer_t debounce_timer_buffer;
static TimerHandle_t debounce_timer;
static bool last_button_state = false;


static void IRAM_ATTR button_isr_handler(void* arg)
{
    // Start the timer to check the GPIO after the debounce time
    BaseType_t wake_higher_prio_task = pdFALSE;
    xTimerStartFromISR(debounce_timer, &wake_higher_prio_task);
}

static void debounce_timer_handler(TimerHandle_t timer)
{
    bool current_button_state = (gpio_get_level(BUTTON_GPIO_PIN));
    if(current_button_state != last_button_state)
    {
        last_button_state = current_button_state;

        if (current_button_state)
        {
            ESP_LOGI(TAG, "Button press");
            esp_event_isr_post_to(gpio_event_handle, GPIO_EVENTS, GPIO_EVENT_BUTTON_PRESS, NULL, 0, NULL);
        }
        else
        {
            ESP_LOGI(TAG, "Button release");
            esp_event_isr_post_to(gpio_event_handle, GPIO_EVENTS, GPIO_EVENT_BUTTON_RELEASE, NULL, 0, NULL);
        }
    }
}

esp_err_t gpio_control_init(esp_event_loop_handle_t event_handle)
{
    gpio_event_handle = event_handle;

    gpio_config_t relay_conf = {0};
    relay_conf.intr_type = GPIO_INTR_DISABLE;
    relay_conf.mode = GPIO_MODE_OUTPUT;
    relay_conf.pin_bit_mask = (1ULL << RELAY_GPIO_PIN);
    relay_conf.pull_down_en = 0;
    relay_conf.pull_up_en = 0;
    gpio_config(&relay_conf);

    gpio_config_t btn_conf = {0};
    btn_conf.intr_type = GPIO_INTR_ANYEDGE;
    btn_conf.pin_bit_mask = (1ULL << BUTTON_GPIO_PIN);
    btn_conf.mode = GPIO_MODE_INPUT;
    btn_conf.pull_down_en = 1;
    gpio_config(&btn_conf);
    debounce_timer = xTimerCreateStatic("debounceTimer",
                                DEBOUNCE_INTERVAL_MS/portTICK_PERIOD_MS,
                                false,
                                NULL,
                                debounce_timer_handler,
                                &debounce_timer_buffer);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_GPIO_PIN, button_isr_handler, NULL);

    return ESP_OK;
}

void gpio_set_relay(bool on)
{
    gpio_set_level(RELAY_GPIO_PIN, on?1:0);
}