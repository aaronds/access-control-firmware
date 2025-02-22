#pragma once

#include <esp_event.h>

ESP_EVENT_DECLARE_BASE(CONTROLLER_MODE_EVENTS);

typedef enum {
    CONTROLLER_MODE_INITIALISING,
    CONTROLLER_MODE_LOCKED,
    CONTROLLER_MODE_UNLOCKED,
    CONTROLLER_MODE_IN_USE,
    CONTROLLER_MODE_AWAIT_INDUCTOR,
    CONTROLLER_MODE_ENROLL,
} controller_mode_t;

typedef enum {
    CONTROLLER_MODE_EVENT_ANY = ESP_EVENT_ANY_ID,
    CONTROLLER_MODE_EVENT_INITIALISING,
    CONTROLLER_MODE_EVENT_LOCKED,
    CONTROLLER_MODE_EVENT_UNLOCKED,
    CONTROLLER_MODE_EVENT_IN_USE,
    CONTROLLER_MODE_EVENT_AWAIT_INDUCTOR,
    CONTROLLER_MODE_EVENT_ENROLL
} controller_mode_event_t;

extern controller_mode_t controller_mode_previous;
extern controller_mode_t controller_mode;
extern esp_event_loop_handle_t controller_mode_event_handle;

esp_err_t controller_mode_init(esp_event_loop_handle_t event_loop);
esp_err_t controller_mode_set(controller_mode_t new_mode);

