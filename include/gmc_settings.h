#ifndef GMC_SETTINGS_H
#define GMC_SETTINGS_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define DEVICE_NAME_MAX_LEN 64
#define DEVICE_MODEL_MAX_LEN 16
#define DEVICE_SN_MAX_LEN 64
#define DEVICE_VERSION_NAME_LEN 16
#define WIFI_SSID_MAX_LEN 64
#define WIFI_PASSWORD_MAX_LEN 64
#define MAC_ADDRESS_STR_LEN 18 // "XX:XX:XX:XX:XX:XX"

    /**
     * @brief Device information structure
     */
    typedef struct
    {
        char name[DEVICE_NAME_MAX_LEN];
        char model[DEVICE_MODEL_MAX_LEN];
        char sn[DEVICE_SN_MAX_LEN];
        char version_name[DEVICE_VERSION_NAME_LEN];
        uint8_t version_code;
        char mac[MAC_ADDRESS_STR_LEN];
    } device_info_t;

    /**
     * @brief WiFi credentials structure
     */
    typedef struct
    {
        char ssid[WIFI_SSID_MAX_LEN];
        char password[WIFI_PASSWORD_MAX_LEN];
    } wifi_credentials_t;

    /**
     * @brief Initialize settings module
     *
     * @return esp_err_t ESP_OK on success
     */
    esp_err_t gmc_settings_init(void);

    /**
     * @brief Check whether the settings module is initialized
     */
    bool gmc_settings_is_initialized(void);

    /**
     * @brief Set device name
     *
     * @param name Device name (max 64 chars)
     * @return esp_err_t ESP_OK on success
     */
    esp_err_t gmc_settings_set_device_name(const char *name);

    /**
     * @brief Get device name
     *
     * @param name Buffer to store device name
     * @param len Buffer length
     * @return esp_err_t ESP_OK on success
     */
    esp_err_t gmc_settings_get_device_name(char *name, size_t len);

    /**
     * @brief Set device model
     *
     * @param model Device model (max 16 chars)
     * @return esp_err_t ESP_OK on success
     */
    esp_err_t gmc_settings_set_device_model(const char *model);

    /**
     * @brief Set device serial number
     *
     * @param sn Serial number (max 64 chars)
     * @return esp_err_t ESP_OK on success
     */
    esp_err_t gmc_settings_set_device_sn(const char *sn);

    /**
     * @brief Get complete device information
     *
     * @param info Pointer to store device info
     * @return esp_err_t ESP_OK on success
     */
    esp_err_t gmc_settings_get_device_info(device_info_t *info);

    /**
     * @brief Set WiFi credentials
     *
     * @param ssid WiFi SSID (max 64 chars)
     * @param password WiFi password (max 64 chars)
     * @return esp_err_t ESP_OK on success
     */
    esp_err_t gmc_settings_set_wifi_credentials(const char *ssid, const char *password);

    /**
     * @brief Get WiFi credentials
     *
     * @param credentials Pointer to store WiFi credentials
     * @return esp_err_t ESP_OK on success
     */
    esp_err_t gmc_settings_get_wifi_credentials(wifi_credentials_t *credentials);

    /**
     * @brief Restore factory settings
     *
     * @return esp_err_t ESP_OK on success
     */
    esp_err_t gmc_settings_restore_factory(void);

    /**
     * @brief Handle setDeviceName command
     *
     * @param name Device name
     * @return esp_err_t ESP_OK on success
     */
    esp_err_t gmc_settings_handle_set_device_name(const char *name);

    /**
     * @brief Handle restoreFactory command
     *
     * @return esp_err_t ESP_OK on success
     */
    esp_err_t gmc_settings_handle_restore_factory(void);

    /**
     * @brief Handle setWIFIPasswd command
     *
     * @param data_json JSON object with ssid and password
     * @return esp_err_t ESP_OK on success
     */
    esp_err_t gmc_settings_handle_set_wifi_passwd(const void *data_json);
    esp_err_t gmc_settings_handle_set_device_sn(const char *sn);

    /**
     * @brief Handle getWIFIInformation command
     *
     * @return esp_err_t ESP_OK on success
     */
    esp_err_t gmc_settings_handle_get_wifi_info(void);

    /**
     * @brief Handle getDeviceInformation command
     *
     * @return esp_err_t ESP_OK on success
     */
    esp_err_t gmc_settings_handle_get_device_info(void);

#ifdef __cplusplus
}
#endif

#endif // GMC_SETTINGS_H
