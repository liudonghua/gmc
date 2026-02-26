#ifndef GMC_RECORDER_H
#define GMC_RECORDER_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Recorder status information
     */
    typedef struct
    {
        bool recording;         // Recording in progress
        uint32_t duration_sec;  // Recording duration in seconds
        uint32_t storage_used;  // Storage used in bytes
        uint32_t storage_total; // Total storage capacity in bytes
        char title[64];         // Recording title
    } recorder_status_t;

    /**
     * @brief Initialize recorder module
     *
     * @return esp_err_t ESP_OK on success
     */
    esp_err_t gmc_recorder_init(void);

    /**
     * @brief Handle startRecording command
     *
     * @param data_json JSON object containing optional title
     * @return esp_err_t ESP_OK on success
     */
    esp_err_t gmc_recorder_handle_start(const void *data_json);

    /**
     * @brief Handle stopRecording command
     *
     * @return esp_err_t ESP_OK on success
     */
    esp_err_t gmc_recorder_handle_stop(void);

    /**
     * @brief Handle getRecordingStatus command
     *
     * @return esp_err_t ESP_OK on success
     */
    esp_err_t gmc_recorder_handle_get_status(void);

    /**
     * @brief Send recording status changed notification
     *
     * @param status Current recorder status
     * @return esp_err_t ESP_OK on success
     */
    esp_err_t gmc_recorder_notify_status_changed(const recorder_status_t *status);

#ifdef __cplusplus
}
#endif

#endif // GMC_RECORDER_H
