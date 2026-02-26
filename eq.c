#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "cJSON.h"
#include "gmc_eq.h"
#include "gmc_protocol.h"

static const char *TAG = "gmc_eq";

// EQ mode names
static const char *eq_mode_names[] = {
    "",             // 0 - unused
    "Movie",        // 1
    "Music",        // 2
    "Voice",        // 3
    "Stadium",      // 4
    "Standard",     // 5
    "Direct",       // 6
    "Part",         // 7
    "Custom"        // 8
};

static uint8_t current_eq_mode = EQ_MODE_DEFAULT;
static eq_mode_t eq_modes[EQ_MODE_MAX + 1];
static SemaphoreHandle_t eq_mutex = NULL;
static eq_mode_change_callback_t eq_callback = NULL;

// Initialize default EQ bands for each mode
static void init_default_eq_modes(void)
{
    memset(eq_modes, 0, sizeof(eq_modes));
    
    // Standard mode with default flat response
    eq_modes[EQ_MODE_STANDARD].mode_id = EQ_MODE_STANDARD;
    strcpy(eq_modes[EQ_MODE_STANDARD].name, "Standard");
    eq_modes[EQ_MODE_STANDARD].band_count = 5;
    
    // Default 5-band EQ settings
    eq_modes[EQ_MODE_STANDARD].bands[0] = (eq_band_t){0, 60.0f, 0.0f, 1.0f, 0};
    eq_modes[EQ_MODE_STANDARD].bands[1] = (eq_band_t){1, 250.0f, 0.0f, 1.0f, 0};
    eq_modes[EQ_MODE_STANDARD].bands[2] = (eq_band_t){2, 1000.0f, 0.0f, 1.0f, 0};
    eq_modes[EQ_MODE_STANDARD].bands[3] = (eq_band_t){3, 4000.0f, 0.0f, 1.0f, 0};
    eq_modes[EQ_MODE_STANDARD].bands[4] = (eq_band_t){4, 12000.0f, 0.0f, 1.0f, 0};
    
    // Movie mode - enhanced bass and highs
    eq_modes[EQ_MODE_MOVIE].mode_id = EQ_MODE_MOVIE;
    strcpy(eq_modes[EQ_MODE_MOVIE].name, "Movie");
    eq_modes[EQ_MODE_MOVIE].band_count = 5;
    eq_modes[EQ_MODE_MOVIE].bands[0] = (eq_band_t){0, 60.0f, 3.0f, 1.0f, 0};
    eq_modes[EQ_MODE_MOVIE].bands[1] = (eq_band_t){1, 250.0f, 0.0f, 1.0f, 0};
    eq_modes[EQ_MODE_MOVIE].bands[2] = (eq_band_t){2, 1000.0f, -1.0f, 1.0f, 0};
    eq_modes[EQ_MODE_MOVIE].bands[3] = (eq_band_t){3, 4000.0f, 2.0f, 1.0f, 0};
    eq_modes[EQ_MODE_MOVIE].bands[4] = (eq_band_t){4, 12000.0f, 3.0f, 1.0f, 0};
    
    // Music mode - balanced
    eq_modes[EQ_MODE_MUSIC].mode_id = EQ_MODE_MUSIC;
    strcpy(eq_modes[EQ_MODE_MUSIC].name, "Music");
    eq_modes[EQ_MODE_MUSIC].band_count = 5;
    eq_modes[EQ_MODE_MUSIC].bands[0] = (eq_band_t){0, 60.0f, 2.0f, 1.0f, 0};
    eq_modes[EQ_MODE_MUSIC].bands[1] = (eq_band_t){1, 250.0f, 1.0f, 1.0f, 0};
    eq_modes[EQ_MODE_MUSIC].bands[2] = (eq_band_t){2, 1000.0f, 0.0f, 1.0f, 0};
    eq_modes[EQ_MODE_MUSIC].bands[3] = (eq_band_t){3, 4000.0f, 1.0f, 1.0f, 0};
    eq_modes[EQ_MODE_MUSIC].bands[4] = (eq_band_t){4, 12000.0f, 2.0f, 1.0f, 0};
    
    // Voice mode - enhanced mids
    eq_modes[EQ_MODE_VOICE].mode_id = EQ_MODE_VOICE;
    strcpy(eq_modes[EQ_MODE_VOICE].name, "Voice");
    eq_modes[EQ_MODE_VOICE].band_count = 5;
    eq_modes[EQ_MODE_VOICE].bands[0] = (eq_band_t){0, 60.0f, -2.0f, 1.0f, 0};
    eq_modes[EQ_MODE_VOICE].bands[1] = (eq_band_t){1, 250.0f, 1.0f, 1.0f, 0};
    eq_modes[EQ_MODE_VOICE].bands[2] = (eq_band_t){2, 1000.0f, 3.0f, 1.0f, 0};
    eq_modes[EQ_MODE_VOICE].bands[3] = (eq_band_t){3, 4000.0f, 2.0f, 1.0f, 0};
    eq_modes[EQ_MODE_VOICE].bands[4] = (eq_band_t){4, 12000.0f, -1.0f, 1.0f, 0};
    
    // Initialize other modes with default values
    for (int i = EQ_MODE_STADIUM; i <= EQ_MODE_CUSTOM; i++) {
        eq_modes[i].mode_id = i;
        strncpy(eq_modes[i].name, eq_mode_names[i], sizeof(eq_modes[i].name) - 1);
        eq_modes[i].band_count = 5;
        memcpy(eq_modes[i].bands, eq_modes[EQ_MODE_STANDARD].bands, sizeof(eq_band_t) * 5);
    }
}

esp_err_t gmc_eq_init(void)
{
    eq_mutex = xSemaphoreCreateMutex();
    if (eq_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    init_default_eq_modes();
    current_eq_mode = EQ_MODE_DEFAULT;
    
    ESP_LOGI(TAG, "EQ module initialized, default mode: %d", current_eq_mode);
    return ESP_OK;
}

void gmc_eq_register_callback(eq_mode_change_callback_t callback)
{
    eq_callback = callback;
}

esp_err_t gmc_eq_get_mode(uint8_t *mode_id)
{
    if (mode_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(eq_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    *mode_id = current_eq_mode;
    
    xSemaphoreGive(eq_mutex);
    return ESP_OK;
}

esp_err_t gmc_eq_set_mode(uint8_t mode_id)
{
    if (mode_id < EQ_MODE_MIN || mode_id > EQ_MODE_MAX) {
        ESP_LOGE(TAG, "Invalid EQ mode: %d", mode_id);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(eq_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    current_eq_mode = mode_id;
    
    xSemaphoreGive(eq_mutex);
    
    ESP_LOGI(TAG, "EQ mode set to: %d (%s)", mode_id, eq_mode_names[mode_id]);
    
    // Call callback if registered
    if (eq_callback != NULL) {
        eq_callback(mode_id);
    }
    
    return ESP_OK;
}

esp_err_t gmc_eq_get_filter(uint8_t eq_id, eq_mode_t *mode)
{
    if (mode == NULL || eq_id < EQ_MODE_MIN || eq_id > EQ_MODE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(eq_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    *mode = eq_modes[eq_id];
    
    xSemaphoreGive(eq_mutex);
    return ESP_OK;
}

esp_err_t gmc_eq_set_filter(uint8_t eq_id, const eq_band_t *band)
{
    if (band == NULL || eq_id < EQ_MODE_MIN || eq_id > EQ_MODE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (band->band_id >= MAX_EQ_BANDS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(eq_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    // Update the specific band
    uint8_t bid = band->band_id;
    if (bid < eq_modes[eq_id].band_count) {
        eq_modes[eq_id].bands[bid] = *band;
        ESP_LOGI(TAG, "Updated EQ filter: mode=%d, band=%d, f=%.1f, g=%.1f", 
                 eq_id, bid, band->frequency, band->gain);
    }
    
    xSemaphoreGive(eq_mutex);
    return ESP_OK;
}

esp_err_t gmc_eq_handle_get_mode_list(void)
{
    cJSON *data = cJSON_CreateArray();
    if (data == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    // Add all supported EQ modes
    for (int i = EQ_MODE_MIN; i <= EQ_MODE_MAX; i++) {
        cJSON *mode = cJSON_CreateObject();
        cJSON_AddNumberToObject(mode, "type", i);
        cJSON_AddStringToObject(mode, "name", eq_mode_names[i]);
        cJSON_AddItemToArray(data, mode);
    }
    
    esp_err_t ret = gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_AUDIO_CONTROL, "getEQModeList", 0, "success", data);
    
    cJSON_Delete(data);
    return ret;
}

esp_err_t gmc_eq_handle_set_mode(uint8_t mode_id)
{
    esp_err_t ret = gmc_eq_set_mode(mode_id);
    
    if (ret == ESP_OK) {
        gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_AUDIO_CONTROL, "setEQMode", 0, "success", NULL);
        // Send notification
        gmc_eq_notify_mode_changed(mode_id);
    } else {
        gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_AUDIO_CONTROL, "setEQMode", -1, "Invalid EQ mode", NULL);
    }
    
    return ret;
}

esp_err_t gmc_eq_handle_get_mode(void)
{
    uint8_t mode_id = 0;
    esp_err_t ret = gmc_eq_get_mode(&mode_id);
    
    if (ret == ESP_OK) {
        cJSON *data = cJSON_CreateNumber(mode_id);
        gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_AUDIO_CONTROL, "getEQMode", 0, "success", data);
        cJSON_Delete(data);
    } else {
        gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_AUDIO_CONTROL, "getEQMode", -1, "Failed to get EQ mode", NULL);
    }
    
    return ret;
}

esp_err_t gmc_eq_handle_get_filter(uint8_t eq_id)
{
    eq_mode_t mode;
    esp_err_t ret = gmc_eq_get_filter(eq_id, &mode);
    
    if (ret != ESP_OK) {
        ret = gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_AUDIO_CONTROL, "getEQFilter", -1, "Invalid EQ ID", NULL);
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "eid", mode.mode_id);
    cJSON_AddNumberToObject(data, "count", mode.band_count);
    
    cJSON *list = cJSON_CreateArray();
    for (int i = 0; i < mode.band_count; i++) {
        cJSON *band = cJSON_CreateObject();
        cJSON_AddNumberToObject(band, "bid", mode.bands[i].band_id);
        cJSON_AddNumberToObject(band, "f", mode.bands[i].frequency);
        cJSON_AddNumberToObject(band, "g", mode.bands[i].gain);
        cJSON_AddNumberToObject(band, "q", mode.bands[i].q_factor);
        cJSON_AddNumberToObject(band, "p", mode.bands[i].phase);
        cJSON_AddItemToArray(list, band);
    }
    
    cJSON_AddItemToObject(data, "list", list);
    
    ret = gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_AUDIO_CONTROL, "getEQFilter", 0, "success", data);
    
    cJSON_Delete(data);
    return ret;
}

esp_err_t gmc_eq_handle_set_filter(const void *data_json)
{
    if (data_json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    const cJSON *json = (const cJSON *)data_json;
    cJSON *eid = cJSON_GetObjectItem(json, "eid");
    cJSON *value = cJSON_GetObjectItem(json, "value");
    
    if (!cJSON_IsNumber(eid) || value == NULL) {
        esp_err_t ret = gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_AUDIO_CONTROL, "setEQFilter", -1, "Invalid parameters", NULL);
        return ret;
    }
    
    uint8_t eq_id = (uint8_t)eid->valueint;
    
    cJSON *bid = cJSON_GetObjectItem(value, "bid");
    cJSON *freq = cJSON_GetObjectItem(value, "f");
    cJSON *gain = cJSON_GetObjectItem(value, "g");
    cJSON *q = cJSON_GetObjectItem(value, "q");
    cJSON *phase = cJSON_GetObjectItem(value, "p");
    
    if (!cJSON_IsNumber(bid) || !cJSON_IsNumber(freq) || 
        !cJSON_IsNumber(gain) || !cJSON_IsNumber(q) || !cJSON_IsNumber(phase)) {
        esp_err_t ret = gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_AUDIO_CONTROL, "setEQFilter", -1, "Invalid filter parameters", NULL);
        return ret;
    }
    
    eq_band_t band = {
        .band_id = (uint8_t)bid->valueint,
        .frequency = (float)freq->valuedouble,
        .gain = (float)gain->valuedouble,
        .q_factor = (float)q->valuedouble,
        .phase = (uint8_t)phase->valueint
    };
    
    esp_err_t ret = gmc_eq_set_filter(eq_id, &band);
    
    if (ret == ESP_OK) {
        gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_AUDIO_CONTROL, "setEQFilter", 0, "success", NULL);
    } else {
        gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_AUDIO_CONTROL, "setEQFilter", -1, "Failed to set filter", NULL);
    }
    
    return ret;
}

esp_err_t gmc_eq_notify_mode_changed(uint8_t mode_id)
{
    cJSON *data = cJSON_CreateNumber(mode_id);
    
    esp_err_t ret = gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_AUDIO_CONTROL,
                                     "eqModeChanged", 0, "notify", data);
    
    cJSON_Delete(data);
    return ret;
}
