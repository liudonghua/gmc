#ifndef GMC_NETWORK_H
#define GMC_NETWORK_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Network protocol type
 */
typedef enum {
    GMC_NET_PROTOCOL_TCP,
    GMC_NET_PROTOCOL_UDP
} gmc_net_protocol_t;

/**
 * @brief Network mode
 */
typedef enum {
    GMC_NET_MODE_SERVER,
    GMC_NET_MODE_CLIENT
} gmc_net_mode_t;

/**
 * @brief Network connection state
 */
typedef enum {
    GMC_NET_STATE_IDLE,
    GMC_NET_STATE_LISTENING,
    GMC_NET_STATE_CONNECTED,
    GMC_NET_STATE_DISCONNECTED,
    GMC_NET_STATE_ERROR
} gmc_net_state_t;

/**
 * @brief Network connection information
 */
typedef struct {
    int socket;
    char remote_ip[16];
    uint16_t remote_port;
    gmc_net_state_t state;
} gmc_net_conn_info_t;

/**
 * @brief Network configuration
 */
typedef struct {
    gmc_net_protocol_t protocol;
    gmc_net_mode_t mode;
    uint16_t port;
    char remote_ip[16];  // Only for client mode
    uint16_t remote_port;  // Only for client mode
    uint32_t timeout_ms;
} gmc_net_config_t;

/**
 * @brief Callback function for received network data
 * 
 * @param data Pointer to received data
 * @param len Length of received data
 * @param remote_ip Remote IP address
 * @param remote_port Remote port
 */
typedef void (*gmc_net_rx_callback_t)(const uint8_t *data, size_t len,
                                       const char *remote_ip, uint16_t remote_port);

/**
 * @brief Callback function for connection events
 * 
 * @param connected true if connected, false if disconnected
 * @param remote_ip Remote IP address
 * @param remote_port Remote port
 */
typedef void (*gmc_net_conn_callback_t)(bool connected, const char *remote_ip, uint16_t remote_port);

/**
 * @brief Initialize GMC network communication
 * 
 * @param config Network configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_net_init(const gmc_net_config_t *config);

/**
 * @brief Start network server/client
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_net_start(void);

/**
 * @brief Stop network communication
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_net_stop(void);

/**
 * @brief Send data via network
 * 
 * @param data Pointer to data to send
 * @param len Length of data
 * @param dest_ip Destination IP (NULL for server mode or connected client)
 * @param dest_port Destination port (0 for server mode or connected client)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_net_send(const uint8_t *data, size_t len,
                       const char *dest_ip, uint16_t dest_port);

/**
 * @brief Register callback for received network data
 * 
 * @param callback Function to call when data is received
 */
void gmc_net_register_rx_callback(gmc_net_rx_callback_t callback);

/**
 * @brief Register callback for connection events
 * 
 * @param callback Function to call on connection/disconnection
 */
void gmc_net_register_conn_callback(gmc_net_conn_callback_t callback);

/**
 * @brief Get current network state
 * 
 * @return gmc_net_state_t Current state
 */
gmc_net_state_t gmc_net_get_state(void);

/**
 * @brief Get current connection information
 * 
 * @param info Pointer to store connection info
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_net_get_conn_info(gmc_net_conn_info_t *info);

/**
 * @brief Close current network connection
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_net_close_connection(void);

#ifdef __cplusplus
}
#endif

#endif // GMC_NETWORK_H
