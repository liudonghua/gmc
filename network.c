#include "gmc_network.h"
#include "gmc_protocol.h"
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"

static const char *TAG = "gmc_network";

// Network configuration and state
static gmc_net_config_t net_config;
static gmc_net_state_t net_state = GMC_NET_STATE_IDLE;
static int listen_socket = -1;
static int client_socket = -1;
static char remote_ip[16] = {0};
static uint16_t remote_port = 0;

// Task handles
static TaskHandle_t server_task_handle = NULL;
static TaskHandle_t client_task_handle = NULL;

// Callbacks
static gmc_net_rx_callback_t rx_callback = NULL;
static gmc_net_conn_callback_t conn_callback = NULL;

// Thread safety
static SemaphoreHandle_t tx_mutex = NULL;
static SemaphoreHandle_t state_mutex = NULL;

// Buffer for receiving data
#define RX_BUFFER_SIZE 2048

// Forward declarations
static void tcp_server_task(void *pvParameters);
static void tcp_client_task(void *pvParameters);
static void udp_server_task(void *pvParameters);
static void udp_client_task(void *pvParameters);

static void set_state(gmc_net_state_t new_state)
{
    if (xSemaphoreTake(state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        net_state = new_state;
        xSemaphoreGive(state_mutex);
    }
}

// TCP Server implementation
static void tcp_server_task(void *pvParameters)
{
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    uint8_t rx_buffer[RX_BUFFER_SIZE];
    int opt = 1;

    ESP_LOGI(TAG, "TCP Server task started on port %d", net_config.port);

    // Create socket
    listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket < 0) {
        ESP_LOGE(TAG, "Failed to create socket: errno %d", errno);
        set_state(GMC_NET_STATE_ERROR);
        vTaskDelete(NULL);
        return;
    }

    // Set socket options
    setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind socket
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(net_config.port);

    if (bind(listen_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind socket: errno %d", errno);
        close(listen_socket);
        listen_socket = -1;
        set_state(GMC_NET_STATE_ERROR);
        vTaskDelete(NULL);
        return;
    }

    // Listen
    if (listen(listen_socket, 1) < 0) {
        ESP_LOGE(TAG, "Failed to listen: errno %d", errno);
        close(listen_socket);
        listen_socket = -1;
        set_state(GMC_NET_STATE_ERROR);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "TCP Server listening on port %d", net_config.port);
    set_state(GMC_NET_STATE_LISTENING);

    while (1) {
        // Accept connection
        ESP_LOGI(TAG, "Waiting for client connection...");
        client_socket = accept(listen_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        
        if (client_socket < 0) {
            if (errno == EINTR) {
                continue;
            }
            ESP_LOGE(TAG, "Accept failed: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // Get client info
        inet_ntop(AF_INET, &client_addr.sin_addr, remote_ip, sizeof(remote_ip));
        remote_port = ntohs(client_addr.sin_port);
        
        ESP_LOGI(TAG, "Client connected from %s:%d", remote_ip, remote_port);
        set_state(GMC_NET_STATE_CONNECTED);

        // Call connection callback
        if (conn_callback != NULL) {
            conn_callback(true, remote_ip, remote_port);
        }

        // Set receive timeout
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 500000; // 500ms
        setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        // Receive loop
        while (1) {
            int len = recv(client_socket, rx_buffer, sizeof(rx_buffer), 0);
            
            if (len < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // Timeout, continue
                    vTaskDelay(pdMS_TO_TICKS(10));
                    continue;
                }
                ESP_LOGE(TAG, "Receive failed: errno %d", errno);
                break;
            } else if (len == 0) {
                ESP_LOGI(TAG, "Client disconnected");
                break;
            }

            // Data received
            ESP_LOGI(TAG, "Received %d bytes from %s:%d", len, remote_ip, remote_port);
            ESP_LOG_BUFFER_HEX(TAG, rx_buffer, len);

            // Call RX callback
            if (rx_callback != NULL) {
                rx_callback(rx_buffer, len, remote_ip, remote_port);
            }
        }

        // Client disconnected
        close(client_socket);
        client_socket = -1;
        set_state(GMC_NET_STATE_LISTENING);

        // Call connection callback
        if (conn_callback != NULL) {
            conn_callback(false, remote_ip, remote_port);
        }

        memset(remote_ip, 0, sizeof(remote_ip));
        remote_port = 0;
    }

    // Cleanup
    if (client_socket >= 0) {
        close(client_socket);
    }
    if (listen_socket >= 0) {
        close(listen_socket);
    }
    vTaskDelete(NULL);
}

// TCP Client implementation
static void tcp_client_task(void *pvParameters)
{
    struct sockaddr_in server_addr;
    uint8_t rx_buffer[RX_BUFFER_SIZE];

    ESP_LOGI(TAG, "TCP Client task started");

    while (1) {
        // Create socket
        client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (client_socket < 0) {
            ESP_LOGE(TAG, "Failed to create socket: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        // Connect to server
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(net_config.remote_port);
        inet_pton(AF_INET, net_config.remote_ip, &server_addr.sin_addr);

        ESP_LOGI(TAG, "Connecting to %s:%d...", net_config.remote_ip, net_config.remote_port);

        if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            ESP_LOGE(TAG, "Connection failed: errno %d", errno);
            close(client_socket);
            client_socket = -1;
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        strncpy(remote_ip, net_config.remote_ip, sizeof(remote_ip) - 1);
        remote_port = net_config.remote_port;

        ESP_LOGI(TAG, "Connected to %s:%d", remote_ip, remote_port);
        set_state(GMC_NET_STATE_CONNECTED);

        // Call connection callback
        if (conn_callback != NULL) {
            conn_callback(true, remote_ip, remote_port);
        }

        // Set receive timeout
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 500000; // 500ms
        setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        // Receive loop
        while (1) {
            int len = recv(client_socket, rx_buffer, sizeof(rx_buffer), 0);
            
            if (len < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                    continue;
                }
                ESP_LOGE(TAG, "Receive failed: errno %d", errno);
                break;
            } else if (len == 0) {
                ESP_LOGI(TAG, "Server disconnected");
                break;
            }

            // Data received
            ESP_LOGI(TAG, "Received %d bytes from %s:%d", len, remote_ip, remote_port);
            ESP_LOG_BUFFER_HEX(TAG, rx_buffer, len);

            // Call RX callback
            if (rx_callback != NULL) {
                rx_callback(rx_buffer, len, remote_ip, remote_port);
            }
        }

        // Disconnected
        close(client_socket);
        client_socket = -1;
        set_state(GMC_NET_STATE_DISCONNECTED);

        // Call connection callback
        if (conn_callback != NULL) {
            conn_callback(false, remote_ip, remote_port);
        }

        memset(remote_ip, 0, sizeof(remote_ip));
        remote_port = 0;

        ESP_LOGI(TAG, "Reconnecting in 5 seconds...");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }

    vTaskDelete(NULL);
}

// UDP Server implementation
static void udp_server_task(void *pvParameters)
{
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    uint8_t rx_buffer[RX_BUFFER_SIZE];

    ESP_LOGI(TAG, "UDP Server task started on port %d", net_config.port);

    // Create socket
    listen_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (listen_socket < 0) {
        ESP_LOGE(TAG, "Failed to create socket: errno %d", errno);
        set_state(GMC_NET_STATE_ERROR);
        vTaskDelete(NULL);
        return;
    }

    // Bind socket
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(net_config.port);

    if (bind(listen_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind socket: errno %d", errno);
        close(listen_socket);
        listen_socket = -1;
        set_state(GMC_NET_STATE_ERROR);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "UDP Server listening on port %d", net_config.port);
    set_state(GMC_NET_STATE_LISTENING);

    // Receive loop
    while (1) {
        int len = recvfrom(listen_socket, rx_buffer, sizeof(rx_buffer), 0,
                          (struct sockaddr *)&client_addr, &client_addr_len);
        
        if (len < 0) {
            ESP_LOGE(TAG, "Receive failed: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Get client info
        char client_ip[16];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        uint16_t client_port = ntohs(client_addr.sin_port);

        ESP_LOGI(TAG, "Received %d bytes from %s:%d", len, client_ip, client_port);
        ESP_LOG_BUFFER_HEX(TAG, rx_buffer, len);

        // Call RX callback
        if (rx_callback != NULL) {
            rx_callback(rx_buffer, len, client_ip, client_port);
        }
    }

    close(listen_socket);
    listen_socket = -1;
    vTaskDelete(NULL);
}

// UDP Client implementation
static void udp_client_task(void *pvParameters)
{
    uint8_t rx_buffer[RX_BUFFER_SIZE];

    ESP_LOGI(TAG, "UDP Client task started");

    // Create socket
    client_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (client_socket < 0) {
        ESP_LOGE(TAG, "Failed to create socket: errno %d", errno);
        set_state(GMC_NET_STATE_ERROR);
        vTaskDelete(NULL);
        return;
    }

    strncpy(remote_ip, net_config.remote_ip, sizeof(remote_ip) - 1);
    remote_port = net_config.remote_port;

    ESP_LOGI(TAG, "UDP Client ready (target: %s:%d)", remote_ip, remote_port);
    set_state(GMC_NET_STATE_CONNECTED);

    // Set receive timeout
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 500000; // 500ms
    setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    // Receive loop (optional for UDP client)
    while (1) {
        struct sockaddr_in from_addr;
        socklen_t from_addr_len = sizeof(from_addr);

        int len = recvfrom(client_socket, rx_buffer, sizeof(rx_buffer), 0,
                          (struct sockaddr *)&from_addr, &from_addr_len);
        
        if (len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            ESP_LOGE(TAG, "Receive failed: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        char from_ip[16];
        inet_ntop(AF_INET, &from_addr.sin_addr, from_ip, sizeof(from_ip));
        uint16_t from_port = ntohs(from_addr.sin_port);

        ESP_LOGI(TAG, "Received %d bytes from %s:%d", len, from_ip, from_port);
        ESP_LOG_BUFFER_HEX(TAG, rx_buffer, len);

        // Call RX callback
        if (rx_callback != NULL) {
            rx_callback(rx_buffer, len, from_ip, from_port);
        }
    }

    close(client_socket);
    client_socket = -1;
    vTaskDelete(NULL);
}

esp_err_t gmc_net_init(const gmc_net_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Copy configuration
    memcpy(&net_config, config, sizeof(gmc_net_config_t));

    // Create mutexes
    tx_mutex = xSemaphoreCreateMutex();
    if (tx_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create TX mutex");
        return ESP_ERR_NO_MEM;
    }

    state_mutex = xSemaphoreCreateMutex();
    if (state_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        vSemaphoreDelete(tx_mutex);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "GMC network initialized: %s %s on port %d",
             config->protocol == GMC_NET_PROTOCOL_TCP ? "TCP" : "UDP",
             config->mode == GMC_NET_MODE_SERVER ? "Server" : "Client",
             config->mode == GMC_NET_MODE_SERVER ? config->port : config->remote_port);

    return ESP_OK;
}

esp_err_t gmc_net_start(void)
{
    if (net_state != GMC_NET_STATE_IDLE) {
        ESP_LOGW(TAG, "Network already started");
        return ESP_OK;
    }

    BaseType_t ret;

    if (net_config.protocol == GMC_NET_PROTOCOL_TCP) {
        if (net_config.mode == GMC_NET_MODE_SERVER) {
            ret = xTaskCreate(tcp_server_task, "tcp_server", 4096, NULL, 5, &server_task_handle);
        } else {
            ret = xTaskCreate(tcp_client_task, "tcp_client", 4096, NULL, 5, &client_task_handle);
        }
    } else {
        if (net_config.mode == GMC_NET_MODE_SERVER) {
            ret = xTaskCreate(udp_server_task, "udp_server", 4096, NULL, 5, &server_task_handle);
        } else {
            ret = xTaskCreate(udp_client_task, "udp_client", 4096, NULL, 5, &client_task_handle);
        }
    }

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create network task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Network started");
    return ESP_OK;
}

esp_err_t gmc_net_stop(void)
{
    // Close sockets
    if (client_socket >= 0) {
        close(client_socket);
        client_socket = -1;
    }
    if (listen_socket >= 0) {
        close(listen_socket);
        listen_socket = -1;
    }

    // Delete tasks
    if (server_task_handle != NULL) {
        vTaskDelete(server_task_handle);
        server_task_handle = NULL;
    }
    if (client_task_handle != NULL) {
        vTaskDelete(client_task_handle);
        client_task_handle = NULL;
    }

    set_state(GMC_NET_STATE_IDLE);

    ESP_LOGI(TAG, "Network stopped");
    return ESP_OK;
}

esp_err_t gmc_net_send(const uint8_t *data, size_t len,
                       const char *dest_ip, uint16_t dest_port)
{
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (net_state != GMC_NET_STATE_CONNECTED && net_state != GMC_NET_STATE_LISTENING) {
        ESP_LOGW(TAG, "Network not connected");
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(tx_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take TX mutex");
        return ESP_ERR_TIMEOUT;
    }

    int sent = 0;
    esp_err_t ret = ESP_OK;

    if (net_config.protocol == GMC_NET_PROTOCOL_TCP) {
        // TCP send
        if (client_socket < 0) {
            ESP_LOGE(TAG, "No active TCP connection");
            ret = ESP_ERR_INVALID_STATE;
        } else {
            sent = send(client_socket, data, len, 0);
            if (sent < 0) {
                ESP_LOGE(TAG, "TCP send failed: errno %d", errno);
                ret = ESP_FAIL;
            } else {
                ESP_LOGI(TAG, "Sent %d bytes via TCP", sent);
                ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, sent, ESP_LOG_DEBUG);
            }
        }
    } else {
        // UDP send
        struct sockaddr_in dest_addr;
        memset(&dest_addr, 0, sizeof(dest_addr));
        dest_addr.sin_family = AF_INET;

        // Use provided destination or default
        if (dest_ip != NULL && dest_port > 0) {
            inet_pton(AF_INET, dest_ip, &dest_addr.sin_addr);
            dest_addr.sin_port = htons(dest_port);
        } else {
            inet_pton(AF_INET, remote_ip, &dest_addr.sin_addr);
            dest_addr.sin_port = htons(remote_port);
        }

        int sock = (net_config.mode == GMC_NET_MODE_SERVER) ? listen_socket : client_socket;
        if (sock < 0) {
            ESP_LOGE(TAG, "No active UDP socket");
            ret = ESP_ERR_INVALID_STATE;
        } else {
            sent = sendto(sock, data, len, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
            if (sent < 0) {
                ESP_LOGE(TAG, "UDP send failed: errno %d", errno);
                ret = ESP_FAIL;
            } else {
                char ip_str[16];
                inet_ntop(AF_INET, &dest_addr.sin_addr, ip_str, sizeof(ip_str));
                ESP_LOGI(TAG, "Sent %d bytes via UDP to %s:%d", sent, ip_str, ntohs(dest_addr.sin_port));
                ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, sent, ESP_LOG_DEBUG);
            }
        }
    }

    xSemaphoreGive(tx_mutex);
    return ret;
}

void gmc_net_register_rx_callback(gmc_net_rx_callback_t callback)
{
    rx_callback = callback;
}

void gmc_net_register_conn_callback(gmc_net_conn_callback_t callback)
{
    conn_callback = callback;
}

gmc_net_state_t gmc_net_get_state(void)
{
    gmc_net_state_t state;
    if (xSemaphoreTake(state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        state = net_state;
        xSemaphoreGive(state_mutex);
    } else {
        state = GMC_NET_STATE_IDLE;
    }
    return state;
}

esp_err_t gmc_net_get_conn_info(gmc_net_conn_info_t *info)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    info->socket = client_socket;
    strncpy(info->remote_ip, remote_ip, sizeof(info->remote_ip) - 1);
    info->remote_port = remote_port;
    info->state = gmc_net_get_state();

    return ESP_OK;
}

esp_err_t gmc_net_close_connection(void)
{
    if (client_socket >= 0) {
        close(client_socket);
        client_socket = -1;
        ESP_LOGI(TAG, "Connection closed");
        return ESP_OK;
    }

    return ESP_ERR_INVALID_STATE;
}
