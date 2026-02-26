#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "esp_pm.h"
#include "cJSON.h"
#include "gmc_device.h"
#include "gmc_protocol.h"

static const char *TAG = "gmc_device";

static TimerHandle_t reboot_timer = NULL;
static uint8_t is_standby = 0;

/**
 * @brief Reboot timer callback
 */
static void reboot_timer_callback(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG, "Reboot timer expired, restarting device...");
    esp_restart();
}

esp_err_t gmc_device_init(void)
{
    ESP_LOGI(TAG, "Device control module initialized");
    return ESP_OK;
}

esp_err_t gmc_device_reboot(uint8_t delay_seconds)
{
    if (delay_seconds == 0) {
        ESP_LOGI(TAG, "Immediate reboot requested");
        esp_restart();
        return ESP_OK;  // Never reached
    }
    
    // Cancel any existing reboot timer
    if (reboot_timer != NULL) {
        xTimerStop(reboot_timer, 0);
        xTimerDelete(reboot_timer, 0);
        reboot_timer = NULL;
    }
    
    // Create new reboot timer
    reboot_timer = xTimerCreate("reboot_timer",
                                pdMS_TO_TICKS(delay_seconds * 1000),
                                pdFALSE,  // One-shot timer
                                NULL,
                                reboot_timer_callback);
    
    if (reboot_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create reboot timer");
        return ESP_ERR_NO_MEM;
    }
    
    if (xTimerStart(reboot_timer, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start reboot timer");
        xTimerDelete(reboot_timer, 0);
        reboot_timer = NULL;
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Device will reboot in %d seconds", delay_seconds);
    return ESP_OK;
}

esp_err_t gmc_device_cancel_reboot(void)
{
    if (reboot_timer != NULL) {
        xTimerStop(reboot_timer, 0);
        xTimerDelete(reboot_timer, 0);
        reboot_timer = NULL;
        ESP_LOGI(TAG, "Reboot cancelled");
        return ESP_OK;
    }
    
    return ESP_ERR_NOT_FOUND;
}

esp_err_t gmc_device_standby(uint8_t mode)
{
    switch (mode) {
        case STANDBY_MODE_LIGHT:
            ESP_LOGI(TAG, "Entering light sleep mode");
            
            // Configure wake up sources (e.g., GPIO, timer)
            // Example: Wake up after 10 seconds or on GPIO event
            esp_sleep_enable_timer_wakeup(10 * 1000000);  // 10 seconds in microseconds
            
            // Enter light sleep
            // Note: Light sleep can be woken by various interrupts
            esp_light_sleep_start();
            
            ESP_LOGI(TAG, "Woke up from light sleep");
            is_standby = 0;
            break;
            
        case STANDBY_MODE_DEEP:
            ESP_LOGI(TAG, "Entering deep sleep mode");
            
            // Configure wake up sources
            // Example: Wake up after 60 seconds
            esp_sleep_enable_timer_wakeup(60 * 1000000);  // 60 seconds
            
            // You can also configure GPIO wake up:
            // esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);  // Wake on GPIO0 LOW
            
            // Enter deep sleep (device will reset on wake)
            esp_deep_sleep_start();
            
            // Never reached - device resets on wake
            break;
            
        case STANDBY_MODE_EXIT:
            ESP_LOGI(TAG, "Exiting standby mode");
            is_standby = 0;
            
            // If in light sleep, this command itself will wake it
            // Deep sleep requires hardware wake trigger
            break;
            
        default:
            ESP_LOGE(TAG, "Invalid standby mode: %d", mode);
            return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
}

esp_err_t gmc_device_handle_reboot(uint8_t delay_seconds)
{
    esp_err_t ret = gmc_device_reboot(delay_seconds);
    
    if (ret == ESP_OK) {
        gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_DEVICE_CONTROL, "reboot", 0, "success", NULL);
    } else {
        gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_DEVICE_CONTROL, "reboot", -1, "Failed to schedule reboot", NULL);
    }
    
    return ret;
}

esp_err_t gmc_device_handle_standby(uint8_t mode)
{
    // Validate mode
    if (mode > STANDBY_MODE_EXIT) {
        gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_DEVICE_CONTROL, "stanby", -1, "Invalid standby mode", NULL);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Send response before entering standby (important!)
    gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_DEVICE_CONTROL, "stanby", 0, "success", NULL);
    
    // Small delay to ensure response is sent
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Enter standby mode
    esp_err_t ret = gmc_device_standby(mode);
    
    // Only reached if mode is EXIT or light sleep wakes up
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enter standby mode");
    }
    
    return ret;
}
