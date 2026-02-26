#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "cJSON.h"
#include "gmc_user.h"
#include "gmc_protocol.h"

static const char *TAG = "gmc_user";
static uint8_t current_user_mode = USER_MODE_DEFAULT;
static uint8_t previous_user_mode = USER_MODE_DEFAULT;
static SemaphoreHandle_t user_mode_mutex = NULL;
static user_mode_change_callback_t mode_change_callback = NULL;

// User mode information table
static const user_mode_info_t user_mode_table[] = {
    {
        .mode_id = USER_MODE_COMPANION,
        .name = "Emotional Companion",
        .description = "Wake up AI agent for emotional conversation and communication",
        .client_settable = true
    },
    {
        .mode_id = USER_MODE_MEETING,
        .name = "Meeting Recorder",
        .description = "Real-time speech recognition during meetings, generating complete records and key summaries",
        .client_settable = true
    },
    {
        .mode_id = USER_MODE_TRANSLATION,
        .name = "Language Translation",
        .description = "Support translation between 6 languages: English, Chinese, Japanese, Spanish, French and German",
        .client_settable = true
    },
    {
        .mode_id = USER_MODE_UPGRADE,
        .name = "OTA Upgrade",
        .description = "Device is performing OTA upgrade and other operations are not allowed",
        .client_settable = false
    }
};

#define USER_MODE_TABLE_SIZE (sizeof(user_mode_table) / sizeof(user_mode_info_t))

esp_err_t gmc_user_init(void)
{
    user_mode_mutex = xSemaphoreCreateMutex();
    if (user_mode_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // User mode is not persisted. Always reset to default after reboot.
    current_user_mode = USER_MODE_DEFAULT;
    previous_user_mode = USER_MODE_DEFAULT;
    
    ESP_LOGI(TAG, "User mode module initialized, current mode: %d (%s)",
             current_user_mode, user_mode_table[current_user_mode].name);
    
    return ESP_OK;
}

void gmc_user_register_callback(user_mode_change_callback_t callback)
{
    mode_change_callback = callback;
}

esp_err_t gmc_user_get_mode(uint8_t *mode_id)
{
    if (mode_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(user_mode_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    *mode_id = current_user_mode;
    
    xSemaphoreGive(user_mode_mutex);
    return ESP_OK;
}

esp_err_t gmc_user_set_mode(uint8_t mode_id, bool internal)
{
    // Validate mode
    if (!internal && mode_id > USER_MODE_MAX) {
        ESP_LOGE(TAG, "Invalid user mode for client: %d", mode_id);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (internal && mode_id > USER_MODE_UPGRADE_INTERNAL) {
        ESP_LOGE(TAG, "Invalid user mode: %d", mode_id);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(user_mode_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    uint8_t old_mode = current_user_mode;
    current_user_mode = mode_id;
    
    xSemaphoreGive(user_mode_mutex);
    
    ESP_LOGI(TAG, "User mode changed: %d (%s) -> %d (%s)",
             old_mode, user_mode_table[old_mode].name,
             mode_id, user_mode_table[mode_id].name);
    
    // Call callback if registered
    if (mode_change_callback != NULL) {
        mode_change_callback(old_mode, mode_id);
    }
    
    return ESP_OK;
}

esp_err_t gmc_user_get_mode_info(uint8_t mode_id, user_mode_info_t *info)
{
    if (info == NULL || mode_id >= USER_MODE_TABLE_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    
    *info = user_mode_table[mode_id];
    return ESP_OK;
}

bool gmc_user_is_upgrade_mode(void)
{
    uint8_t mode = 0;
    if (gmc_user_get_mode(&mode) == ESP_OK) {
        return (mode == USER_MODE_UPGRADE);
    }
    return false;
}

esp_err_t gmc_user_enter_upgrade_mode(void)
{
    ESP_LOGI(TAG, "Entering upgrade mode");

    if (xSemaphoreTake(user_mode_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (current_user_mode != USER_MODE_UPGRADE) {
        previous_user_mode = current_user_mode;
    }
    xSemaphoreGive(user_mode_mutex);

    return gmc_user_set_mode(USER_MODE_UPGRADE, true);
}

esp_err_t gmc_user_exit_upgrade_mode(void)
{
    ESP_LOGI(TAG, "Exiting upgrade mode");

    uint8_t mode_to_restore = USER_MODE_DEFAULT;
    if (xSemaphoreTake(user_mode_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    mode_to_restore = previous_user_mode;
    xSemaphoreGive(user_mode_mutex);

    if (mode_to_restore > USER_MODE_MAX) {
        mode_to_restore = USER_MODE_DEFAULT;
    }

    return gmc_user_set_mode(mode_to_restore, true);
}

esp_err_t gmc_user_handle_get_mode_list(void)
{
    // Return list of client-settable modes [0, 1, 2]
    cJSON *data = cJSON_CreateArray();
    if (data == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    for (int i = 0; i <= USER_MODE_MAX; i++) {
        if (user_mode_table[i].client_settable) {
            cJSON *mode_obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(mode_obj, "type", i);
            cJSON_AddStringToObject(mode_obj, "name", user_mode_table[i].name);
            cJSON_AddItemToArray(data, mode_obj);
        }
    }
    
    esp_err_t ret = gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_SETTINGS, "getUserModeList", 0, "success", data);
    
    cJSON_Delete(data);
    return ret;
}

esp_err_t gmc_user_handle_set_mode(uint8_t mode_id)
{
    // Check if mode is client settable
    if (mode_id > USER_MODE_MAX || !user_mode_table[mode_id].client_settable) {
        ESP_LOGW(TAG, "Client attempted to set non-settable mode: %d", mode_id);
        esp_err_t ret = gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_SETTINGS, "setUserMode", -1, "Mode not settable by client", NULL);
        return ret;
    }
    
    esp_err_t ret = gmc_user_set_mode(mode_id, false);
    
    if (ret == ESP_OK) {
        gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_SETTINGS, "setUserMode", 0, "success", NULL);
        // Send notification
        gmc_user_notify_mode_changed(mode_id);
    } else {
        gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_SETTINGS, "setUserMode", -1, "Failed to set user mode", NULL);
    }
    
    return ret;
}

esp_err_t gmc_user_handle_get_mode(void)
{
    uint8_t mode_id = 0;
    esp_err_t ret = gmc_user_get_mode(&mode_id);
    
    if (ret == ESP_OK) {
        cJSON *data = cJSON_CreateNumber(mode_id);
        gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_SETTINGS, "getUserMode", 0, "success", data);
        cJSON_Delete(data);
    } else {
        gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_SETTINGS, "getUserMode", -1, "Failed to get user mode", NULL);
    }
    
    return ret;
}

esp_err_t gmc_user_notify_mode_changed(uint8_t mode_id)
{
    cJSON *data = cJSON_CreateNumber(mode_id);
    
    esp_err_t ret = gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_SETTINGS,
                                     "userModeChanged", 0, "notify", data);
    
    cJSON_Delete(data);
    return ret;
}
