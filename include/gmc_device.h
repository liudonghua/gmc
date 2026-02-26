#ifndef GMC_DEVICE_H
#define GMC_DEVICE_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Standby modes
#define STANDBY_MODE_LIGHT      0   // Light sleep mode
#define STANDBY_MODE_DEEP       1   // Deep sleep mode
#define STANDBY_MODE_EXIT       2   // Exit standby mode

/**
 * @brief Initialize device control module
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_device_init(void);

/**
 * @brief Reboot device after specified delay
 * 
 * @param delay_seconds Countdown in seconds before reboot
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_device_reboot(uint8_t delay_seconds);

/**
 * @brief Enter standby mode
 * 
 * @param mode Standby mode: 0=light sleep, 1=deep sleep, 2=exit standby
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_device_standby(uint8_t mode);

/**
 * @brief Cancel pending reboot
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_device_cancel_reboot(void);

/**
 * @brief Handle reboot command
 * 
 * @param delay_seconds Countdown in seconds
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_device_handle_reboot(uint8_t delay_seconds);

/**
 * @brief Handle standby command
 * 
 * @param mode Standby mode
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_device_handle_standby(uint8_t mode);

#ifdef __cplusplus
}
#endif

#endif // GMC_DEVICE_H
