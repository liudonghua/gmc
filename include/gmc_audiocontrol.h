#ifndef GMC_AUDIOCONTROL_H
#define GMC_AUDIOCONTROL_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Audio channel IDs
#define AUDIO_CH_ALL                    0   // All channels
#define AUDIO_CH_MAIN_LEFT              1   // Main - Left
#define AUDIO_CH_MAIN_CENTER            2   // Main - Center
#define AUDIO_CH_MAIN_RIGHT             3   // Main - Right
#define AUDIO_CH_MAIN_FRONT_LEFT        4   // Main - Front Left Surround
#define AUDIO_CH_MAIN_FRONT_RIGHT       5   // Main - Front Right Surround
#define AUDIO_CH_MAIN_MID_LEFT          6   // Main - Mid Left Surround
#define AUDIO_CH_MAIN_MID_RIGHT         7   // Main - Mid Right Surround
#define AUDIO_CH_MAIN_REAR_LEFT         8   // Main - Rear Left Surround
#define AUDIO_CH_MAIN_REAR_RIGHT        9   // Main - Rear Right Surround
#define AUDIO_CH_SUBWOOFER              10  // Subwoofer
#define AUDIO_CH_HEIGHT_FRONT_LEFT      11  // Height - Front Left
#define AUDIO_CH_HEIGHT_FRONT_RIGHT     12  // Height - Front Right
#define AUDIO_CH_HEIGHT_REAR_LEFT       13  // Height - Rear Left
#define AUDIO_CH_HEIGHT_REAR_RIGHT      14  // Height - Rear Right

#define AUDIO_VOLUME_MIN                0
#define AUDIO_VOLUME_MAX                100
#define AUDIO_VOLUME_DEFAULT            50

/**
 * @brief Audio channel information
 */
typedef struct {
    uint8_t id;             // Channel ID
    uint8_t changeable;     // 0-not changeable, 1-changeable
    uint8_t volume;         // Current volume (0-100)
    uint8_t min_volume;     // Minimum volume
    uint8_t max_volume;     // Maximum volume
    uint8_t status;         // 0-disconnected, 1-connected
    uint8_t muted;          // 0-muted, 1-unmuted
} audio_channel_t;

/**
 * @brief Callback function type for volume/mute changes
 * 
 * @param channel_id Channel ID that changed
 * @param volume New volume value
 * @param muted New mute state
 */
typedef void (*audio_change_callback_t)(uint8_t channel_id, uint8_t volume, uint8_t muted);

/**
 * @brief Initialize audio control module
 * 
 * @param channel_map Audio channel map string (e.g., "2.1.0", "5.1.4")
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_audio_init(const char *channel_map);

/**
 * @brief Register callback for audio changes
 * 
 * @param callback Function to call when audio settings change
 */
void gmc_audio_register_callback(audio_change_callback_t callback);

/**
 * @brief Get audio channel by ID
 * 
 * @param channel_id Channel ID
 * @param channel Pointer to store channel info
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_audio_get_channel(uint8_t channel_id, audio_channel_t *channel);

/**
 * @brief Set audio channel volume
 * 
 * @param channel_id Channel ID
 * @param volume Volume value (0-100)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_audio_set_volume(uint8_t channel_id, uint8_t volume);

/**
 * @brief Get audio channel volume
 * 
 * @param channel_id Channel ID
 * @param volume Pointer to store volume value
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_audio_get_volume(uint8_t channel_id, uint8_t *volume);

/**
 * @brief Set audio channel mute state
 * 
 * @param channel_id Channel ID
 * @param muted 0-mute, 1-unmute
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_audio_set_mute(uint8_t channel_id, uint8_t muted);

/**
 * @brief Get audio channel mute state
 * 
 * @param channel_id Channel ID
 * @param muted Pointer to store mute state
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_audio_get_mute(uint8_t channel_id, uint8_t *muted);

/**
 * @brief Handle getAudioChannelList command
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_audio_handle_get_channel_list(void);

/**
 * @brief Handle setAudioChannelVolume command
 * 
 * @param data_json JSON object containing channel_id and volume
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_audio_handle_set_volume(const void *data_json);

/**
 * @brief Handle getAudioChannelVolume command
 * 
 * @param channel_id Channel ID
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_audio_handle_get_volume(uint8_t channel_id);

/**
 * @brief Handle setMute command
 * 
 * @param data_json JSON object containing channel_id and mute value
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_audio_handle_set_mute(const void *data_json);

/**
 * @brief Handle getMute command
 * 
 * @param channel_id Channel ID
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_audio_handle_get_mute(uint8_t channel_id);

/**
 * @brief Send volume changed notification
 * 
 * @param channel_id Channel ID
 * @param volume New volume value
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_audio_notify_volume_changed(uint8_t channel_id, uint8_t volume);

/**
 * @brief Send mute changed notification
 * 
 * @param channel_id Channel ID
 * @param muted New mute state
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_audio_notify_mute_changed(uint8_t channel_id, uint8_t muted);

#ifdef __cplusplus
}
#endif

#endif // GMC_AUDIOCONTROL_H
