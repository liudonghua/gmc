#ifndef GMC_PROTOCOL_H
#define GMC_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

// Protocol constants
#define GMC_HEADER              0x00000001
#define GMC_HEADER_SIZE         4
// Minimum GMC frame size (no CRC):
// Header(4) + Type(1) + Control(1) + FrameID(1) + PacketID(2) + Reserved(2) + DataLen(2)
#define GMC_MIN_FRAME_SIZE      13

// Communication layer types
#define GMC_COMM_UART           0x00
#define GMC_COMM_BT             0x01
#define GMC_COMM_NETWORK        0x02
#define GMC_COMM_RESERVED       0x03

// Encryption types
#define GMC_ENC_NONE            0x00
#define GMC_ENC_XTEA            0x01
#define GMC_ENC_MD5             0x02
#define GMC_ENC_SHA128          0x03

// Data format types
#define GMC_FORMAT_BINARY       0x00
#define GMC_FORMAT_JSON         0x01

// Protocol version
#define GMC_VERSION_0           0x00
#define GMC_VERSION_1           0x01
#define GMC_VERSION_2           0x02  // Version 2 for JSON (data[4]=0x22)

// Control bits
#define GMC_CTRL_FEND           0x80  // Frame end
#define GMC_CTRL_ACK            0x40  // ACK required
#define GMC_CTRL_FRAGMENT       0x20  // Fragment flag (bit5)
// MSG_SET values (bit3-bit1)
#define GMC_CTRL_MSG_RESET      0x00  // Reset all links (00)
#define GMC_CTRL_MSG_NORMAL     0x01  // Normal message (01)
#define GMC_CTRL_MSG_UPGRADE    0x02  // Upgrade package (02)
#define GMC_CTRL_MSG_RESERVED   0x03  // Reserved (03)

// MTU sizes
#define GMC_MTU_BT              150   // Bluetooth MTU (recommended)
#define GMC_MTU_NETWORK         1200  // Network MTU

// Message types
#define GMC_TYPE_PLAY_CONTROL       "playControl"
#define GMC_TYPE_AUDIO_CONTROL      "audioControl"
#define GMC_TYPE_SETTINGS           "settings"
#define GMC_TYPE_DEVICE_CONTROL     "deviceControl"
#define GMC_TYPE_SHARE_AUDIO        "shareAudio"
#define GMC_TYPE_MAIL               "mail"
#define GMC_TYPE_RECORDER           "recorder"

// Message modes
#define GMC_MODE_READ               "read"
#define GMC_MODE_WRITE              "write"
#define GMC_MODE_NOTIFY             "notify"

// GMC frame structure
typedef struct {
    uint8_t compressed;     // 0-uncompressed, 1-compressed
    uint8_t comm_layer;     // Communication layer type
    uint8_t encryption;     // Encryption type
    uint8_t format;         // Data format
    uint8_t version;        // Protocol version
} gmc_packet_type_t;

typedef struct {
    uint8_t fend;           // Frame end flag
    uint8_t ack;            // ACK required flag
    uint8_t fragment;       // Fragment flag (bit5)
    uint8_t msg_set;        // Message set type
} gmc_control_t;

typedef struct {
    uint32_t header;        // 0x00000001
    gmc_packet_type_t type;
    gmc_control_t control;
    uint16_t frame_id;
    uint16_t packet_id;
    uint16_t data_len;
    uint8_t *data;
    uint32_t crc32;
} gmc_frame_t;

// Callback function types
typedef void (*gmc_rx_callback_t)(const gmc_frame_t *frame, const cJSON *json);
typedef void (*gmc_ack_callback_t)(uint16_t frame_id, bool success);

/**
 * @brief Initialize GMC protocol
 * 
 * @param comm_layer Communication layer type (GMC_COMM_UART/BT/NETWORK)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_protocol_init(uint8_t comm_layer);

/**
 * @brief Register callback for received frames
 * 
 * @param callback Function to call when frame is received
 */
void gmc_register_rx_callback(gmc_rx_callback_t callback);

/**
 * @brief Register callback for ACK responses
 * 
 * @param callback Function to call when ACK is received
 */
void gmc_register_ack_callback(gmc_ack_callback_t callback);

/**
 * @brief Log GMC JSON payload from a raw frame buffer (for forwarding diagnostics)
 *
 * @param data Pointer to raw GMC frame bytes
 * @param len Length of raw data
 * @param direction Direction label for logging (e.g., "AUDIO_TX", "AUDIO_RX_NOTIFY")
 */
void gmc_log_json_from_frame(const uint8_t *data, size_t len, const char *direction);


/**
 * @brief Send JSON message
 * 
 * @param mode Mode: "read", "write", or "notify"
 * @param type Message type
 * @param cmd Command
 * @param data_json Data JSON object (can be NULL)
 * @param need_ack Whether ACK is required
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_send_message(const char *mode, const char *type, const char *cmd, 
                           const cJSON *data_json, bool need_ack);

/**
 * @brief Send JSON message to BLE audio peripheral only (no phone forwarding)
 *
 * @param mode Mode: "read", "write", or "notify"
 * @param type Message type
 * @param cmd Command
 * @param data_json Data JSON object (can be NULL)
 * @param need_ack Whether ACK is required
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_send_message_to_audio(const char *mode, const char *type, const char *cmd,
                                    const cJSON *data_json, bool need_ack);

/**
 * @brief Send response message
 * 
 * @param mode Message mode (e.g., "notify", "read", "write")
 * @param type Message type (e.g., "settings", "audioControl", etc.)
 * @param cmd Command being responded to
 * @param code Response code (0 = success, -1 = failure)
 * @param msg Message string
 * @param data_json Data JSON object (can be NULL)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_send_response(const char *mode, const char *type, const char *cmd, int code, const char *msg, const cJSON *data_json);

/**
 * @brief Send ACK frame
 * 
 * @param frame_id Frame ID to acknowledge
 * @param success true if received successfully, false otherwise
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_send_ack(uint16_t frame_id, bool success);

/**
 * @brief Calculate CRC32
 * 
 * @param data Data buffer
 * @param length Length of data
 * @return uint32_t CRC32 value
 */
uint32_t gmc_crc32(const uint8_t *data, size_t length);

// Convenience functions for specific commands

/**
 * @brief Send input source list response
 */
esp_err_t gmc_send_input_source_list(const uint8_t *types, const char **names, uint8_t count);

/**
 * @brief Send current input source response
 */
esp_err_t gmc_send_current_input_source(uint8_t source_type);

/**
 * @brief Set input source
 */
esp_err_t gmc_set_input_source(uint8_t source_type);

/**
 * @brief Send volume changed notification
 */
esp_err_t gmc_notify_volume_changed(uint8_t channel_id, uint8_t volume);

/**
 * @brief Send device information response
 */
esp_err_t gmc_send_device_info(const char *name, const char *model, const char *sn,
                                const char *version_name, uint8_t version_code, const char *mac);

#ifdef __cplusplus
}
#endif

#endif // GMC_PROTOCOL_H
