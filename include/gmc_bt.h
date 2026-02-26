#ifndef GMC_BT_H
#define GMC_BT_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief BLE connection state
     */
    typedef enum
    {
        GMC_BT_STATE_IDLE,
        GMC_BT_STATE_ADVERTISING,
        GMC_BT_STATE_CONNECTED,
        GMC_BT_STATE_DISCONNECTED
    } gmc_bt_state_t;

    /**
     * @brief BLE connection information
     */
    typedef struct
    {
        uint16_t conn_id;
        uint8_t remote_bda[6];
        gmc_bt_state_t state;
    } gmc_bt_conn_info_t;

    /**
     * @brief Callback function for received BLE data
     *
     * @param data Pointer to received data
     * @param len Length of received data
     */
    typedef void (*gmc_bt_rx_callback_t)(const uint8_t *data, size_t len);

    /**
     * @brief Callback function for BLE state changes
     *
     * @param state New BLE state
     * @param connected True if device is connected
     */
    typedef void (*gmc_bt_state_callback_t)(gmc_bt_state_t state, bool connected);

    /**
     * @brief Initialize GMC BLE communication
     *
     * @return esp_err_t ESP_OK on success
     */
    esp_err_t gmc_bt_init(void);

    /**
     * @brief Update BLE device name (GAP + advertising name)
     *
     * Best-effort: updates the GAP Device Name characteristic immediately.
     * If currently advertising, advertising payload is refreshed.
     *
     * @param name New UTF-8 device name (will be truncated if too long)
     * @return esp_err_t ESP_OK on success
     */
    esp_err_t gmc_bt_set_device_name(const char *name);

    /**
     * @brief Start BLE advertising
     *
     * @return esp_err_t ESP_OK on success
     */
    esp_err_t gmc_bt_start_advertising(void);

    /**
     * @brief Stop BLE advertising
     *
     * @return esp_err_t ESP_OK on success
     */
    esp_err_t gmc_bt_stop_advertising(void);

    /**
     * @brief Send data via BLE
     *
     * @param data Pointer to data to send
     * @param len Length of data
     * @return esp_err_t ESP_OK on success
     */
    esp_err_t gmc_bt_send(const uint8_t *data, size_t len);

    /**
     * @brief Send data to Audio device (Central role)
     *
     * @param data Pointer to data to send
     * @param len Length of data
     * @return esp_err_t ESP_OK on success
     */
    esp_err_t gmc_bt_send_to_audio(const uint8_t *data, size_t len);

    /**
     * @brief Register callback for received BLE data
     *
     * @param callback Function to call when data is received
     */
    void gmc_bt_register_rx_callback(gmc_bt_rx_callback_t callback);

    /**
     * @brief Register callback for BLE state changes
     *
     * @param callback Function to call when BLE state changes
     */
    void gmc_bt_register_state_callback(gmc_bt_state_callback_t callback);

    /**
     * @brief Get current BLE connection state
     *
     * @return gmc_bt_state_t Current state
     */
    gmc_bt_state_t gmc_bt_get_state(void);

    /**
     * @brief Check if device can send notifications
     *
     * @return true if connected and notifications enabled
     */
    bool gmc_bt_can_send(void);

    /**
     * @brief Check if audio peripheral is connected (Central role)
     *
     * @return true if audio peripheral is connected
     */
    bool gmc_bt_audio_connected(void);

    /**
     * @brief Get current connection information
     *
     * @param info Pointer to store connection info
     * @return esp_err_t ESP_OK on success
     */
    esp_err_t gmc_bt_get_conn_info(gmc_bt_conn_info_t *info);

    /**
     * @brief Disconnect current BLE connection
     *
     * @return esp_err_t ESP_OK on success
     */
    esp_err_t gmc_bt_disconnect(void);

#ifdef __cplusplus
}
#endif

#endif // GMC_BT_H
