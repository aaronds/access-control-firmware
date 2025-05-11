#pragma once

#include "esp_err.h"
#include <string.h>
#include "cJSON.h"


esp_err_t http_api_init(void);
esp_err_t http_api_unlock(uint8_t* card_id, int len);
esp_err_t http_api_lock(void);
esp_err_t http_api_enroll(uint8_t* inductor_id, int inductor_id_len, uint8_t* inductee_id, int inductee_id_len);
esp_err_t http_api_has_update(char* out_url, size_t out_url_len);
esp_err_t http_api_settings(unsigned int *threshold, unsigned int *timeout);
esp_err_t http_api_ota(const char* url);
