#ifndef GMC_USER_H
#define GMC_USER_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// User mode IDs
#define USER_MODE_COMPANION         0   // 情感陪伴模式
#define USER_MODE_MEETING           1   // 会议记录模式
#define USER_MODE_TRANSLATION       2   // 语言翻译模式
#define USER_MODE_UPGRADE           3   // 升级模式 (不支持客户端设置)

#define USER_MODE_MIN               0
#define USER_MODE_MAX               2   // Max mode that clients can set
#define USER_MODE_UPGRADE_INTERNAL  3   // Internal only
#define USER_MODE_DEFAULT           USER_MODE_COMPANION

/**
 * @brief User mode information
 */
typedef struct {
    uint8_t mode_id;
    const char *name;
    const char *description;
    bool client_settable;  // Whether client can set this mode
} user_mode_info_t;

/**
 * @brief Callback function type for user mode changes
 * 
 * @param old_mode Previous mode ID
 * @param new_mode New mode ID
 */
typedef void (*user_mode_change_callback_t)(uint8_t old_mode, uint8_t new_mode);

/**
 * @brief Initialize user mode module
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_user_init(void);

/**
 * @brief Register callback for user mode changes
 * 
 * @param callback Function to call when user mode changes
 */
void gmc_user_register_callback(user_mode_change_callback_t callback);

/**
 * @brief Get current user mode
 * 
 * @param mode_id Pointer to store current mode ID
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_user_get_mode(uint8_t *mode_id);

/**
 * @brief Set user mode
 * 
 * @param mode_id User mode ID to set
 * @param internal If true, allows setting upgrade mode (internal use only)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_user_set_mode(uint8_t mode_id, bool internal);

/**
 * @brief Get mode information
 * 
 * @param mode_id Mode ID
 * @param info Pointer to store mode information
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_user_get_mode_info(uint8_t mode_id, user_mode_info_t *info);

/**
 * @brief Check if currently in upgrade mode
 * 
 * @return true if in upgrade mode
 */
bool gmc_user_is_upgrade_mode(void);

/**
 * @brief Enter upgrade mode (internal use only)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_user_enter_upgrade_mode(void);

/**
 * @brief Exit upgrade mode (internal use only)
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_user_exit_upgrade_mode(void);

/**
 * @brief Handle getUserModeList command
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_user_handle_get_mode_list(void);

/**
 * @brief Handle setUserMode command
 * 
 * @param mode_id User mode ID
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_user_handle_set_mode(uint8_t mode_id);

/**
 * @brief Handle getUserMode command
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_user_handle_get_mode(void);

/**
 * @brief Send user mode changed notification
 * 
 * @param mode_id New mode ID
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_user_notify_mode_changed(uint8_t mode_id);

#ifdef __cplusplus
}
#endif

#endif // GMC_USER_H
