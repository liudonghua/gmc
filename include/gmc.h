#ifndef GMC_H
#define GMC_H

#include "gmc_audiocontrol.h"
#include "gmc_audio_control.h"
#include "gmc_broadcast.h"
#include "gmc_bt.h"
#include "gmc_device.h"
#include "gmc_email.h"
#include "gmc_eq.h"
#include "gmc_network.h"
#include "gmc_protocol.h"
#include "gmc_recorder.h"
#include "gmc_settings.h"
#include "gmc_uart.h"
#include "gmc_user.h"


#include <cJSON.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief GMC command handler function pointer type
     *
     * @param json JSON object containing the command data
     * @return esp_err_t ESP_OK on success, error code otherwise
     */
    typedef esp_err_t (*gmc_cmd_handler_t)(const cJSON *json);

    /**
     * @brief GMC command mapping structure
     */
    typedef struct
    {
        const char *type;          // Message type (e.g., "playControl", "audioControl")
        const char *cmd;           // Command name (e.g., "getInputSourceList")
        gmc_cmd_handler_t handler; // Handler function pointer
    } gmc_cmd_map_t;

    /**
     * @brief Process incoming GMC JSON command
     *
     * This function parses the JSON object, identifies the command type and name,
     * and dispatches it to the appropriate handler function.
     *
     * @param json JSON object containing "type", "cmd", and optional "data" fields
     * @return esp_err_t ESP_OK on success, ESP_ERR_NOT_FOUND if command not found,
     *                   ESP_ERR_INVALID_ARG if JSON is invalid
     */
    esp_err_t gmc_proc(const cJSON *json);

#ifdef __cplusplus
}
#endif

#endif // GMC_H