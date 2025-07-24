#include "status_indicator.h"

#include "led_indicator.h"

#define LED_INDICATOR_IO_NUM 19

const blink_step_t blink_error_brief[] = {
    {LED_BLINK_HOLD, LED_STATE_ON, 500},
    {LED_BLINK_HOLD, LED_STATE_OFF, 500},
    {LED_BLINK_HOLD, LED_STATE_ON, 500},
    {LED_BLINK_HOLD, LED_STATE_OFF, 500},
    {LED_BLINK_HOLD, LED_STATE_ON, 500},
    {LED_BLINK_HOLD, LED_STATE_OFF, 500},
    {LED_BLINK_STOP, 0, 0},
};

const blink_step_t blink_error_fatal[] = {
    {LED_BLINK_HOLD, LED_STATE_ON, 500},
    {LED_BLINK_HOLD, LED_STATE_OFF, 500},
    {LED_BLINK_LOOP, 0, 0},
};

const blink_step_t blink_enroll_success[] = {
    {LED_BLINK_HOLD, LED_STATE_ON, 1500},
    {LED_BLINK_HOLD, LED_STATE_OFF, 750},
    {LED_BLINK_STOP, 0, 0},
};

const blink_step_t blink_enroll[] = {
    {LED_BLINK_HOLD, LED_STATE_ON, 250},
    {LED_BLINK_HOLD, LED_STATE_OFF, 750},
    {LED_BLINK_LOOP, 0, 0},
};

const blink_step_t blink_await_inductor[] = {
    {LED_BLINK_HOLD, LED_STATE_ON, 250},
    {LED_BLINK_LOOP, 0, 0},
};

const blink_step_t blink_output_on[] = {
    {LED_BLINK_HOLD, LED_STATE_OFF, 500},
    {LED_BLINK_LOOP, 0, 0},
};

const blink_step_t blink_idle[] = {
    {LED_BLINK_BREATHE, LED_STATE_ON, 500},
    {LED_BLINK_BREATHE, LED_STATE_25_PERCENT, 500},
    {LED_BLINK_LOOP, 0, 0},
};

const blink_step_t blink_observe[] = {
    {LED_BLINK_BREATHE, LED_STATE_ON, 30},
    {LED_BLINK_BREATHE, LED_STATE_25_PERCENT, 30},
    {LED_BLINK_BREATHE, LED_STATE_25_PERCENT, 8000},
    {LED_BLINK_LOOP, 0, 0},
};

typedef enum {
    STATUS_INDICATOR_ERROR_BRIEF,
    STATUS_INDICATOR_ERROR_FATAL,
    STATUS_INDICATOR_ENROLL_SUCCESS,
    STATUS_INDICATOR_ENROLL,
    STATUS_INDICATOR_AWAIT_INDUCTOR,
    STATUS_INDICATOR_OUTPUT_ON,
    STATUS_INDICATOR_IDLE,
    STATUS_INDICATOR_OBSERVE,
    STATUS_INDICATOR_MAX,                 /**< INVALID type */
} status_indicator_blink_type_t;

static blink_step_t const *led_blink_list[] = {
    [STATUS_INDICATOR_ERROR_BRIEF] = blink_error_brief,
    [STATUS_INDICATOR_ERROR_FATAL] = blink_error_fatal,
    [STATUS_INDICATOR_ENROLL_SUCCESS] = blink_enroll_success,
    [STATUS_INDICATOR_ENROLL] = blink_enroll,
    [STATUS_INDICATOR_AWAIT_INDUCTOR] = blink_await_inductor,
    [STATUS_INDICATOR_OUTPUT_ON] = blink_output_on,
    [STATUS_INDICATOR_IDLE] = blink_idle,
    [STATUS_INDICATOR_OBSERVE] = blink_observe,
    [STATUS_INDICATOR_MAX] = NULL,
};

static led_indicator_handle_t led_handle = NULL;

void status_indicator_init(void)
{
    led_indicator_ledc_config_t led_indicator_ledc_config = {
        .is_active_level_high = 1,
        .timer_inited = false,
        .timer_num = LEDC_TIMER_0,
        .gpio_num = LED_INDICATOR_IO_NUM,
        .channel = LEDC_CHANNEL_0,
    };

    led_indicator_config_t config = {
        .mode = LED_LEDC_MODE,
        .led_indicator_ledc_config = &led_indicator_ledc_config,
        .blink_lists = led_blink_list,
        .blink_list_num = STATUS_INDICATOR_MAX,
    };

    led_handle = led_indicator_create(&config);
}

void status_indicator_idle(void) {
    led_indicator_stop(led_handle, STATUS_INDICATOR_ENROLL);
    led_indicator_stop(led_handle, STATUS_INDICATOR_OUTPUT_ON);
    led_indicator_stop(led_handle, STATUS_INDICATOR_AWAIT_INDUCTOR);
    led_indicator_start(led_handle, STATUS_INDICATOR_IDLE);
}

void status_indicator_observe(void) {
    led_indicator_stop(led_handle, STATUS_INDICATOR_ENROLL);
    led_indicator_stop(led_handle, STATUS_INDICATOR_OUTPUT_ON);
    led_indicator_stop(led_handle, STATUS_INDICATOR_AWAIT_INDUCTOR);
    led_indicator_start(led_handle, STATUS_INDICATOR_OBSERVE);
}

void status_indicator_output_on(void) {
    led_indicator_stop(led_handle, STATUS_INDICATOR_ENROLL);
    led_indicator_stop(led_handle, STATUS_INDICATOR_IDLE);
    led_indicator_start(led_handle, STATUS_INDICATOR_OUTPUT_ON);
}

void status_indicator_await_inductor(void) {
    led_indicator_stop(led_handle, STATUS_INDICATOR_IDLE);
    led_indicator_stop(led_handle, STATUS_INDICATOR_ENROLL);
    led_indicator_start(led_handle, STATUS_INDICATOR_AWAIT_INDUCTOR);
}

void status_indicator_enroll(void) {
    led_indicator_stop(led_handle, STATUS_INDICATOR_AWAIT_INDUCTOR);
    led_indicator_start(led_handle, STATUS_INDICATOR_ENROLL);
}

void status_indicator_enroll_success(void) {
    led_indicator_start(led_handle, STATUS_INDICATOR_ENROLL_SUCCESS);
}

void status_indicator_error_brief(void) {
    led_indicator_preempt_start(led_handle, STATUS_INDICATOR_ERROR_BRIEF);
}

void status_indicator_error_fatal(void) {
    led_indicator_start(led_handle, STATUS_INDICATOR_ERROR_FATAL);
}
