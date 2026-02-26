#include "gmc.h"
#include "gmc_protocol.h"
#include "gmc_audiocontrol.h"
#include "gmc_bt.h"
#include "gmc_settings.h"
#include "gmc_device.h"
#include "gmc_user.h"
#include "gmc_broadcast.h"
#include "gmc_email.h"
#include "gmc_recorder.h"
#include "gmc_eq.h"
#include <esp_log.h>
#include <string.h>

#define TAG "GMC"

// ==================== 播放控制处理函数 ====================
static esp_err_t handle_get_input_source_list(const cJSON *json)
{
    ESP_LOGI(TAG, "Processing: getInputSourceList");
    // TODO: Implement input source list and call gmc_send_input_source_list
    ESP_LOGW(TAG, "getInputSourceList not implemented yet");
    return ESP_OK;
}

static esp_err_t handle_get_current_input_source(const cJSON *json)
{
    ESP_LOGI(TAG, "Processing: getCurrentInputSource");
    // TODO: Implement and call gmc_send_current_input_source
    ESP_LOGW(TAG, "getCurrentInputSource not implemented yet");
    return ESP_OK;
}

static esp_err_t handle_set_current_input_source(const cJSON *json)
{
    const cJSON *data = cJSON_GetObjectItem(json, "data");
    if (!data || !cJSON_IsNumber(data))
    {
        ESP_LOGE(TAG, "Invalid data for setCurrentInputSource");
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t source = (uint8_t)data->valueint;
    ESP_LOGI(TAG, "Processing: setCurrentInputSource = %d", source);
    // TODO: Implement via gmc_set_input_source when available
    ESP_LOGW(TAG, "setCurrentInputSource not implemented yet");
    return ESP_OK;
}

static esp_err_t handle_set_play(const cJSON *json)
{
    const cJSON *data = cJSON_GetObjectItem(json, "data");
    if (!data || !cJSON_IsNumber(data))
    {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t play_state = (uint8_t)data->valueint;
    ESP_LOGI(TAG, "Processing: setPlay = %d", play_state);
    // TODO: Implement play control
    ESP_LOGW(TAG, "setPlay not implemented yet");
    return ESP_OK;
}

static esp_err_t handle_get_play_status(const cJSON *json)
{
    ESP_LOGI(TAG, "Processing: getPlayStatus");
    // TODO: Implement play status retrieval
    ESP_LOGW(TAG, "getPlayStatus not implemented yet");
    return ESP_OK;
}

// ==================== 音频控制处理函数 ====================
static esp_err_t handle_get_audio_channel_list(const cJSON *json)
{
    ESP_LOGI(TAG, "Processing: getAudioChannelList");
    return gmc_audio_handle_get_channel_list();
}

static esp_err_t handle_set_audio_channel_volume(const cJSON *json)
{
    ESP_LOGI(TAG, "Processing: setAudioChannelVolume");
    const cJSON *data = cJSON_GetObjectItem(json, "data");
    return gmc_audio_handle_set_volume(data);
}

static esp_err_t handle_get_audio_channel_volume(const cJSON *json)
{
    const cJSON *data = cJSON_GetObjectItem(json, "data");
    if (!data || !cJSON_IsNumber(data))
    {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t channel_id = (uint8_t)data->valueint;
    ESP_LOGI(TAG, "Processing: getAudioChannelVolume [%d]", channel_id);
    return gmc_audio_handle_get_volume(channel_id);
}

static esp_err_t handle_set_mute(const cJSON *json)
{
    ESP_LOGI(TAG, "Processing: setMute");
    const cJSON *data = cJSON_GetObjectItem(json, "data");
    return gmc_audio_handle_set_mute(data);
}

static esp_err_t handle_get_mute(const cJSON *json)
{
    const cJSON *data = cJSON_GetObjectItem(json, "data");
    if (!data || !cJSON_IsNumber(data))
    {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t channel_id = (uint8_t)data->valueint;
    ESP_LOGI(TAG, "Processing: getMute [%d]", channel_id);
    return gmc_audio_handle_get_mute(channel_id);
}

static esp_err_t handle_get_eq_mode_list(const cJSON *json)
{
    ESP_LOGI(TAG, "Processing: getEQModeList");
    return gmc_eq_handle_get_mode_list();
}

static esp_err_t handle_set_eq_mode(const cJSON *json)
{
    const cJSON *data = cJSON_GetObjectItem(json, "data");
    if (!data || !cJSON_IsNumber(data))
    {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t mode = (uint8_t)data->valueint;
    ESP_LOGI(TAG, "Processing: setEQMode = %d", mode);
    return gmc_eq_handle_set_mode(mode);
}

static esp_err_t handle_get_eq_mode(const cJSON *json)
{
    ESP_LOGI(TAG, "Processing: getEQMode");
    return gmc_eq_handle_get_mode();
}

static esp_err_t handle_get_eq_filter(const cJSON *json)
{
    const cJSON *data = cJSON_GetObjectItem(json, "data");
    if (!data || !cJSON_IsNumber(data))
    {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t eq_id = (uint8_t)data->valueint;
    ESP_LOGI(TAG, "Processing: getEQFilter [%d]", eq_id);
    return gmc_eq_handle_get_filter(eq_id);
}

static esp_err_t handle_set_eq_filter(const cJSON *json)
{
    ESP_LOGI(TAG, "Processing: setEQFilter");
    const cJSON *data = cJSON_GetObjectItem(json, "data");
    return gmc_eq_handle_set_filter(data);
}

// ==================== 设置处理函数 ====================
static esp_err_t handle_set_device_name(const cJSON *json)
{
    const cJSON *data = cJSON_GetObjectItem(json, "data");
    if (!data || !cJSON_IsString(data))
    {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "Processing: setDeviceName = %s", data->valuestring);
    return gmc_settings_handle_set_device_name(data->valuestring);
}

static esp_err_t handle_restore_factory(const cJSON *json)
{
    ESP_LOGI(TAG, "Processing: restoreFactory");
    return gmc_settings_handle_restore_factory();
}

static esp_err_t handle_set_wifi_passwd(const cJSON *json)
{
    ESP_LOGI(TAG, "Processing: setWIFIPasswd");
    const cJSON *data = cJSON_GetObjectItem(json, "data");
    return gmc_settings_handle_set_wifi_passwd(data);
}

static esp_err_t handle_set_device_sn(const cJSON *json)
{
    const cJSON *data = cJSON_GetObjectItem(json, "data");
    if (!data || !cJSON_IsString(data))
    {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "Processing: setDeviceSn = %s", data->valuestring);
    return gmc_settings_handle_set_device_sn(data->valuestring);
}

static esp_err_t handle_get_wifi_information(const cJSON *json)
{
    ESP_LOGI(TAG, "Processing: getWIFIInformation");
    return gmc_settings_handle_get_wifi_info();
}

static esp_err_t handle_get_device_information(const cJSON *json)
{
    ESP_LOGI(TAG, "Processing: getDeviceInformation");
    return gmc_settings_handle_get_device_info();
}

static esp_err_t handle_get_user_mode_list(const cJSON *json)
{
    ESP_LOGI(TAG, "Processing: getUserModeList");
    return gmc_user_handle_get_mode_list();
}

static esp_err_t handle_set_user_mode(const cJSON *json)
{
    const cJSON *data = cJSON_GetObjectItem(json, "data");
    if (!data || !cJSON_IsNumber(data))
    {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t mode = (uint8_t)data->valueint;
    ESP_LOGI(TAG, "Processing: setUserMode = %d", mode);
    return gmc_user_handle_set_mode(mode);
}

static esp_err_t handle_get_user_mode(const cJSON *json)
{
    ESP_LOGI(TAG, "Processing: getUserMode");
    return gmc_user_handle_get_mode();
}

// ==================== ShareAudio处理函数 ====================
static esp_err_t handle_get_share_mode(const cJSON *json)
{
    ESP_LOGI(TAG, "Processing: getShareAudioMode");
    return gmc_broadcast_handle_get_mode();
}

static esp_err_t handle_set_share_mode(const cJSON *json)
{
    const cJSON *data = cJSON_GetObjectItem(json, "data");
    if (!data || !cJSON_IsNumber(data))
    {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t mode = (uint8_t)data->valueint;
    ESP_LOGI(TAG, "Processing: setShareAudioMode = %d", mode);
    return gmc_broadcast_handle_set_mode(mode);
}

static esp_err_t handle_get_main_mode_cfg(const cJSON *json)
{
    ESP_LOGI(TAG, "Processing: getMainModeCfg");
    return gmc_broadcast_handle_get_main_config();
}

static esp_err_t handle_set_main_mode_cfg(const cJSON *json)
{
    const cJSON *data = cJSON_GetObjectItem(json, "data");
    if (!data || !cJSON_IsNumber(data))
    {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t preemptive = (uint8_t)data->valueint;
    ESP_LOGI(TAG, "Processing: setMainModeCfg preemptive = %d", preemptive);
    return gmc_broadcast_handle_set_main_config(preemptive);
}

static esp_err_t handle_get_sub_mode_cfg(const cJSON *json)
{
    ESP_LOGI(TAG, "Processing: getSubModeCfg");
    return gmc_broadcast_handle_get_sub_config();
}

static esp_err_t handle_change_sub_mode_group(const cJSON *json)
{
    const cJSON *data = cJSON_GetObjectItem(json, "data");
    if (!data || !cJSON_IsString(data))
    {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "Processing: changeSubModeGroup = %s", data->valuestring);
    return gmc_broadcast_handle_change_sub_group(data->valuestring);
}

// ==================== 设备控制处理函数 ====================
static esp_err_t handle_reboot(const cJSON *json)
{
    const cJSON *data = cJSON_GetObjectItem(json, "data");
    uint8_t delay = 0;
    if (data && cJSON_IsNumber(data))
    {
        delay = (uint8_t)data->valueint;
    }
    ESP_LOGI(TAG, "Processing: reboot delay=%d", delay);
    return gmc_device_handle_reboot(delay);
}

static esp_err_t handle_standby(const cJSON *json)
{
    const cJSON *data = cJSON_GetObjectItem(json, "data");
    if (!data || !cJSON_IsNumber(data))
    {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t mode = (uint8_t)data->valueint;
    ESP_LOGI(TAG, "Processing: standby mode=%d", mode);
    return gmc_device_handle_standby(mode);
}

static esp_err_t handle_ota_upgrade(const cJSON *json)
{
    const cJSON *data = cJSON_GetObjectItem(json, "data");
    if (!data || !cJSON_IsString(data))
    {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "Processing: OTAUpgrade url = %s", data->valuestring);
    // TODO: Implement OTA upgrade when API becomes available
    ESP_LOGW(TAG, "OTAUpgrade not implemented yet");
    return ESP_OK;
}

static esp_err_t handle_set_recorder(const cJSON *json)
{
    const cJSON *data = cJSON_GetObjectItem(json, "data");
    if (!data || !cJSON_IsNumber(data))
    {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t state = (uint8_t)data->valueint;
    ESP_LOGI(TAG, "Processing: setRecorder = %d", state);
    esp_err_t ret;
    if (state == 1)
    {
        ret = gmc_recorder_handle_start(data); // Start recording
    }
    else
    {
        ret = gmc_recorder_handle_stop(); // Stop recording
    }
    
    if (ret == ESP_OK) {
        esp_err_t send_ret = gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_DEVICE_CONTROL, "setRecorder", 0, "success", NULL);
        return send_ret;
    } else {
        esp_err_t send_ret = gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_DEVICE_CONTROL, "setRecorder", -1, "Failed", NULL);
        return send_ret;
    }
}

static esp_err_t handle_get_recorder(const cJSON *json)
{
    ESP_LOGI(TAG, "Processing: getRecorder");
    // gmc_recorder_handle_get_status sends response internally
    return gmc_recorder_handle_get_status();
}

// ==================== 邮件处理函数 ====================
static esp_err_t handle_read_mail(const cJSON *json)
{
    ESP_LOGI(TAG, "Processing: readmail");
    const cJSON *data = cJSON_GetObjectItem(json, "data");
    return gmc_email_handle_readmail(data);
}

// ==================== 命令映射表 ====================
static const gmc_cmd_map_t cmd_map[] = {
    // 播放控制
    {"playControl", "getInputSourceList", handle_get_input_source_list},
    {"playControl", "getCurrentInputSource", handle_get_current_input_source},
    {"playControl", "setCurrentInputSource", handle_set_current_input_source},
    {"playControl", "setPlay", handle_set_play},
    {"playControl", "getPlayStatus", handle_get_play_status},

    // 音频控制
    {"audioControl", "getAudioChannelList", handle_get_audio_channel_list},
    {"audioControl", "setAudioChannelVolume", handle_set_audio_channel_volume},
    {"audioControl", "getAudioChannelVolume", handle_get_audio_channel_volume},
    {"audioControl", "setMute", handle_set_mute},
    {"audioControl", "getMute", handle_get_mute},
    {"audioControl", "getEQModeList", handle_get_eq_mode_list},
    {"audioControl", "setEQMode", handle_set_eq_mode},
    {"audioControl", "getEQMode", handle_get_eq_mode},
    {"audioControl", "getEQFilter", handle_get_eq_filter},
    {"audioControl", "setEQFilter", handle_set_eq_filter},

    // 设置
    {"settings", "setDeviceName", handle_set_device_name},
    {"settings", "restoreFactary", handle_restore_factory},
    {"settings", "setWIFIPasswd", handle_set_wifi_passwd},
    {"settings", "setDeviceSn", handle_set_device_sn},
    {"settings", "setDeviceSN", handle_set_device_sn},
    {"settings", "getWIFIInformation", handle_get_wifi_information},
    {"settings", "getDeviceInformation", handle_get_device_information},
    {"settings", "getUserModeList", handle_get_user_mode_list},
    {"settings", "setUserMode", handle_set_user_mode},
    {"settings", "getUserMode", handle_get_user_mode},

    // 设备控制
    {"deviceControl", "reboot", handle_reboot},
    {"deviceControl", "stanby", handle_standby},
    {"deviceControl", "OTAUpgrade", handle_ota_upgrade},
    {"deviceControl", "setRecorder", handle_set_recorder},
    {"deviceControl", "getRecorder", handle_get_recorder},

    // ShareAudio
    {"shareAudio", "getMode", handle_get_share_mode},
    {"shareAudio", "setMode", handle_set_share_mode},
    {"shareAudio", "getMainModeCfg", handle_get_main_mode_cfg},
    {"shareAudio", "setMainModeCfg", handle_set_main_mode_cfg},
    {"shareAudio", "getSubModeCfg", handle_get_sub_mode_cfg},
    {"shareAudio", "changeSubModeGroup", handle_change_sub_mode_group},

    // 邮件
    {"mail", "readmail", handle_read_mail},
};

#define CMD_MAP_SIZE (sizeof(cmd_map) / sizeof(gmc_cmd_map_t))

// ==================== 主处理函数 ====================
esp_err_t gmc_proc(const cJSON *json)
{
    if (!json || !cJSON_IsObject(json))
    {
        ESP_LOGE(TAG, "Invalid JSON object");
        return ESP_ERR_INVALID_ARG;
    }

    // 提取 type 和 cmd 字段
    const cJSON *type_item = cJSON_GetObjectItem(json, "type");
    const cJSON *cmd_item = cJSON_GetObjectItem(json, "cmd");

    if (!type_item || !cmd_item ||
        !cJSON_IsString(type_item) || !cJSON_IsString(cmd_item))
    {
        ESP_LOGE(TAG, "Missing or invalid 'type'/'cmd' fields");
        return ESP_ERR_INVALID_ARG;
    }

    const char *type_str = type_item->valuestring;
    const char *cmd_str = cmd_item->valuestring;

    // 查找匹配的命令处理器
    for (size_t i = 0; i < CMD_MAP_SIZE; i++)
    {
        if (strcmp(cmd_map[i].type, type_str) == 0 &&
            strcmp(cmd_map[i].cmd, cmd_str) == 0)
        {

            ESP_LOGI(TAG, "Matched command: type=%s, cmd=%s", type_str, cmd_str);
            return cmd_map[i].handler(json);
        }
    }

    // 未找到匹配的命令
    ESP_LOGW(TAG, "Unknown command: type=%s, cmd=%s", type_str, cmd_str);
    return ESP_ERR_NOT_FOUND;
}
