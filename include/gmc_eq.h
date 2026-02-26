#ifndef GMC_EQ_H
#define GMC_EQ_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// EQ Mode IDs
#define EQ_MODE_MOVIE           1
#define EQ_MODE_MUSIC           2
#define EQ_MODE_VOICE           3
#define EQ_MODE_STADIUM         4
#define EQ_MODE_STANDARD        5
#define EQ_MODE_DIRECT          6
#define EQ_MODE_PART            7
#define EQ_MODE_CUSTOM          8

#define EQ_MODE_MIN             1
#define EQ_MODE_MAX             8
#define EQ_MODE_DEFAULT         EQ_MODE_STANDARD

#define MAX_EQ_BANDS            10

/**
 * @brief EQ band/filter structure
 */
typedef struct {
    uint8_t band_id;        // Band ID
    float frequency;        // Frequency (Hz)
    float gain;             // Gain (dB)
    float q_factor;         // Q factor (slope)
    uint8_t phase;          // Phase (1/180 degrees)
} eq_band_t;

/**
 * @brief EQ mode information
 */
typedef struct {
    uint8_t mode_id;
    char name[32];
    uint8_t band_count;
    eq_band_t bands[MAX_EQ_BANDS];
} eq_mode_t;

/**
 * @brief Callback function type for EQ mode changes
 * 
 * @param mode_id New EQ mode ID
 */
typedef void (*eq_mode_change_callback_t)(uint8_t mode_id);

/**
 * @brief Initialize EQ module
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_eq_init(void);

/**
 * @brief Register callback for EQ mode changes
 * 
 * @param callback Function to call when EQ mode changes
 */
void gmc_eq_register_callback(eq_mode_change_callback_t callback);

/**
 * @brief Get current EQ mode
 * 
 * @param mode_id Pointer to store current mode ID
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_eq_get_mode(uint8_t *mode_id);

/**
 * @brief Set EQ mode
 * 
 * @param mode_id EQ mode ID to set
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_eq_set_mode(uint8_t mode_id);

/**
 * @brief Get EQ filter/band parameters for a mode
 * 
 * @param eq_id EQ mode ID
 * @param mode Pointer to store mode information
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_eq_get_filter(uint8_t eq_id, eq_mode_t *mode);

/**
 * @brief Set EQ filter/band parameters
 * 
 * @param eq_id EQ mode ID
 * @param band Pointer to band parameters to set
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_eq_set_filter(uint8_t eq_id, const eq_band_t *band);

/**
 * @brief Handle getEQModeList command
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_eq_handle_get_mode_list(void);

/**
 * @brief Handle setEQMode command
 * 
 * @param mode_id EQ mode ID to set
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_eq_handle_set_mode(uint8_t mode_id);

/**
 * @brief Handle getEQMode command
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_eq_handle_get_mode(void);

/**
 * @brief Handle getEQFilter command
 * 
 * @param eq_id EQ mode ID
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_eq_handle_get_filter(uint8_t eq_id);

/**
 * @brief Handle setEQFilter command
 * 
 * @param data_json JSON object containing filter parameters
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_eq_handle_set_filter(const void *data_json);

/**
 * @brief Send EQ mode changed notification
 * 
 * @param mode_id New EQ mode ID
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_eq_notify_mode_changed(uint8_t mode_id);

#ifdef __cplusplus
}
#endif

#endif // GMC_EQ_H
