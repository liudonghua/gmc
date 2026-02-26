#ifndef GMC_UART_H
#define GMC_UART_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief UART state enumeration
 */
typedef enum {
    GMC_UART_STATE_IDLE = 0,      // UART not initialized
    GMC_UART_STATE_READY,         // UART ready for communication
    GMC_UART_STATE_SENDING,       // Currently sending data
    GMC_UART_STATE_RECEIVING,     // Currently receiving data
    GMC_UART_STATE_ERROR          // UART error state
} gmc_uart_state_t;

/**
 * @brief UART configuration structure
 */
typedef struct {
    uart_port_t uart_num;         // UART port number (UART_NUM_0, UART_NUM_1, UART_NUM_2)
    int baud_rate;                // Baud rate (e.g., 115200)
    int tx_pin;                   // TX pin GPIO number
    int rx_pin;                   // RX pin GPIO number
    int rts_pin;                  // RTS pin GPIO number (UART_PIN_NO_CHANGE if not used)
    int cts_pin;                  // CTS pin GPIO number (UART_PIN_NO_CHANGE if not used)
    int rx_buffer_size;           // RX ring buffer size (default: 2048)
    int tx_buffer_size;           // TX ring buffer size (default: 0 for polling)
    int queue_size;               // Event queue size (default: 10)
} gmc_uart_config_t;

/**
 * @brief UART connection info structure
 */
typedef struct {
    gmc_uart_state_t state;       // Current UART state
    uart_port_t uart_num;         // UART port number
    int baud_rate;                // Current baud rate
    uint32_t tx_count;            // Total bytes transmitted
    uint32_t rx_count;            // Total bytes received
    uint32_t error_count;         // Total error count
} gmc_uart_info_t;

/**
 * @brief UART receive callback
 * @param data Received data buffer
 * @param len Length of received data
 */
typedef void (*gmc_uart_rx_callback_t)(const uint8_t *data, size_t len);

/**
 * @brief UART error callback
 * @param error_code Error code
 */
typedef void (*gmc_uart_error_callback_t)(esp_err_t error_code);

/**
 * @brief Initialize UART for GMC communication
 * @param config UART configuration
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t gmc_uart_init(const gmc_uart_config_t *config);

/**
 * @brief Start UART receive task
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t gmc_uart_start(void);

/**
 * @brief Stop UART receive task
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t gmc_uart_stop(void);

/**
 * @brief Deinitialize UART
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t gmc_uart_deinit(void);

/**
 * @brief Send data via UART
 * @param data Data buffer to send
 * @param len Length of data
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t gmc_uart_send(const uint8_t *data, size_t len);

/**
 * @brief Register receive callback
 * @param callback Callback function for received data
 */
void gmc_uart_register_rx_callback(gmc_uart_rx_callback_t callback);

/**
 * @brief Register error callback
 * @param callback Callback function for errors
 */
void gmc_uart_register_error_callback(gmc_uart_error_callback_t callback);

/**
 * @brief Get current UART state
 * @return Current UART state
 */
gmc_uart_state_t gmc_uart_get_state(void);

/**
 * @brief Get UART information
 * @param info Pointer to info structure to fill
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t gmc_uart_get_info(gmc_uart_info_t *info);

/**
 * @brief Flush UART TX buffer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t gmc_uart_flush_tx(void);

/**
 * @brief Flush UART RX buffer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t gmc_uart_flush_rx(void);

#ifdef __cplusplus
}
#endif

#endif // GMC_UART_H
