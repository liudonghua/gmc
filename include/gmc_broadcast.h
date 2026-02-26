#ifndef GMC_BROADCAST_H
#define GMC_BROADCAST_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Share Audio modes
#define BROADCAST_MODE_OFF          0   // 关闭
#define BROADCAST_MODE_MAIN         1   // 主模式
#define BROADCAST_MODE_SUB          2   // 从模式

#define BROADCAST_MODE_MIN          0
#define BROADCAST_MODE_MAX          2
#define BROADCAST_MODE_DEFAULT      BROADCAST_MODE_OFF

// Preemptive mode
#define BROADCAST_PREEMPTIVE        1   // 抢占
#define BROADCAST_NON_PREEMPTIVE    0   // 非抢占

#define MAX_GROUP_ID_LEN            64
#define MAX_GROUP_LIST_SIZE         10

/**
 * @brief Broadcast group information
 */
typedef struct {
    char id[MAX_GROUP_ID_LEN];
} broadcast_group_t;

/**
 * @brief Main mode configuration
 */
typedef struct {
    char group_id[MAX_GROUP_ID_LEN];
    uint8_t preemptive;  // 1=preemptive, 0=non-preemptive
} main_mode_config_t;

/**
 * @brief Sub mode configuration
 */
typedef struct {
    char current_group_id[MAX_GROUP_ID_LEN];
    broadcast_group_t group_list[MAX_GROUP_LIST_SIZE];
    uint8_t group_count;
} sub_mode_config_t;

/**
 * @brief Callback function type for mode changes
 * 
 * @param mode New mode ID
 */
typedef void (*broadcast_mode_change_callback_t)(uint8_t mode);

/**
 * @brief Callback function type for main mode config changes
 * 
 * @param preemptive New preemptive setting
 */
typedef void (*broadcast_main_config_change_callback_t)(uint8_t preemptive);

/**
 * @brief Callback function type for sub mode group changes
 * 
 * @param group_id New group ID
 */
typedef void (*broadcast_sub_group_change_callback_t)(const char *group_id);

/**
 * @brief Initialize broadcast module
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_broadcast_init(void);

/**
 * @brief Register callback for mode changes
 * 
 * @param callback Function to call when mode changes
 */
void gmc_broadcast_register_mode_callback(broadcast_mode_change_callback_t callback);

/**
 * @brief Register callback for main mode config changes
 * 
 * @param callback Function to call when main config changes
 */
void gmc_broadcast_register_main_config_callback(broadcast_main_config_change_callback_t callback);

/**
 * @brief Register callback for sub mode group changes
 * 
 * @param callback Function to call when sub group changes
 */
void gmc_broadcast_register_sub_group_callback(broadcast_sub_group_change_callback_t callback);

/**
 * @brief Get current broadcast mode
 * 
 * @param mode Pointer to store current mode
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_broadcast_get_mode(uint8_t *mode);

/**
 * @brief Set broadcast mode
 * 
 * @param mode Mode to set (0=off, 1=main, 2=sub)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_broadcast_set_mode(uint8_t mode);

/**
 * @brief Get main mode configuration
 * 
 * @param config Pointer to store configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_broadcast_get_main_config(main_mode_config_t *config);

/**
 * @brief Set main mode preemptive setting
 * 
 * @param preemptive 1=preemptive, 0=non-preemptive
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_broadcast_set_main_preemptive(uint8_t preemptive);

/**
 * @brief Get sub mode configuration
 * 
 * @param config Pointer to store configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_broadcast_get_sub_config(sub_mode_config_t *config);

/**
 * @brief Change sub mode group
 * 
 * @param group_id Group ID to join
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_broadcast_change_sub_group(const char *group_id);

/**
 * @brief Add group to available list
 * 
 * @param group_id Group ID to add
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_broadcast_add_group(const char *group_id);

/**
 * @brief Remove group from available list
 * 
 * @param group_id Group ID to remove
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_broadcast_remove_group(const char *group_id);

/**
 * @brief Handle getMode command
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_broadcast_handle_get_mode(void);

/**
 * @brief Handle setMode command
 * 
 * @param mode Mode ID
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_broadcast_handle_set_mode(uint8_t mode);

/**
 * @brief Handle getMainModeCfg command
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_broadcast_handle_get_main_config(void);

/**
 * @brief Handle setMainModeCfg command
 * 
 * @param preemptive Preemptive setting
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_broadcast_handle_set_main_config(uint8_t preemptive);

/**
 * @brief Handle getSubModeCfg command
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_broadcast_handle_get_sub_config(void);

/**
 * @brief Handle changeSubModeGroup command
 * 
 * @param group_id Group ID
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_broadcast_handle_change_sub_group(const char *group_id);

/**
 * @brief Send mode changed notification
 * 
 * @param mode New mode ID
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_broadcast_notify_mode_changed(uint8_t mode);

/**
 * @brief Send main mode config changed notification
 * 
 * @param preemptive New preemptive setting
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_broadcast_notify_main_config_changed(uint8_t preemptive);

/**
 * @brief Send sub mode group changed notification
 * 
 * @param group_id New group ID
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_broadcast_notify_sub_group_changed(const char *group_id);

/**
 * @brief Send group list changed notification
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_broadcast_notify_group_list_changed(void);

#ifdef __cplusplus
}
#endif

#endif // GMC_BROADCAST_H
