#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "gmc_uart.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "gmc_uart";

// UART receive buffer size
#define UART_RX_BUFFER_SIZE     2048
#define UART_TX_BUFFER_SIZE     0     // 0 for polling mode
#define UART_EVENT_QUEUE_SIZE   10
#define UART_READ_TIMEOUT_MS    100

// UART state management
static gmc_uart_state_t uart_state = GMC_UART_STATE_IDLE;
static gmc_uart_config_t uart_config;
static SemaphoreHandle_t tx_mutex = NULL;
static SemaphoreHandle_t state_mutex = NULL;
static TaskHandle_t rx_task_handle = NULL;
static QueueHandle_t uart_event_queue = NULL;

// Statistics
static uint32_t tx_count = 0;
static uint32_t rx_count = 0;
static uint32_t error_count = 0;

// Callbacks
static gmc_uart_rx_callback_t rx_callback = NULL;
static gmc_uart_error_callback_t error_callback = NULL;

// Forward declarations
static void uart_rx_task(void *arg);
static void set_uart_state(gmc_uart_state_t new_state);

/**
 * @brief Set UART state (thread-safe)
 */
static void set_uart_state(gmc_uart_state_t new_state) {
    if (state_mutex && xSemaphoreTake(state_mutex, portMAX_DELAY) == pdTRUE) {
        uart_state = new_state;
        xSemaphoreGive(state_mutex);
    }
}

/**
 * @brief Initialize UART for GMC communication
 */
esp_err_t gmc_uart_init(const gmc_uart_config_t *config) {
    if (!config) {
        ESP_LOGE(TAG, "Invalid config");
        return ESP_ERR_INVALID_ARG;
    }

    if (uart_state != GMC_UART_STATE_IDLE) {
        ESP_LOGW(TAG, "UART already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Create mutexes
    if (!tx_mutex) {
        tx_mutex = xSemaphoreCreateMutex();
        if (!tx_mutex) {
            ESP_LOGE(TAG, "Failed to create TX mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    if (!state_mutex) {
        state_mutex = xSemaphoreCreateMutex();
        if (!state_mutex) {
            ESP_LOGE(TAG, "Failed to create state mutex");
            vSemaphoreDelete(tx_mutex);
            tx_mutex = NULL;
            return ESP_ERR_NO_MEM;
        }
    }

    // Store configuration
    memcpy(&uart_config, config, sizeof(gmc_uart_config_t));

    // Set default values if not specified
    if (uart_config.rx_buffer_size == 0) {
        uart_config.rx_buffer_size = UART_RX_BUFFER_SIZE;
    }
    if (uart_config.tx_buffer_size == 0) {
        uart_config.tx_buffer_size = UART_TX_BUFFER_SIZE;
    }
    if (uart_config.queue_size == 0) {
        uart_config.queue_size = UART_EVENT_QUEUE_SIZE;
    }

    // UART configuration
    uart_config_t uart_cfg = {
        .baud_rate = config->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    // Install UART driver
    esp_err_t ret = uart_driver_install(config->uart_num, 
                                        uart_config.rx_buffer_size,
                                        uart_config.tx_buffer_size,
                                        uart_config.queue_size,
                                        &uart_event_queue, 
                                        0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    // Configure UART parameters
    ret = uart_param_config(config->uart_num, &uart_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(ret));
        goto cleanup_driver;
    }

    // Set UART pins
    ret = uart_set_pin(config->uart_num, 
                      config->tx_pin, 
                      config->rx_pin,
                      config->rts_pin, 
                      config->cts_pin);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(ret));
        goto cleanup_driver;
    }

    // Reset statistics
    tx_count = 0;
    rx_count = 0;
    error_count = 0;

    set_uart_state(GMC_UART_STATE_READY);
    ESP_LOGI(TAG, "UART%d initialized: %d baud, TX=%d, RX=%d", 
             config->uart_num, config->baud_rate, config->tx_pin, config->rx_pin);

    return ESP_OK;

cleanup_driver:
    uart_driver_delete(config->uart_num);
    uart_event_queue = NULL;

cleanup:
    if (tx_mutex) {
        vSemaphoreDelete(tx_mutex);
        tx_mutex = NULL;
    }
    if (state_mutex) {
        vSemaphoreDelete(state_mutex);
        state_mutex = NULL;
    }
    set_uart_state(GMC_UART_STATE_ERROR);
    return ret;
}

/**
 * @brief Start UART receive task
 */
esp_err_t gmc_uart_start(void) {
    if (uart_state != GMC_UART_STATE_READY) {
        ESP_LOGE(TAG, "UART not ready (state=%d)", uart_state);
        return ESP_ERR_INVALID_STATE;
    }

    if (rx_task_handle) {
        ESP_LOGW(TAG, "RX task already running");
        return ESP_OK;
    }

    // Create receive task
    BaseType_t ret = xTaskCreate(uart_rx_task, 
                                 "gmc_uart_rx",
                                 4096,
                                 NULL,
                                 5,
                                 &rx_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create RX task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "UART RX task started");
    return ESP_OK;
}

/**
 * @brief Stop UART receive task
 */
esp_err_t gmc_uart_stop(void) {
    if (rx_task_handle) {
        vTaskDelete(rx_task_handle);
        rx_task_handle = NULL;
        ESP_LOGI(TAG, "UART RX task stopped");
    }

    return ESP_OK;
}

/**
 * @brief Deinitialize UART
 */
esp_err_t gmc_uart_deinit(void) {
    // Stop receive task
    gmc_uart_stop();

    // Delete UART driver
    if (uart_config.uart_num >= 0) {
        uart_driver_delete(uart_config.uart_num);
        uart_event_queue = NULL;
    }

    // Delete mutexes
    if (tx_mutex) {
        vSemaphoreDelete(tx_mutex);
        tx_mutex = NULL;
    }

    if (state_mutex) {
        vSemaphoreDelete(state_mutex);
        state_mutex = NULL;
    }

    set_uart_state(GMC_UART_STATE_IDLE);
    ESP_LOGI(TAG, "UART deinitialized");

    return ESP_OK;
}

/**
 * @brief Send data via UART
 */
esp_err_t gmc_uart_send(const uint8_t *data, size_t len) {
    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (uart_state != GMC_UART_STATE_READY) {
        ESP_LOGE(TAG, "UART not ready (state=%d)", uart_state);
        return ESP_ERR_INVALID_STATE;
    }

    // Acquire TX mutex
    if (xSemaphoreTake(tx_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire TX mutex");
        return ESP_ERR_TIMEOUT;
    }

    set_uart_state(GMC_UART_STATE_SENDING);

    // Send data
    int written = uart_write_bytes(uart_config.uart_num, data, len);
    
    esp_err_t ret = ESP_OK;
    if (written != len) {
        ESP_LOGE(TAG, "UART write error: sent %d/%zu bytes", written, len);
        error_count++;
        ret = ESP_FAIL;
    } else {
        tx_count += len;
        ESP_LOGD(TAG, "UART sent %zu bytes", len);
    }

    set_uart_state(GMC_UART_STATE_READY);
    xSemaphoreGive(tx_mutex);

    return ret;
}

/**
 * @brief UART receive task
 */
static void uart_rx_task(void *arg) {
    uart_event_t event;
    const size_t rx_buffer_size = (uart_config.rx_buffer_size > 0) ? (size_t)uart_config.rx_buffer_size : (size_t)UART_RX_BUFFER_SIZE;
    uint8_t *rx_buffer = (uint8_t *)heap_caps_malloc(rx_buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    
    if (!rx_buffer) {
        ESP_LOGE(TAG, "Failed to allocate RX buffer");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "UART RX task running");

    while (1) {
        // Wait for UART event
        if (xQueueReceive(uart_event_queue, &event, pdMS_TO_TICKS(UART_READ_TIMEOUT_MS))) {
            switch (event.type) {
                case UART_DATA:
                    // Data available
                    if (event.size > 0) {
                        set_uart_state(GMC_UART_STATE_RECEIVING);
                        
                        size_t to_read = event.size > rx_buffer_size ? rx_buffer_size : event.size;
                        int len = uart_read_bytes(uart_config.uart_num, rx_buffer, to_read, pdMS_TO_TICKS(100));
                        
                        if (len > 0) {
                            rx_count += len;
                            ESP_LOGI(TAG, "UART received %d bytes", len);
                            ESP_LOG_BUFFER_HEX(TAG, rx_buffer, len);
                            
                            // Call receive callback
                            if (rx_callback) {
                                rx_callback(rx_buffer, len);
                            }
                        }
                        
                        set_uart_state(GMC_UART_STATE_READY);
                    }
                    break;

                case UART_FIFO_OVF:
                    ESP_LOGW(TAG, "UART FIFO overflow");
                    uart_flush_input(uart_config.uart_num);
                    xQueueReset(uart_event_queue);
                    error_count++;
                    if (error_callback) {
                        error_callback(ESP_ERR_INVALID_STATE);
                    }
                    break;

                case UART_BUFFER_FULL:
                    ESP_LOGW(TAG, "UART ring buffer full");
                    uart_flush_input(uart_config.uart_num);
                    xQueueReset(uart_event_queue);
                    error_count++;
                    if (error_callback) {
                        error_callback(ESP_ERR_NO_MEM);
                    }
                    break;

                case UART_BREAK:
                    ESP_LOGW(TAG, "UART break detected");
                    error_count++;
                    break;

                case UART_PARITY_ERR:
                    ESP_LOGW(TAG, "UART parity error");
                    error_count++;
                    if (error_callback) {
                        error_callback(ESP_ERR_INVALID_CRC);
                    }
                    break;

                case UART_FRAME_ERR:
                    ESP_LOGW(TAG, "UART frame error");
                    error_count++;
                    if (error_callback) {
                        error_callback(ESP_ERR_INVALID_RESPONSE);
                    }
                    break;

                default:
                    ESP_LOGW(TAG, "UART event type: %d", event.type);
                    break;
            }
        }
    }

    free(rx_buffer);
    vTaskDelete(NULL);
}

/**
 * @brief Register receive callback
 */
void gmc_uart_register_rx_callback(gmc_uart_rx_callback_t callback) {
    rx_callback = callback;
}

/**
 * @brief Register error callback
 */
void gmc_uart_register_error_callback(gmc_uart_error_callback_t callback) {
    error_callback = callback;
}

/**
 * @brief Get current UART state
 */
gmc_uart_state_t gmc_uart_get_state(void) {
    gmc_uart_state_t state;
    if (state_mutex && xSemaphoreTake(state_mutex, portMAX_DELAY) == pdTRUE) {
        state = uart_state;
        xSemaphoreGive(state_mutex);
    } else {
        state = GMC_UART_STATE_ERROR;
    }
    return state;
}

/**
 * @brief Get UART information
 */
esp_err_t gmc_uart_get_info(gmc_uart_info_t *info) {
    if (!info) {
        return ESP_ERR_INVALID_ARG;
    }

    info->state = gmc_uart_get_state();
    info->uart_num = uart_config.uart_num;
    info->baud_rate = uart_config.baud_rate;
    info->tx_count = tx_count;
    info->rx_count = rx_count;
    info->error_count = error_count;

    return ESP_OK;
}

/**
 * @brief Flush UART TX buffer
 */
esp_err_t gmc_uart_flush_tx(void) {
    if (uart_state == GMC_UART_STATE_IDLE) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = uart_wait_tx_done(uart_config.uart_num, pdMS_TO_TICKS(1000));
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "TX buffer flushed");
    } else {
        ESP_LOGW(TAG, "TX flush timeout");
    }

    return ret;
}

/**
 * @brief Flush UART RX buffer
 */
esp_err_t gmc_uart_flush_rx(void) {
    if (uart_state == GMC_UART_STATE_IDLE) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = uart_flush_input(uart_config.uart_num);
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "RX buffer flushed");
    }

    return ret;
}
