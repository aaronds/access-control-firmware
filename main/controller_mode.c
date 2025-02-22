#include "controller_mode.h"

controller_mode_t controller_mode_previous;
controller_mode_t controller_mode;
esp_event_loop_handle_t controller_mode_event_handle;

ESP_EVENT_DEFINE_BASE(CONTROLLER_MODE_EVENTS);

esp_err_t controller_mode_init(esp_event_loop_handle_t event_loop) {
    controller_mode_event_handle = event_loop;
    
    return ESP_OK; 
}

esp_err_t controller_mode_set(controller_mode_t new_mode) {
    controller_mode_previous = controller_mode;
    controller_mode = new_mode;
    
    esp_event_post_to(controller_mode_event_handle, CONTROLLER_MODE_EVENTS, controller_mode, &controller_mode_previous, sizeof(controller_mode_previous), portMAX_DELAY);

    return ESP_OK;
}
