#include "http_api.h"

#include "esp_mac.h"
#include "esp_log.h"
#include "esp_ota_ops.h"

#include "http_rest_json_client.h"

#define CARD_ID_MAX_LEN 4
#define MACHINE_MAC_LEN 6

static const char* TAG = "http_api";

static char mac_str[(MACHINE_MAC_LEN*2)+1] = {0};
static char url_buf[200];


esp_err_t format_hex_string(uint8_t* data, int data_len, char* out_str, size_t out_str_len)
{
    if ((data_len*2)+1 > out_str_len) {
        return ESP_FAIL;
    }

    char* out_str_end = out_str;
    for (int i=data_len-1; i>=0; i--)
    {
        sprintf(out_str_end, "%02hhx", data[i]);
        out_str_end += 2;
    }
    return ESP_OK;
}

esp_err_t http_api_init(void)
{
    uint8_t mac[MACHINE_MAC_LEN];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char* mac_str_end = mac_str;
    for (int i=0; i<MACHINE_MAC_LEN; i++)
    {
        sprintf(mac_str_end, "%02hhx", mac[i]);
        mac_str_end += 2;
    }
    return ESP_OK;
}

esp_err_t http_api_unlock(uint8_t* card_id, int len)
{
    esp_err_t ret;

    if (!card_id) {
        return ESP_ERR_INVALID_ARG;
    }

    char card_id_str[(CARD_ID_MAX_LEN*2)+1] = {0};
    ret = format_hex_string(card_id, len, card_id_str, sizeof(card_id_str));
    if (ret != ESP_OK) {
        return ret;
    }

    http_rest_recv_buffer_t response_buffer = {0};

    snprintf(url_buf, sizeof(url_buf), CONFIG_BASE_URL "/api/machines/%s/unlock?card_id=%s", mac_str, card_id_str);
    ret = http_rest_client_get(url_buf, &response_buffer);

    if (ret != ESP_OK) {
        goto cleanup;
    }

    if (response_buffer.status_code != 200){
        ESP_LOGE(TAG, "an http error occured: %d", response_buffer.status_code);
        ret = ESP_FAIL;
        goto cleanup;
    }

cleanup:
    http_rest_client_cleanup(&response_buffer);
    return ret;
}

esp_err_t http_api_lock(void)
{
    esp_err_t ret;

    http_rest_recv_buffer_t response_buffer = {0};

    snprintf(url_buf, sizeof(url_buf), CONFIG_BASE_URL "/api/machines/%s/lock", mac_str);
    ret = http_rest_client_get(url_buf, &response_buffer);

    if (ret != ESP_OK) {
        goto cleanup;
    }

    if (response_buffer.status_code != 200){
        ESP_LOGE(TAG, "an http error occured: %d", response_buffer.status_code);
        ret = ESP_FAIL;
        goto cleanup;
    }

cleanup:
    http_rest_client_cleanup(&response_buffer);
    return ret;
}

esp_err_t http_api_enroll(uint8_t* inductor_id, int inductor_id_len, uint8_t* inductee_id, int inductee_id_len)
{
    esp_err_t ret;

    if (!inductor_id) {
        return ESP_ERR_INVALID_ARG;
    }

    char card_id_str[(CARD_ID_MAX_LEN*2)+1] = {0};
    ret = format_hex_string(inductor_id, inductor_id_len, card_id_str, sizeof(card_id_str));
    if (ret != ESP_OK) {
        return ret;
    }

    size_t url_len = snprintf(url_buf, sizeof(url_buf), CONFIG_BASE_URL "/api/machines/%s/enroll?inductor_id=%s", mac_str, card_id_str);

    if (inductee_id) {
        ret = format_hex_string(inductee_id, inductee_id_len, card_id_str, sizeof(card_id_str));
        if (ret != ESP_OK) {
            return ret;
        }

        snprintf(url_buf+url_len, sizeof(url_buf)-url_len, "&inductee_id=%s", card_id_str);
    }

    http_rest_recv_buffer_t response_buffer = {0};
    ret = http_rest_client_get(url_buf, &response_buffer);

    if (ret != ESP_OK) {
        goto cleanup;
    }

    if (response_buffer.status_code != 200){
        ESP_LOGE(TAG, "an http error occured: %d", response_buffer.status_code);
        ret = ESP_FAIL;
        goto cleanup;
    }

cleanup:
    http_rest_client_cleanup(&response_buffer);
    return ret;
}

esp_err_t http_api_settings(unsigned int *threshold, unsigned int *timeout) 
{
    esp_err_t ret;

    size_t url_len = snprintf(url_buf, sizeof(url_buf), CONFIG_BASE_URL "/api/machines/%s/settings", mac_str);

    http_rest_recv_json_t response_buffer = {0};
    ret = http_rest_client_get_json(url_buf, &response_buffer);

    if (ret != ESP_OK) {
        goto cleanup;
    }

    if (response_buffer.status_code != 200){
        ESP_LOGE(TAG, "an http error occured: %d", response_buffer.status_code);
        ret = ESP_FAIL;
        goto cleanup;
    }

    if (!cJSON_IsObject(response_buffer.json))
    {
        goto cleanup;
    }

    cJSON *idle_timeout_json = cJSON_GetObjectItemCaseSensitive(response_buffer.json, "idle_timeout");
    double idle_timeout = cJSON_GetNumberValue(idle_timeout_json);

    if (idle_timeout < 0.01) {
        *timeout = 0; 
    } else {
        *timeout = (unsigned int) idle_timeout; 
    }

    cJSON *idle_power_threshold_json = cJSON_GetObjectItemCaseSensitive(response_buffer.json, "idle_power_threshold");
    double idle_power_threshold = cJSON_GetNumberValue(idle_power_threshold_json);
    
    if (idle_power_threshold < 0.01) {
        *threshold = 0;
    } else {
        *threshold = (unsigned int) idle_power_threshold;
    }

cleanup:
    http_rest_client_cleanup_json(&response_buffer);
    return ret;
}

esp_err_t http_api_has_update(char* out_url, size_t out_url_len)
{
    esp_err_t ret = ESP_OK;

    http_rest_recv_json_t response_buffer = {0};

    snprintf(url_buf, sizeof(url_buf), CONFIG_BASE_URL "/api/machines/%s/has_update", mac_str);
    ret = http_rest_client_get_json(url_buf, &response_buffer);

    if (ret != ESP_OK) {
        goto cleanup;
    }

    if (response_buffer.status_code != 200){
        ESP_LOGE(TAG, "an http error occured: %d", response_buffer.status_code);
        ret = ESP_FAIL;
        goto cleanup;
    }

    if (!cJSON_IsObject(response_buffer.json))
    {
        goto cleanup;
    }

    cJSON *update_url_json = cJSON_GetObjectItemCaseSensitive(response_buffer.json, "update_url");
    char* url = cJSON_GetStringValue(update_url_json);

    if (!url) {
        goto cleanup;
    }

    size_t url_strlen = strlen(url);
    if (url_strlen+1 > out_url_len)
    {
        ESP_LOGE(TAG, "Not enough space in out_url");
        ret = ESP_ERR_INVALID_ARG;
        goto cleanup;
    }

    memcpy(out_url, url, url_strlen+1);

cleanup:
    http_rest_client_cleanup_json(&response_buffer);
    return ret;
}

#define OTA_BUFFSIZE 1024

esp_err_t http_api_ota(const char* url)
{
    esp_err_t err;
    const esp_partition_t *update_partition = NULL;
    esp_ota_handle_t update_handle = 0 ;
    static char ota_write_data[OTA_BUFFSIZE + 1] = { 0 };

    esp_http_client_config_t http_cfg = {
        .url = url,
        .keep_alive_enable = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);

    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        goto http_cleanup;
    }

    esp_http_client_fetch_headers(client);

    update_partition = esp_ota_get_next_update_partition(NULL);
    assert(update_partition != NULL);
    ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%"PRIx32,
             update_partition->subtype, update_partition->address);

    int binary_file_length = 0;

    err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed (%s)", esp_err_to_name(err));
        goto ota_cleanup;
    }

    ESP_LOGI(TAG, "Beginning firmware download");
    while (1) {
        int data_read = esp_http_client_read(client, ota_write_data, OTA_BUFFSIZE);
        if (data_read < 0) {
            err = data_read;
            ESP_LOGE(TAG, "Data read error");
            goto ota_cleanup;
        } else if (data_read > 0) {
            err = esp_ota_write( update_handle, (const void *)ota_write_data, data_read);
            if (err != ESP_OK) {
                goto ota_cleanup;
            }
            binary_file_length += data_read;


        } else if (data_read == 0) {
             if (errno == ECONNRESET || errno == ENOTCONN) {
                ESP_LOGE(TAG, "Connection closed, errno = %d", errno);
                break;
            }
            if (esp_http_client_is_complete_data_received(client) == true) {
                ESP_LOGI(TAG, "Connection closed");
                break;
            }
        }
    }

    ESP_LOGI(TAG, "Total firmware update size: %d", binary_file_length);
    if (esp_http_client_is_complete_data_received(client) != true) {
        ESP_LOGE(TAG, "Error in receiving complete file");
        goto ota_cleanup;
    }

    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed, image is corrupted");
        } else {
            ESP_LOGE(TAG, "esp_ota_end failed (%s)!", esp_err_to_name(err));
        }
        goto http_cleanup;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed (%s)!", esp_err_to_name(err));
        goto http_cleanup;
    }

    goto http_cleanup;

ota_cleanup:
    esp_ota_abort(update_handle);
http_cleanup:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return err;
}
