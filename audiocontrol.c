#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "cJSON.h"
#include "gmc_audiocontrol.h"
#include "gmc_protocol.h"

static const char *TAG = "gmc_audio";

#define MAX_AUDIO_CHANNELS 15

static audio_channel_t audio_channels[MAX_AUDIO_CHANNELS];
static char audio_channel_map[16] = "2.1.0";
static uint8_t num_active_channels = 0;
static SemaphoreHandle_t audio_mutex = NULL;
static audio_change_callback_t audio_callback = NULL;

// Initialize default channel configuration for 2.1.0 system
static void init_default_channels(void)
{
    memset(audio_channels, 0, sizeof(audio_channels));
    
    // Channel 0: All channels
    audio_channels[0] = (audio_channel_t){
        .id = AUDIO_CH_ALL,
        .changeable = 1,
        .volume = AUDIO_VOLUME_DEFAULT,
        .min_volume = AUDIO_VOLUME_MIN,
        .max_volume = AUDIO_VOLUME_MAX,
        .status = 1,
        .muted = 1
    };
    
    // Channel 1: Main Left
    audio_channels[1] = (audio_channel_t){
        .id = AUDIO_CH_MAIN_LEFT,
        .changeable = 1,
        .volume = AUDIO_VOLUME_DEFAULT,
        .min_volume = AUDIO_VOLUME_MIN,
        .max_volume = AUDIO_VOLUME_MAX,
        .status = 1,
        .muted = 1
    };
    
    // Channel 3: Main Right
    audio_channels[3] = (audio_channel_t){
        .id = AUDIO_CH_MAIN_RIGHT,
        .changeable = 1,
        .volume = AUDIO_VOLUME_DEFAULT,
        .min_volume = AUDIO_VOLUME_MIN,
        .max_volume = AUDIO_VOLUME_MAX,
        .status = 1,
        .muted = 1
    };
    
    // Channel 10: Subwoofer
    audio_channels[10] = (audio_channel_t){
        .id = AUDIO_CH_SUBWOOFER,
        .changeable = 1,
        .volume = AUDIO_VOLUME_DEFAULT,
        .min_volume = AUDIO_VOLUME_MIN,
        .max_volume = AUDIO_VOLUME_MAX,
        .status = 1,
        .muted = 1
    };
    
    num_active_channels = 4;
}

esp_err_t gmc_audio_init(const char *channel_map)
{
    if (channel_map != NULL) {
        strncpy(audio_channel_map, channel_map, sizeof(audio_channel_map) - 1);
    }
    
    audio_mutex = xSemaphoreCreateMutex();
    if (audio_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    init_default_channels();
    
    ESP_LOGI(TAG, "Audio control initialized with map: %s", audio_channel_map);
    return ESP_OK;
}

void gmc_audio_register_callback(audio_change_callback_t callback)
{
    audio_callback = callback;
}

esp_err_t gmc_audio_get_channel(uint8_t channel_id, audio_channel_t *channel)
{
    if (channel == NULL || channel_id >= MAX_AUDIO_CHANNELS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(audio_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    *channel = audio_channels[channel_id];
    
    xSemaphoreGive(audio_mutex);
    return ESP_OK;
}

esp_err_t gmc_audio_set_volume(uint8_t channel_id, uint8_t volume)
{
    if (channel_id >= MAX_AUDIO_CHANNELS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (volume > AUDIO_VOLUME_MAX) {
        volume = AUDIO_VOLUME_MAX;
    }
    
    if (xSemaphoreTake(audio_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    if (audio_channels[channel_id].changeable == 0) {
        xSemaphoreGive(audio_mutex);
        ESP_LOGW(TAG, "Channel %d is not changeable", channel_id);
        return ESP_ERR_INVALID_STATE;
    }
    
    audio_channels[channel_id].volume = volume;
    
    // If setting all channels, update all active channels
    if (channel_id == AUDIO_CH_ALL) {
        for (int i = 1; i < MAX_AUDIO_CHANNELS; i++) {
            if (audio_channels[i].status == 1 && audio_channels[i].changeable == 1) {
                audio_channels[i].volume = volume;
            }
        }
    }
    
    xSemaphoreGive(audio_mutex);
    
    ESP_LOGI(TAG, "Set volume: channel=%d, volume=%d", channel_id, volume);
    
    // Call callback if registered
    if (audio_callback != NULL) {
        audio_callback(channel_id, volume, audio_channels[channel_id].muted);
    }
    
    return ESP_OK;
}

esp_err_t gmc_audio_get_volume(uint8_t channel_id, uint8_t *volume)
{
    if (volume == NULL || channel_id >= MAX_AUDIO_CHANNELS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(audio_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    *volume = audio_channels[channel_id].volume;
    
    xSemaphoreGive(audio_mutex);
    return ESP_OK;
}

esp_err_t gmc_audio_set_mute(uint8_t channel_id, uint8_t muted)
{
    if (channel_id >= MAX_AUDIO_CHANNELS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(audio_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    audio_channels[channel_id].muted = muted;
    
    // If setting all channels, update all active channels
    if (channel_id == AUDIO_CH_ALL) {
        for (int i = 1; i < MAX_AUDIO_CHANNELS; i++) {
            if (audio_channels[i].status == 1) {
                audio_channels[i].muted = muted;
            }
        }
    }
    
    xSemaphoreGive(audio_mutex);
    
    ESP_LOGI(TAG, "Set mute: channel=%d, muted=%d", channel_id, muted);
    
    // Call callback if registered
    if (audio_callback != NULL) {
        audio_callback(channel_id, audio_channels[channel_id].volume, muted);
    }
    
    return ESP_OK;
}

esp_err_t gmc_audio_get_mute(uint8_t channel_id, uint8_t *muted)
{
    if (muted == NULL || channel_id >= MAX_AUDIO_CHANNELS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(audio_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    *muted = audio_channels[channel_id].muted;
    
    xSemaphoreGive(audio_mutex);
    return ESP_OK;
}

esp_err_t gmc_audio_handle_get_channel_list(void)
{
    cJSON *data = cJSON_CreateObject();
    if (data == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    cJSON_AddStringToObject(data, "map", audio_channel_map);
    
    cJSON *list = cJSON_CreateArray();
    if (list == NULL) {
        cJSON_Delete(data);
        return ESP_ERR_NO_MEM;
    }
    
    if (xSemaphoreTake(audio_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        cJSON_Delete(list);
        cJSON_Delete(data);
        return ESP_ERR_TIMEOUT;
    }
    
    // Add all active channels to list
    for (int i = 0; i < MAX_AUDIO_CHANNELS; i++) {
        if (audio_channels[i].status == 1 || i == 0) {
            cJSON *ch = cJSON_CreateObject();
            cJSON_AddNumberToObject(ch, "id", audio_channels[i].id);
            cJSON_AddNumberToObject(ch, "chg", audio_channels[i].changeable);
            cJSON_AddNumberToObject(ch, "val", audio_channels[i].volume);
            cJSON_AddNumberToObject(ch, "max", audio_channels[i].max_volume);
            cJSON_AddNumberToObject(ch, "min", audio_channels[i].min_volume);
            cJSON_AddNumberToObject(ch, "sta", audio_channels[i].status);
            cJSON_AddItemToArray(list, ch);
        }
    }
    
    xSemaphoreGive(audio_mutex);
    
    cJSON_AddItemToObject(data, "list", list);
    cJSON_AddStringToObject(data, "map", audio_channel_map);
    
    esp_err_t ret = gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_AUDIO_CONTROL, "getAudioChannelList", 0, "success", data);
    
    cJSON_Delete(data);
    return ret;
}

esp_err_t gmc_audio_handle_set_volume(const void *data_json)
{
    if (data_json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    const cJSON *json = (const cJSON *)data_json;
    cJSON *id = cJSON_GetObjectItem(json, "id");
    cJSON *value = cJSON_GetObjectItem(json, "value");
    
    if (!cJSON_IsNumber(id) || !cJSON_IsNumber(value)) {
        esp_err_t ret = gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_AUDIO_CONTROL, "setAudioChannelVolume", -1, "Invalid parameters", NULL);
        return ret;
    }
    
    uint8_t channel_id = (uint8_t)id->valueint;
    uint8_t volume = (uint8_t)value->valueint;
    
    esp_err_t ret = gmc_audio_set_volume(channel_id, volume);
    
    if (ret == ESP_OK) {
        gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_AUDIO_CONTROL, "setAudioChannelVolume", 0, "success", NULL);
        // Send notification
        gmc_audio_notify_volume_changed(channel_id, volume);
    } else {
        gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_AUDIO_CONTROL, "setAudioChannelVolume", -1, "Failed to set volume", NULL);
    }
    
    return ret;
}

esp_err_t gmc_audio_handle_get_volume(uint8_t channel_id)
{
    uint8_t volume = 0;
    esp_err_t ret = gmc_audio_get_volume(channel_id, &volume);
    
    if (ret == ESP_OK) {
        cJSON *data = cJSON_CreateObject();
        cJSON_AddNumberToObject(data, "id", channel_id);
        cJSON_AddNumberToObject(data, "value", volume);
        
        gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_AUDIO_CONTROL, "getAudioChannelVolume", 0, "success", data);
        cJSON_Delete(data);
    } else {
        gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_AUDIO_CONTROL, "getAudioChannelVolume", -1, "Failed to get volume", NULL);
    }
    
    return ret;
}

esp_err_t gmc_audio_handle_set_mute(const void *data_json)
{
    if (data_json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    const cJSON *json = (const cJSON *)data_json;
    cJSON *id = cJSON_GetObjectItem(json, "id");
    cJSON *value = cJSON_GetObjectItem(json, "value");
    
    if (!cJSON_IsNumber(id) || !cJSON_IsNumber(value)) {
        esp_err_t ret = gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_AUDIO_CONTROL, "setMute", -1, "Invalid parameters", NULL);
        return ret;
    }
    
    uint8_t channel_id = (uint8_t)id->valueint;
    uint8_t muted = (uint8_t)value->valueint;
    
    esp_err_t ret = gmc_audio_set_mute(channel_id, muted);
    
    if (ret == ESP_OK) {
        gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_AUDIO_CONTROL, "setMute", 0, "success", NULL);
        // Send notification
        gmc_audio_notify_mute_changed(channel_id, muted);
    } else {
        gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_AUDIO_CONTROL, "setMute", -1, "Failed to set mute", NULL);
    }
    
    return ret;
}

esp_err_t gmc_audio_handle_get_mute(uint8_t channel_id)
{
    uint8_t muted = 0;
    esp_err_t ret = gmc_audio_get_mute(channel_id, &muted);
    
    if (ret == ESP_OK) {
        cJSON *data = cJSON_CreateObject();
        cJSON_AddNumberToObject(data, "id", channel_id);
        cJSON_AddNumberToObject(data, "value", muted);

        gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_AUDIO_CONTROL, "getMute", 0, "success", data);
        cJSON_Delete(data);
    } else {
        gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_AUDIO_CONTROL, "getMute", -1, "Failed to get mute", NULL);
    }
    
    return ret;
}

esp_err_t gmc_audio_notify_volume_changed(uint8_t channel_id, uint8_t volume)
{
    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "id", channel_id);
    cJSON_AddNumberToObject(data, "value", volume);
    
    esp_err_t ret = gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_AUDIO_CONTROL,
                                     "audioChannelVolumeChanged", 0, "notify", data);
    
    cJSON_Delete(data);
    return ret;
}

esp_err_t gmc_audio_notify_mute_changed(uint8_t channel_id, uint8_t muted)
{
    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "id", channel_id);
    cJSON_AddNumberToObject(data, "value", muted);
    
    esp_err_t ret = gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_AUDIO_CONTROL,
                                     "muteChanged", 0, "notify", data);
    
    cJSON_Delete(data);
    return ret;
}
