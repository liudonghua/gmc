#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include "gmc_broadcast.h"
#include "gmc_protocol.h"

static const char *TAG = "gmc_broadcast";
static const char *NVS_NAMESPACE = "broadcast";
static const char *NVS_KEY_MODE = "mode";
static const char *NVS_KEY_MAIN_GID = "main_gid";
static const char *NVS_KEY_MAIN_PREEMPT = "main_preempt";
static const char *NVS_KEY_SUB_GID = "sub_gid";

static uint8_t current_mode = BROADCAST_MODE_DEFAULT;
static main_mode_config_t main_config;
static sub_mode_config_t sub_config;
static SemaphoreHandle_t broadcast_mutex = NULL;

// Callbacks
static broadcast_mode_change_callback_t mode_callback = NULL;
static broadcast_main_config_change_callback_t main_config_callback = NULL;
static broadcast_sub_group_change_callback_t sub_group_callback = NULL;

/**
 * @brief Generate unique group ID for main mode
 */
static void generate_main_group_id(char *gid, size_t len)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(gid, len, "GRP_%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/**
 * @brief Load settings from NVS
 */
static void load_settings_from_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS, using defaults");
        return;
    }
    
    // Load mode
    uint8_t mode = BROADCAST_MODE_DEFAULT;
    if (nvs_get_u8(nvs_handle, NVS_KEY_MODE, &mode) == ESP_OK) {
        if (mode <= BROADCAST_MODE_MAX) {
            current_mode = mode;
        }
    }
    
    // Load main config
    size_t len = MAX_GROUP_ID_LEN;
    nvs_get_str(nvs_handle, NVS_KEY_MAIN_GID, main_config.group_id, &len);
    
    uint8_t preempt = BROADCAST_NON_PREEMPTIVE;
    if (nvs_get_u8(nvs_handle, NVS_KEY_MAIN_PREEMPT, &preempt) == ESP_OK) {
        main_config.preemptive = preempt;
    }
    
    // Load sub config
    len = MAX_GROUP_ID_LEN;
    nvs_get_str(nvs_handle, NVS_KEY_SUB_GID, sub_config.current_group_id, &len);
    
    nvs_close(nvs_handle);
}

/**
 * @brief Save setting to NVS
 */
static esp_err_t save_to_nvs(const char *key, const void *value, bool is_u8)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing");
        return ret;
    }
    
    if (is_u8) {
        ret = nvs_set_u8(nvs_handle, key, *(const uint8_t *)value);
    } else {
        ret = nvs_set_str(nvs_handle, key, (const char *)value);
    }
    
    if (ret == ESP_OK) {
        ret = nvs_commit(nvs_handle);
    }
    
    nvs_close(nvs_handle);
    return ret;
}

esp_err_t gmc_broadcast_init(void)
{
    broadcast_mutex = xSemaphoreCreateMutex();
    if (broadcast_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize main config
    memset(&main_config, 0, sizeof(main_config));
    generate_main_group_id(main_config.group_id, MAX_GROUP_ID_LEN);
    main_config.preemptive = BROADCAST_NON_PREEMPTIVE;
    
    // Initialize sub config
    memset(&sub_config, 0, sizeof(sub_config));
    sub_config.group_count = 0;
    
    // Load from NVS
    load_settings_from_nvs();
    
    ESP_LOGI(TAG, "Broadcast module initialized, mode: %d, main GID: %s",
             current_mode, main_config.group_id);
    
    return ESP_OK;
}

void gmc_broadcast_register_mode_callback(broadcast_mode_change_callback_t callback)
{
    mode_callback = callback;
}

void gmc_broadcast_register_main_config_callback(broadcast_main_config_change_callback_t callback)
{
    main_config_callback = callback;
}

void gmc_broadcast_register_sub_group_callback(broadcast_sub_group_change_callback_t callback)
{
    sub_group_callback = callback;
}

esp_err_t gmc_broadcast_get_mode(uint8_t *mode)
{
    if (mode == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(broadcast_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    *mode = current_mode;
    
    xSemaphoreGive(broadcast_mutex);
    return ESP_OK;
}

esp_err_t gmc_broadcast_set_mode(uint8_t mode)
{
    if (mode > BROADCAST_MODE_MAX) {
        ESP_LOGE(TAG, "Invalid mode: %d", mode);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(broadcast_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    uint8_t old_mode = current_mode;
    current_mode = mode;
    
    xSemaphoreGive(broadcast_mutex);
    
    // Save to NVS
    save_to_nvs(NVS_KEY_MODE, &mode, true);
    
    ESP_LOGI(TAG, "Broadcast mode changed: %d -> %d", old_mode, mode);
    
    // Call callback
    if (mode_callback != NULL) {
        mode_callback(mode);
    }
    
    return ESP_OK;
}

esp_err_t gmc_broadcast_get_main_config(main_mode_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(broadcast_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    *config = main_config;
    
    xSemaphoreGive(broadcast_mutex);
    return ESP_OK;
}

esp_err_t gmc_broadcast_set_main_preemptive(uint8_t preemptive)
{
    if (preemptive > 1) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(broadcast_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    main_config.preemptive = preemptive;
    
    xSemaphoreGive(broadcast_mutex);
    
    // Save to NVS
    save_to_nvs(NVS_KEY_MAIN_PREEMPT, &preemptive, true);
    
    ESP_LOGI(TAG, "Main mode preemptive set to: %d", preemptive);
    
    // Call callback
    if (main_config_callback != NULL) {
        main_config_callback(preemptive);
    }
    
    return ESP_OK;
}

esp_err_t gmc_broadcast_get_sub_config(sub_mode_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(broadcast_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    *config = sub_config;
    
    xSemaphoreGive(broadcast_mutex);
    return ESP_OK;
}

esp_err_t gmc_broadcast_change_sub_group(const char *group_id)
{
    if (group_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(broadcast_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    strncpy(sub_config.current_group_id, group_id, MAX_GROUP_ID_LEN - 1);
    sub_config.current_group_id[MAX_GROUP_ID_LEN - 1] = '\0';
    
    xSemaphoreGive(broadcast_mutex);
    
    // Save to NVS
    save_to_nvs(NVS_KEY_SUB_GID, group_id, false);
    
    ESP_LOGI(TAG, "Sub mode group changed to: %s", group_id);
    
    // Call callback
    if (sub_group_callback != NULL) {
        sub_group_callback(group_id);
    }
    
    return ESP_OK;
}

esp_err_t gmc_broadcast_add_group(const char *group_id)
{
    if (group_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(broadcast_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    // Check if group already exists
    for (int i = 0; i < sub_config.group_count; i++) {
        if (strcmp(sub_config.group_list[i].id, group_id) == 0) {
            xSemaphoreGive(broadcast_mutex);
            return ESP_OK;  // Already exists
        }
    }
    
    // Add new group
    if (sub_config.group_count < MAX_GROUP_LIST_SIZE) {
        strncpy(sub_config.group_list[sub_config.group_count].id, group_id, MAX_GROUP_ID_LEN - 1);
        sub_config.group_list[sub_config.group_count].id[MAX_GROUP_ID_LEN - 1] = '\0';
        sub_config.group_count++;
        ESP_LOGI(TAG, "Added group: %s (total: %d)", group_id, sub_config.group_count);
    } else {
        ESP_LOGW(TAG, "Group list full, cannot add: %s", group_id);
        xSemaphoreGive(broadcast_mutex);
        return ESP_ERR_NO_MEM;
    }
    
    xSemaphoreGive(broadcast_mutex);
    return ESP_OK;
}

esp_err_t gmc_broadcast_remove_group(const char *group_id)
{
    if (group_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(broadcast_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    // Find and remove group
    for (int i = 0; i < sub_config.group_count; i++) {
        if (strcmp(sub_config.group_list[i].id, group_id) == 0) {
            // Shift remaining groups
            for (int j = i; j < sub_config.group_count - 1; j++) {
                sub_config.group_list[j] = sub_config.group_list[j + 1];
            }
            sub_config.group_count--;
            ESP_LOGI(TAG, "Removed group: %s (remaining: %d)", group_id, sub_config.group_count);
            xSemaphoreGive(broadcast_mutex);
            return ESP_OK;
        }
    }
    
    xSemaphoreGive(broadcast_mutex);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t gmc_broadcast_handle_get_mode(void)
{
    uint8_t mode = 0;
    esp_err_t ret = gmc_broadcast_get_mode(&mode);
    
    if (ret == ESP_OK) {
        cJSON *data = cJSON_CreateNumber(mode);
        gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_SHARE_AUDIO, "getMode", 0, "success", data);
        cJSON_Delete(data);
    } else {
        gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_SHARE_AUDIO, "getMode", -1, "Failed to get mode", NULL);
    }
    
    return ret;
}

esp_err_t gmc_broadcast_handle_set_mode(uint8_t mode)
{
    esp_err_t ret = gmc_broadcast_set_mode(mode);
    
    if (ret == ESP_OK) {
        gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_SHARE_AUDIO, "setMode", 0, "success", NULL);
        gmc_broadcast_notify_mode_changed(mode);
    } else {
        gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_SHARE_AUDIO, "setMode", -1, "Invalid mode", NULL);
    }
    
    return ret;
}

esp_err_t gmc_broadcast_handle_get_main_config(void)
{
    main_mode_config_t config;
    esp_err_t ret = gmc_broadcast_get_main_config(&config);
    
    if (ret != ESP_OK) {
        ret = gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_SHARE_AUDIO, "getMainModeCfg", -1, "Failed to get config", NULL);
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "gid", config.group_id);
    cJSON_AddNumberToObject(data, "preemtive", config.preemptive);
    
    ret = gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_SHARE_AUDIO, "getMainModeCfg", 0, "success", data);
    
    cJSON_Delete(data);
    return ret;
}

esp_err_t gmc_broadcast_handle_set_main_config(uint8_t preemptive)
{
    esp_err_t ret = gmc_broadcast_set_main_preemptive(preemptive);
    
    if (ret == ESP_OK) {
        gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_SHARE_AUDIO, "setMainModeCfg", 0, "success", NULL);
        gmc_broadcast_notify_main_config_changed(preemptive);
    } else {
        gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_SHARE_AUDIO, "setMainModeCfg", -1, "Failed to set config", NULL);
    }
    
    return ret;
}

esp_err_t gmc_broadcast_handle_get_sub_config(void)
{
    sub_mode_config_t config;
    esp_err_t ret = gmc_broadcast_get_sub_config(&config);
    
    if (ret != ESP_OK) {
        ret = gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_SHARE_AUDIO, "getSubModeCfg", -1, "Failed to get config", NULL);
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "gid", config.current_group_id);
    
    cJSON *list = cJSON_CreateArray();
    for (int i = 0; i < config.group_count; i++) {
        cJSON *group = cJSON_CreateObject();
        cJSON_AddStringToObject(group, "id", config.group_list[i].id);
        cJSON_AddItemToArray(list, group);
    }
    cJSON_AddItemToObject(data, "list", list);
    
    ret = gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_SHARE_AUDIO, "getSubModeCfg", 0, "success", data);
    
    cJSON_Delete(data);
    return ret;
}

esp_err_t gmc_broadcast_handle_change_sub_group(const char *group_id)
{
    if (group_id == NULL) {
        esp_err_t ret = gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_SHARE_AUDIO, "changeSubModeGroup", -1, "Invalid group ID", NULL);
        return ret;
    }
    
    esp_err_t ret = gmc_broadcast_change_sub_group(group_id);
    
    if (ret == ESP_OK) {
        gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_SHARE_AUDIO, "changeSubModeGroup", 0, "success", NULL);
        gmc_broadcast_notify_sub_group_changed(group_id);
    } else {
        gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_SHARE_AUDIO, "changeSubModeGroup", -1, "Failed to change group", NULL);
    }
    
    return ret;
}

esp_err_t gmc_broadcast_notify_mode_changed(uint8_t mode)
{
    cJSON *data = cJSON_CreateNumber(mode);
    
    esp_err_t ret = gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_SHARE_AUDIO,
                                     "modeChanged", 0, "notify", data);
    
    cJSON_Delete(data);
    return ret;
}

esp_err_t gmc_broadcast_notify_main_config_changed(uint8_t preemptive)
{
    cJSON *data = cJSON_CreateNumber(preemptive);
    
    esp_err_t ret = gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_SHARE_AUDIO,
                                     "mainModeCfgChanged", 0, "notify", data);
    
    cJSON_Delete(data);
    return ret;
}

esp_err_t gmc_broadcast_notify_sub_group_changed(const char *group_id)
{
    cJSON *data = cJSON_CreateString(group_id);
    
    esp_err_t ret = gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_SHARE_AUDIO,
                                     "subModeGroupChanged", 0, "notify", data);
    
    cJSON_Delete(data);
    return ret;
}

esp_err_t gmc_broadcast_notify_group_list_changed(void)
{
    sub_mode_config_t config;
    esp_err_t ret = gmc_broadcast_get_sub_config(&config);
    
    if (ret != ESP_OK) {
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON *list = cJSON_CreateArray();
    
    for (int i = 0; i < config.group_count; i++) {
        cJSON *group = cJSON_CreateObject();
        cJSON_AddStringToObject(group, "id", config.group_list[i].id);
        cJSON_AddItemToArray(list, group);
    }
    
    cJSON_AddItemToObject(data, "list", list);
    
    ret = gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_SHARE_AUDIO,
                          "groupListChanged", 0, "notify", data);
    
    cJSON_Delete(data);
    return ret;
}
