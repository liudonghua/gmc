#include <string.h>
#include <stdlib.h>

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "cJSON.h"
#include "gmc_protocol.h"
#include "gmc_bt.h"
#include "gmc_network.h"
#include "gmc_uart.h"

static const char *TAG = "gmc_protocol";

#ifndef GMC_LOG_MAX_JSON_CHARS
#define GMC_LOG_MAX_JSON_CHARS 1024
#endif

// JSON分包大小限制：130字节
#define GMC_MAX_JSON_FRAGMENT_SIZE 130

// 重组缓冲区（用于接收分包的JSON）
typedef struct {
    uint16_t frame_id;      // 帧ID
    uint16_t total_packets; // 预期包总数（根据接收到的数据动态调整）
    uint16_t received_mask; // 已接收包的位掩码（最多支持16个分包）
    uint8_t *buffer;        // 重组缓冲区
    size_t buffer_size;     // 缓冲区大小
    size_t current_length;  // 当前已接收数据长度
    uint32_t last_update;   // 最后更新时间（用于超时检测）
} json_reassembly_t;

static uint8_t comm_layer_type = GMC_COMM_NETWORK;
static uint16_t current_frame_id = 0;
static SemaphoreHandle_t tx_mutex = NULL;
static gmc_rx_callback_t rx_callback = NULL;
static gmc_ack_callback_t ack_callback = NULL;

// JSON重组缓冲区（最多同时处理4个不同的分包序列）
#define GMC_MAX_REASSEMBLY_BUFFERS 4
static json_reassembly_t reassembly_buffers[GMC_MAX_REASSEMBLY_BUFFERS];
static SemaphoreHandle_t reassembly_mutex = NULL;

static void log_gmc_json_payload_rx(const uint8_t *payload, uint16_t payload_len)
{
    if (payload == NULL || payload_len == 0)
    {
        return;
    }

    // Logging enabled even if Audio is connected to distinguish traffic
    
    size_t log_len = payload_len;
    bool truncated = false;
    if (log_len > GMC_LOG_MAX_JSON_CHARS)
    {
        log_len = GMC_LOG_MAX_JSON_CHARS;
        truncated = true;
    }

    char *buf = (char *)malloc(log_len + 1);
    if (buf == NULL)
    {
        ESP_LOGW(TAG, "PHONE_RX JSON (len=%u) <no-mem>", (unsigned)payload_len);
        return;
    }

    memcpy(buf, payload, log_len);
    buf[log_len] = '\0';

    if (truncated)
    {
        ESP_LOGI(TAG, "📥 PHONE_RX JSON (len=%u, truncated to %u):\n%s...",
                 (unsigned)payload_len, (unsigned)log_len, buf);
    }
    else
    {
        ESP_LOGI(TAG, "📥 PHONE_RX JSON (len=%u):\n%s", (unsigned)payload_len, buf);
    }

    free(buf);
}

static void log_gmc_json_payload_direction(const uint8_t *payload, uint16_t payload_len, const char *direction)
{
    if (payload == NULL || payload_len == 0 || direction == NULL)
    {
        return;
    }

    size_t log_len = payload_len;
    bool truncated = false;

    if (log_len > GMC_LOG_MAX_JSON_CHARS)
    {
        log_len = GMC_LOG_MAX_JSON_CHARS;
        truncated = true;
    }

    char *buf = (char *)malloc(log_len + 1);
    if (buf == NULL)
    {
        ESP_LOGW(TAG, "%s JSON (len=%u) <no-mem>", direction, (unsigned)payload_len);
        return;
    }

    memcpy(buf, payload, log_len);
    buf[log_len] = '\0';

    if (truncated)
    {
        ESP_LOGI(TAG, "%s JSON (len=%u, truncated to %u):\n%s...",
                 direction, (unsigned)payload_len, (unsigned)log_len, buf);
    }
    else
    {
        ESP_LOGI(TAG, "%s JSON (len=%u):\n%s", direction, (unsigned)payload_len, buf);
    }

    free(buf);
}

// Forward declarations for endian helpers used below
static inline uint16_t read_u16_le(const uint8_t *p);
static inline uint16_t read_u16_be(const uint8_t *p);
static inline uint32_t read_u32_le(const uint8_t *p);

void gmc_log_json_from_frame(const uint8_t *data, size_t len, const char *direction)
{
    if (data == NULL || len == 0 || direction == NULL)
    {
        return;
    }

    const size_t min_frame_size = GMC_MIN_FRAME_SIZE;
    if (len < min_frame_size)
    {
        return;
    }

    size_t offset = 0;
    uint32_t header = read_u32_le(&data[offset]);
    offset += 4;

    if (header != GMC_HEADER)
    {
        return;
    }

    uint8_t type_byte = data[offset++];
    uint8_t ctrl_byte = data[offset++];

    (void)ctrl_byte;

    // Need: FrameID(1) + PacketID(2) + Reserved(2) + DataLen(2)
    if (len < offset + 1 + 2 + 2 + 2)
    {
        return;
    }

    uint16_t frame_id = data[offset++];
    (void)frame_id;

    uint16_t packet_id = read_u16_le(&data[offset]);
    offset += 2;
    (void)packet_id;

    // Reserved (2 bytes)
    offset += 2;

    // Data length (2 bytes, little-endian)
    uint16_t data_len = read_u16_le(&data[offset]);
    offset += 2;

    if (len < offset + data_len)
    {
        return;
    }

    const uint8_t *payload = &data[offset];
    uint16_t json_len = data_len;

    bool is_json = (type_byte & 0x04) != 0;
    const uint8_t *json_payload = payload;
    while (json_len > 0 && (json_payload[0] == 0x00 || json_payload[0] == ' ' ||
                            json_payload[0] == '\r' || json_payload[0] == '\n' || json_payload[0] == '\t'))
    {
        json_payload++;
        json_len--;
    }
    while (json_len > 0 && (json_payload[json_len - 1] == 0x00))
    {
        json_len--;
    }
    if (!is_json && json_len > 0 && (json_payload[0] == '{' || json_payload[0] == '['))
    {
        is_json = true;
    }

    if (is_json && json_len > 0)
    {
        log_gmc_json_payload_direction(json_payload, json_len, direction);
    }
}


// CRC32 lookup table
static const uint32_t crc32_table[] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
    0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
    0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
    0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
    0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
    0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
    0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
    0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
    0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
    0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
    0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
    0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
    0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
    0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
    0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
    0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
    0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
    0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
    0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
    0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
    0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
    0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d};

uint32_t gmc_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++)
    {
        crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFF];
    }
    return ~crc;
}

static uint8_t build_packet_type(bool compressed, uint8_t comm, uint8_t enc, uint8_t fmt, uint8_t ver)
{
    uint8_t type = 0;
    type |= (compressed & 0x01) << 7;
    type |= (comm & 0x03) << 5;
    type |= (enc & 0x03) << 3;
    type |= (fmt & 0x01) << 2;
    type |= (ver & 0x03);
    return type;
}

static uint8_t build_control(bool fend, bool ack, bool fragment, uint8_t msg_set)
{
    uint8_t ctrl = 0;
    ctrl |= (fend & 0x01) << 7;       // Bit7: FEND
    ctrl |= (ack & 0x01) << 6;        // Bit6: ACK
    ctrl |= (fragment & 0x01) << 5;   // Bit5: FRAGMENT
    ctrl |= (msg_set & 0x07) << 1;    // Bit3-Bit1: MSG_SET (3 bits)
    return ctrl;
}

// BLE receive buffer for frame parsing
// Commented out: unused when using BLE communication
// typedef struct
// {
//     uint8_t *buffer;
//     size_t size;
//     size_t offset;
//     bool parsing;
// } gmc_rx_buffer_t;

// static gmc_rx_buffer_t rx_buffer = {
//     .buffer = NULL,
//     .size = 0,
//     .offset = 0,
//     .parsing = false};

static inline uint16_t read_u16_le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint16_t read_u16_be(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

static inline uint32_t read_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void write_u16_le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static inline void write_u16_be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)((v >> 8) & 0xFF);
    p[1] = (uint8_t)(v & 0xFF);
}

static inline void write_u32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

// JSON分包重组辅助函数

// 查找或创建重组缓冲区 (需在持有reassembly_mutex时调用)
static json_reassembly_t* find_or_create_reassembly_buffer_nolock(uint16_t frame_id)
{
    json_reassembly_t *free_slot = NULL;
    json_reassembly_t *oldest_slot = NULL;
    uint32_t oldest_time = 0xFFFFFFFF;

    // 查找已存在的缓冲区或空闲槽位
    for (int i = 0; i < GMC_MAX_REASSEMBLY_BUFFERS; i++)
    {
        if (reassembly_buffers[i].buffer != NULL && reassembly_buffers[i].frame_id == frame_id)
        {
            return &reassembly_buffers[i];
        }
        if (reassembly_buffers[i].buffer == NULL && free_slot == NULL)
        {
            free_slot = &reassembly_buffers[i];
        }
        if (reassembly_buffers[i].buffer != NULL && reassembly_buffers[i].last_update < oldest_time)
        {
            oldest_time = reassembly_buffers[i].last_update;
            oldest_slot = &reassembly_buffers[i];
        }
    }

    // 使用空闲槽位或最旧的槽位
    json_reassembly_t *slot = free_slot ? free_slot : oldest_slot;
    if (slot)
    {
        // 清理旧数据
        if (slot->buffer != NULL)
        {
            free(slot->buffer);
        }
        memset(slot, 0, sizeof(json_reassembly_t));
        slot->frame_id = frame_id;
        slot->last_update = xTaskGetTickCount();
    }

    return slot;
}

// 释放重组缓冲区 (需在持有reassembly_mutex时调用)
static void free_reassembly_buffer_nolock(json_reassembly_t *buffer)
{
    if (buffer == NULL) return;

    if (buffer->buffer != NULL)
    {
        free(buffer->buffer);
    }
    memset(buffer, 0, sizeof(json_reassembly_t));
}

// Parse received GMC frame (Little-Endian)
static void parse_gmc_frame(const uint8_t *data, size_t len)
{
    const size_t min_frame_size = GMC_MIN_FRAME_SIZE;
    if (len < min_frame_size)
    {
        ESP_LOGW(TAG, "Frame too short: %d bytes", len);
        return;
    }

    // Print received binary data for debugging
    ESP_LOGI(TAG, "📥 RX frame: size=%d", len);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_INFO);

    size_t offset = 0;

    // Parse header (4 bytes, little-endian). On-wire bytes for 0x00000001 are: 01 00 00 00
    uint32_t header = read_u32_le(&data[offset]);
    offset += 4;

    if (header != GMC_HEADER)
    {
        // Raw JSON-over-BLE is not supported; require GMC framed messages.
        // (Keep GMC framed JSON payload support below.)
        if (comm_layer_type == GMC_COMM_BT && (data[0] == '{' || data[0] == '['))
        {
            ESP_LOGW(TAG, "Raw JSON over BLE is disabled; expected GMC frame header 0x%08lX", (unsigned long)GMC_HEADER);
            return;
        }

        ESP_LOGW(TAG, "Invalid header: 0x%08lX", header);
        return;
    }

    // Parse packet type
    uint8_t type_byte = data[offset++];

    // Parse control
    uint8_t ctrl_byte = data[offset++];

    // Parse frame ID (1 byte)
    uint16_t frame_id = data[offset++];

    // Parse packet ID (2 bytes, little-endian)
    uint16_t packet_id = read_u16_le(&data[offset]);
    offset += 2;

    // Parse reserved bytes (2 bytes)
    // Reserved for future use; currently ignored.
    offset += 2;

    // Parse data length (2 bytes, little-endian)
    uint16_t data_len = read_u16_le(&data[offset]);
    offset += 2;
    
    ESP_LOGI(TAG, "   Parse: FrameID=%d, PktID=%d, ctrl=0x%02X (fend=%d, ack=%d, msg_set=%d), type=0x%02X, len=%d",
             frame_id, packet_id, ctrl_byte, 
             (ctrl_byte >> 7) & 0x01, (ctrl_byte >> 6) & 0x01, (ctrl_byte >> 1) & 0x07,
             type_byte, data_len);

    // Check if we have enough data for payload
    if (len < offset + data_len)
    {
        ESP_LOGW(TAG, "Incomplete frame: expected >=%d, got %d", offset + data_len, len);
        return;
    }

    // Extract data payload
    const uint8_t *payload = &data[offset];
    offset += data_len;

    if (!gmc_bt_audio_connected())
    {
        ESP_LOGI(TAG, "Received valid frame: ID=%d, len=%d", frame_id, data_len);
    }

    // Check if JSON format.
    // Primary: format bit in type_byte. Fallback: detect by payload leading character.
    bool is_json = (type_byte & 0x04) != 0;
    const uint8_t *json_payload = payload;
    uint16_t json_len = data_len;
    while (json_len > 0 && (json_payload[0] == 0x00 || json_payload[0] == ' ' ||
                            json_payload[0] == '\r' || json_payload[0] == '\n' || json_payload[0] == '\t'))
    {
        json_payload++;
        json_len--;
    }
    while (json_len > 0 && (json_payload[json_len - 1] == 0x00))
    {
        json_len--;
    }
    if (!is_json && json_len > 0 && (json_payload[0] == '{' || json_payload[0] == '['))
    {
        is_json = true;
    }

    // 提取控制位
    bool fend = (ctrl_byte & 0x80) != 0;
    bool need_ack = (ctrl_byte & 0x40) != 0;

    if (is_json && rx_callback != NULL)
    {
        // 检查是否为分包数据
        if (!fend || packet_id > 0)
        {
            // 这是一个分包，需要重组
            ESP_LOGD(TAG, "接收JSON分包: frame_id=%u packet_id=%u fend=%u len=%u",
                     frame_id, packet_id, fend, json_len);

            if (xSemaphoreTake(reassembly_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
            {
                ESP_LOGW(TAG, "无法获取重组锁");
                if (need_ack) gmc_send_ack(frame_id, false);
                return;
            }

            json_reassembly_t *reassembly = find_or_create_reassembly_buffer_nolock(frame_id);
            if (reassembly == NULL)
            {
                xSemaphoreGive(reassembly_mutex);
                ESP_LOGW(TAG, "无法创建重组缓冲区");
                if (need_ack)
                {
                    gmc_send_ack(frame_id, false);
                }
                return;
            }

            // 计算需要的缓冲区大小
            size_t required_size = (packet_id + 1) * GMC_MAX_JSON_FRAGMENT_SIZE + json_len;
            if (reassembly->buffer == NULL || reassembly->buffer_size < required_size)
            {
                size_t new_size = required_size + GMC_MAX_JSON_FRAGMENT_SIZE * 2;
                uint8_t *new_buffer = (uint8_t *)realloc(reassembly->buffer, new_size);
                if (new_buffer == NULL)
                {
                    ESP_LOGE(TAG, "重组缓冲区内存分配失败");
                    free_reassembly_buffer_nolock(reassembly);
                    xSemaphoreGive(reassembly_mutex);
                    if (need_ack)
                    {
                        gmc_send_ack(frame_id, false);
                    }
                    return;
                }
                reassembly->buffer = new_buffer;
                reassembly->buffer_size = new_size;
            }

            // 将数据复制到对应位置
            size_t offset = packet_id * GMC_MAX_JSON_FRAGMENT_SIZE;
            memcpy(reassembly->buffer + offset, json_payload, json_len);
            
            // 标记这个包已接收
            if (packet_id < 16)
            {
                reassembly->received_mask |= (1 << packet_id);
            }

            // 更新长度
            size_t end_offset = offset + json_len;
            if (end_offset > reassembly->current_length)
            {
                reassembly->current_length = end_offset;
            }

            reassembly->last_update = xTaskGetTickCount();

            // 如果是最后一个包（fend=1），尝试解析完整JSON
            if (fend)
            {
                ESP_LOGI(TAG, "接收到最后一个分包, 开始重组JSON (总长度=%u)", 
                         (unsigned)reassembly->current_length);

                log_gmc_json_payload_rx(reassembly->buffer, reassembly->current_length);

                // 解析完整的JSON (在锁内解析，以保护buffer不被释放)
                cJSON *json = cJSON_ParseWithLength((const char *)reassembly->buffer, 
                                                    reassembly->current_length);
                
                // 释放重组缓冲区 (释放buffer指针)
                free_reassembly_buffer_nolock(reassembly);
                
                // 释放锁
                xSemaphoreGive(reassembly_mutex);

                if (json != NULL)
                {
                    // 创建临时frame结构用于回调
                    gmc_frame_t frame = {
                        .header = header,
                        .frame_id = frame_id,
                        .packet_id = 0,
                        .data_len = 0, // 重组后数据不在frame payload中
                        .data = NULL};

                    rx_callback(&frame, json);
                    cJSON_Delete(json);

                    // 发送ACK（成功）
                    if (need_ack)
                    {
                        gmc_send_ack(frame_id, true);
                    }
                }
                else
                {
                    ESP_LOGW(TAG, "重组后的JSON解析失败");
                    if (need_ack)
                    {
                        gmc_send_ack(frame_id, false);
                    }
                }
            }
            else
            {
                // 不是最后一个包，等待更多分包
                ESP_LOGD(TAG, "等待更多分包 (当前已接收=%u字节)", 
                         (unsigned)reassembly->current_length);
                xSemaphoreGive(reassembly_mutex);
            }
        }
        else
        {
            // 单个完整的JSON包（未分包）
            log_gmc_json_payload_rx(json_payload, json_len);

            // Parse JSON data
            cJSON *json = cJSON_ParseWithLength((const char *)json_payload, json_len);
            if (json != NULL)
            {
                gmc_frame_t frame = {
                    .header = header,
                    .frame_id = frame_id,
                    .packet_id = packet_id,
                    .data_len = json_len,
                    .data = (uint8_t *)json_payload};

                rx_callback(&frame, json);
                cJSON_Delete(json);

                // 发送ACK
                if (need_ack)
                {
                    gmc_send_ack(frame_id, true);
                }
            }
            else
            {
                ESP_LOGW(TAG, "Failed to parse JSON");
                if (need_ack)
                {
                    gmc_send_ack(frame_id, false);
                }
            }
        }
    }
    else if (!is_json)
    {
        // 非JSON数据，按原样处理
        if (need_ack)
        {
            gmc_send_ack(frame_id, true);
        }
    }
}

// BLE receive callback
static void gmc_bt_rx_handler(const uint8_t *data, size_t len)
{
    // ESP_LOGI(TAG, "BLE RX: %d bytes", len);
    // ESP_LOG_BUFFER_HEX(TAG, data, len);

    // Forward raw GMC data to audio peripheral only when connected (Central role)
    if (gmc_bt_audio_connected())
    {
        // gmc_bt_send_to_audio will log "AUDIO_TX" internally
        esp_err_t fwd_ret = gmc_bt_send_to_audio(data, len);
        if (fwd_ret != ESP_OK)
        {
            ESP_LOGD(TAG, "Forward to audio device rejected or failed: %s", esp_err_to_name(fwd_ret));
        }
    }

    // For simplicity, assume each BLE packet contains a complete GMC frame
    // In production, you may need to handle fragmentation
    parse_gmc_frame(data, len);
}
// Commented out: unused when using BLE communication
// Network receive callback
// static void gmc_net_rx_handler(const uint8_t *data, size_t len,
//                                const char *remote_ip, uint16_t remote_port)
// {
//     ESP_LOGI(TAG, "Network RX: %d bytes from %s:%d", len, remote_ip, remote_port);
//     ESP_LOG_BUFFER_HEX(TAG, data, len);
//
//     // Parse received GMC frame
//     parse_gmc_frame(data, len);
// }

// Network connection callback
// static void gmc_net_conn_handler(bool connected, const char *remote_ip, uint16_t remote_port)
// {
//     if (connected)
//     {
//         ESP_LOGI(TAG, "Network connected: %s:%d", remote_ip, remote_port);
//     }
//     else
//     {
//         ESP_LOGI(TAG, "Network disconnected: %s:%d", remote_ip, remote_port);
//     }
// }
esp_err_t gmc_protocol_init(uint8_t comm_layer)
{
    comm_layer_type = comm_layer;
    current_frame_id = 0;

    tx_mutex = xSemaphoreCreateMutex();
    if (tx_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // 初始化重组缓冲区mutex
    reassembly_mutex = xSemaphoreCreateMutex();
    if (reassembly_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create reassembly mutex");
        return ESP_ERR_NO_MEM;
    }

    // 初始化重组缓冲区
    memset(reassembly_buffers, 0, sizeof(reassembly_buffers));

    // Initialize BLE transport if needed
    if (comm_layer == GMC_COMM_BT)
    {
        ESP_LOGW(TAG, "Before BLE init: free_internal=%u free_psram=%u min_free_internal=%u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                 (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));

        esp_err_t ret = gmc_bt_init();
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "BLE init failed: %s", esp_err_to_name(ret));
            return ret;
        }

        // Register BLE receive callback
        gmc_bt_register_rx_callback(gmc_bt_rx_handler);

        // Advertising is started from the NimBLE sync callback inside gmc_bt_init().
        // Calling gmc_bt_start_advertising() here as well can cause rapid stop/start and
        // increase the chance of ENOMEM / BLE_ERR_MEM_CAPACITY on low-heap systems.
    }
    // Initialize Network transport if needed
    else if (comm_layer == GMC_COMM_NETWORK)
    {
        // Network initialization is done separately via gmc_net_init()
        // because network config needs to be provided by application
        ESP_LOGI(TAG, "Network transport layer selected");
        ESP_LOGI(TAG, "Call gmc_net_init() and gmc_net_start() to activate");
    }

    ESP_LOGI(TAG, "GMC protocol initialized (comm_layer=%d)", comm_layer);
    return ESP_OK;
}

void gmc_register_rx_callback(gmc_rx_callback_t callback)
{
    rx_callback = callback;
}

void gmc_register_ack_callback(gmc_ack_callback_t callback)
{
    ack_callback = callback;
}

static esp_err_t send_frame(const gmc_frame_t *frame, bool forward_to_audio)
{
    if (frame == NULL || frame->data == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // Calculate total frame size (no CRC)
    // Frame format (13-byte header):
    // Header(4) + Type(1) + Ctrl(1) + FrameID(1) + PktID(2) + Reserved(2) + DataLen(2) + Payload(data_len)
    size_t frame_size = GMC_HEADER_SIZE + 1 + 1 + 1 + 2 + 2 + 2 + frame->data_len;
    uint8_t *buffer = (uint8_t *)heap_caps_malloc(frame_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL)
    {
        buffer = (uint8_t *)malloc(frame_size);
    }
    if (buffer == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        return ESP_ERR_NO_MEM;
    }

    size_t offset = 0;

    // Header (4 bytes, little-endian)
    write_u32_le(&buffer[offset], frame->header);
    offset += 4;

    // Packet type (1 byte)
    buffer[offset++] = build_packet_type(frame->type.compressed, frame->type.comm_layer,
                                         frame->type.encryption, frame->type.format,
                                         frame->type.version);

    // Control (1 byte)
    buffer[offset++] = build_control(frame->control.fend, frame->control.ack,
                                     frame->control.fragment, frame->control.msg_set);

    // Frame ID (1 byte)
    buffer[offset++] = (uint8_t)(frame->frame_id & 0xFF);

    // Packet ID (2 bytes, little-endian)
    write_u16_le(&buffer[offset], frame->packet_id);
    offset += 2;

    // Reserved (2 bytes)
    buffer[offset++] = 0x00;
    buffer[offset++] = 0x00;

    // Data length (2 bytes, little-endian)
    write_u16_le(&buffer[offset], frame->data_len);
    offset += 2;

    // Data
    memcpy(&buffer[offset], frame->data, frame->data_len);
    offset += frame->data_len;

    // CRC32 DISABLED - Phone does not use CRC32 in BLE communication
    // uint32_t crc = gmc_crc32(buffer, offset);
    // write_u32_le(&buffer[offset], crc);
    // offset += 4;

    // Print binary data for debugging
    ESP_LOGD(TAG, "📤 TX frame: ID=%d, PktID=%d, len=%d, ctrl=0x%02X, size=%d",
             frame->frame_id, frame->packet_id, frame->data_len, 
             buffer[5], frame_size);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, buffer, frame_size, ESP_LOG_DEBUG);

    esp_err_t ret = ESP_OK;

    if (comm_layer_type == GMC_COMM_BT)
    {
        if (!gmc_bt_can_send())
        {
            ESP_LOGW(TAG, "BLE not ready (not connected or notifications disabled)");
            ret = ESP_ERR_INVALID_STATE;
        }
        else
        {
            ret = gmc_bt_send(buffer, frame_size);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "BLE send failed: %s", esp_err_to_name(ret));
            }
        }

        // Also forward the same GMC frame to audio peripheral only when connected
        // AND explicitly requested (e.g. for user-initiated Commands, but not Responses)
        if (forward_to_audio && gmc_bt_audio_connected())
        {
            esp_err_t fwd_ret = gmc_bt_send_to_audio(buffer, frame_size);
            if (fwd_ret != ESP_OK)
            {
                ESP_LOGD(TAG, "Forward to audio failed: %s", esp_err_to_name(fwd_ret));
            }
        }
    }
    else if (comm_layer_type == GMC_COMM_NETWORK)
    {
        // Send via network (TCP/UDP)
        gmc_net_state_t net_state = gmc_net_get_state();
        if (net_state == GMC_NET_STATE_CONNECTED || net_state == GMC_NET_STATE_LISTENING)
        {
            ret = gmc_net_send(buffer, frame_size, NULL, 0);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "Network send failed: %s", esp_err_to_name(ret));
            }
        }
        else
        {
            ESP_LOGW(TAG, "Network not connected");
            ret = ESP_ERR_INVALID_STATE;
        }
    }
    else if (comm_layer_type == GMC_COMM_UART)
    {
        // UART transport
        if (gmc_uart_get_state() == GMC_UART_STATE_READY)
        {
            ret = gmc_uart_send(buffer, frame_size);
        }
        else
        {
            ESP_LOGW(TAG, "UART not ready");
            ret = ESP_ERR_INVALID_STATE;
        }
    }
    else
    {
        ESP_LOGW(TAG, "No active transport layer");
        ret = ESP_ERR_INVALID_STATE;
    }

    free(buffer);
    return ret;
}

esp_err_t gmc_send_message(const char *mode, const char *type, const char *cmd,
                           const cJSON *data_json, bool need_ack)
{
    if (mode == NULL || type == NULL || cmd == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "mode", mode);
    cJSON_AddStringToObject(root, "type", type);
    cJSON_AddStringToObject(root, "cmd", cmd);

    if (data_json != NULL)
    {
        cJSON_AddItemToObject(root, "data", cJSON_Duplicate(data_json, true));
    }

    char *json_compact = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_compact == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    const size_t json_len = strlen(json_compact);
    
    {
        size_t log_len = json_len;
        if (log_len > GMC_LOG_MAX_JSON_CHARS)
        {
            log_len = GMC_LOG_MAX_JSON_CHARS;
        }
        const bool truncated = (json_len > log_len);

        // 判断是否为通知模式
        const bool is_notify = (strcmp(mode, GMC_MODE_NOTIFY) == 0);

        ESP_LOGI(TAG,
                 "%sPHONE_TX JSON (mode=%s type=%s cmd=%s len=%u%s):\n%.*s%s",
                 is_notify ? "[NOTIFY] " : "",
                 mode,
                 type,
                 cmd,
                 (unsigned)json_len,
                 truncated ? " truncated" : "",
                 (int)log_len,
                 json_compact,
                 truncated ? "..." : "");
    }

    if (xSemaphoreTake(tx_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        free(json_compact);
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;

    // 检查是否需要分包
    if (json_len <= GMC_MAX_JSON_FRAGMENT_SIZE)
    {
        // 数据不超过130字节，直接发送单个包
        gmc_frame_t frame = {
            .header = GMC_HEADER,
            .type = {
                .compressed = 0,
                .comm_layer = comm_layer_type,
                .encryption = GMC_ENC_NONE,
                .format = GMC_FORMAT_BINARY,  // format=0 for data[4]=0x22
                .version = GMC_VERSION_2},     // version=2 for data[4]=0x22
            .control = {.fend = 1, .ack = 1, .fragment = 0, .msg_set = GMC_CTRL_MSG_NORMAL},  // 单包不分片, 0xC2
            .frame_id = current_frame_id++,
            .packet_id = 0,
            .data_len = json_len,
            .data = (uint8_t *)json_compact};

        ret = send_frame(&frame, true);
    }
    else
    {
        // 数据超过130字节，需要分包发送
        uint16_t frame_id = current_frame_id++;
        uint16_t total_packets = (json_len + GMC_MAX_JSON_FRAGMENT_SIZE - 1) / GMC_MAX_JSON_FRAGMENT_SIZE;
        
        ESP_LOGD(TAG, "JSON分包发送: 总长度=%u 字节, 分成 %u 个包", (unsigned)json_len, total_packets);

        for (uint16_t packet_id = 0; packet_id < total_packets; packet_id++)
        {
            size_t offset = packet_id * GMC_MAX_JSON_FRAGMENT_SIZE;
            size_t fragment_len = json_len - offset;
            if (fragment_len > GMC_MAX_JSON_FRAGMENT_SIZE)
            {
                fragment_len = GMC_MAX_JSON_FRAGMENT_SIZE;
            }

            // 最后一个包fend=1，其他包fend=0
            uint8_t fend = (packet_id == total_packets - 1) ? 1 : 0;
            uint8_t fragment = fend ? 0 : 1;  // 未结束时fragment=1
            uint8_t msg_set = fend ? GMC_CTRL_MSG_NORMAL : GMC_CTRL_MSG_RESERVED;  // 0xC2/0x66

            gmc_frame_t frame = {
                .header = GMC_HEADER,
                .type = {
                    .compressed = 0,
                    .comm_layer = comm_layer_type,
                    .encryption = GMC_ENC_NONE,
                    .format = GMC_FORMAT_BINARY,  // format=0 for data[4]=0x22
                    .version = GMC_VERSION_2},     // version=2 for data[4]=0x22
                .control = {.fend = fend, .ack = 1, .fragment = fragment, .msg_set = msg_set},  // 0xC2/0x66
                .frame_id = frame_id,
                .packet_id = packet_id,
                .data_len = fragment_len,
                .data = (uint8_t *)(json_compact + offset)};

            ESP_LOGD(TAG, "发送分包 %u/%u: fend=%u fragment_len=%u", 
                     packet_id + 1, total_packets, fend, (unsigned)fragment_len);

            ret = send_frame(&frame, true);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "发送分包 %u/%u 失败", packet_id + 1, total_packets);
                break;
            }

            // 添加小延迟以避免接收方缓冲区溢出
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    xSemaphoreGive(tx_mutex);
    free(json_compact);

    return ret;
}

esp_err_t gmc_send_message_to_audio(const char *mode, const char *type, const char *cmd,
                                    const cJSON *data_json, bool need_ack)
{
    if (mode == NULL || type == NULL || cmd == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!gmc_bt_audio_connected())
    {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "mode", mode);
    cJSON_AddStringToObject(root, "type", type);
    cJSON_AddStringToObject(root, "cmd", cmd);

    if (data_json != NULL)
    {
        cJSON_AddItemToObject(root, "data", cJSON_Duplicate(data_json, true));
    }

    char *json_compact = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_compact == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    gmc_frame_t frame = {
        .header = GMC_HEADER,
        .type = {
            .compressed = 0,
            .comm_layer = comm_layer_type,
            .encryption = GMC_ENC_NONE,
            .format = GMC_FORMAT_BINARY,  // format=0 for data[4]=0x22
            .version = GMC_VERSION_2},     // version=2 for data[4]=0x22
        .control = {.fend = 1, .ack = 1, .fragment = 0, .msg_set = GMC_CTRL_MSG_NORMAL},  // 单包不分片, 0xC2
        .frame_id = current_frame_id++,
        .packet_id = 0,
        .data_len = strlen(json_compact),
        .data = (uint8_t *)json_compact};

    if (xSemaphoreTake(tx_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        free(json_compact);
        return ESP_ERR_TIMEOUT;
    }

    // Build raw GMC frame buffer (no CRC)
    // Header(4) + Type(1) + Ctrl(1) + FrameID(1) + PktID(2) + Reserved(2) + DataLen(2) + JSON
    size_t frame_size = GMC_HEADER_SIZE + 1 + 1 + 1 + 2 + 2 + 2 + frame.data_len;
    uint8_t *buffer = (uint8_t *)heap_caps_malloc(frame_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == NULL)
    {
        buffer = (uint8_t *)malloc(frame_size);
    }
    if (buffer == NULL)
    {
        xSemaphoreGive(tx_mutex);
        free(json_compact);
        ESP_LOGE(TAG, "Failed to allocate buffer");
        return ESP_ERR_NO_MEM;
    }

    size_t offset = 0;
    write_u32_le(&buffer[offset], frame.header);
    offset += 4;
    buffer[offset++] = build_packet_type(frame.type.compressed, frame.type.comm_layer,
                                         frame.type.encryption, frame.type.format,
                                         frame.type.version);
    buffer[offset++] = build_control(frame.control.fend, frame.control.ack,
                                     frame.control.fragment, frame.control.msg_set);
    buffer[offset++] = (uint8_t)(frame.frame_id & 0xFF);
    write_u16_le(&buffer[offset], frame.packet_id);
    offset += 2;
    buffer[offset++] = 0x00;
    buffer[offset++] = 0x00;
    write_u16_le(&buffer[offset], frame.data_len);  // Little-endian
    offset += 2;
    
    // Copy JSON payload
    memcpy(&buffer[offset], frame.data, frame.data_len);
    offset += frame.data_len;

    // No CRC

    // ESP_LOGI(TAG, "TX to Audio: ID=%d, len=%d, size=%d",
    //          frame.frame_id, frame.data_len, frame_size);
    
    // Manually print hex data
    // printf("[gmc_protocol] TX Audio HEX: ");
    // for (size_t i = 0; i < frame_size; i++) {
    //     printf("%02X ", buffer[i]);
    //     if ((i + 1) % 16 == 0) printf("\n");
    // }
    // if (frame_size % 16 != 0) printf("\n");

    esp_err_t ret = gmc_bt_send_to_audio(buffer, frame_size);

    xSemaphoreGive(tx_mutex);
    free(buffer);
    free(json_compact);

    return ret;
}

esp_err_t gmc_send_response(const char *mode, const char *type, const char *cmd, int code, const char *msg, const cJSON *data_json)
{
    cJSON *response = cJSON_CreateObject();
    if (response == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    if (mode != NULL)
    {
        cJSON_AddStringToObject(response, "mode", mode);
    }
    if (type != NULL)
    {
        cJSON_AddStringToObject(response, "type", type);
    }
    cJSON_AddNumberToObject(response, "code", code);
    if (cmd != NULL)
    {
        cJSON_AddStringToObject(response, "cmd", cmd);
    }
    cJSON_AddStringToObject(response, "msg", msg ? msg : "");

    if (data_json != NULL)
    {
        cJSON_AddItemToObject(response, "data", cJSON_Duplicate(data_json, true));
    }
    else
    {
        cJSON_AddStringToObject(response, "data", "");
    }

    char *json_str = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);

    if (json_str == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    {
        const size_t json_len = strlen(json_str);
        size_t log_len = json_len;
        if (log_len > GMC_LOG_MAX_JSON_CHARS)
        {
            log_len = GMC_LOG_MAX_JSON_CHARS;
        }
        const bool truncated = (json_len > log_len);

        ESP_LOGI(TAG,
                 "PHONE_TX JSON (mode=%s type=%s cmd=%s code=%d len=%u%s):\n%.*s%s",
                 mode ? mode : "",
                 type ? type : "",
                 cmd ? cmd : "",
                 code,
                 (unsigned)json_len,
                 truncated ? " truncated" : "",
                 (int)log_len,
                 json_str,
                 truncated ? "..." : "");
    }

    if (xSemaphoreTake(tx_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        free(json_str);
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;
    const size_t json_len = strlen(json_str);

    // 检查是否需要分包
    if (json_len <= GMC_MAX_JSON_FRAGMENT_SIZE)
    {
        // 数据不超过130字节，直接发送单个包
        gmc_frame_t frame = {
            .header = GMC_HEADER,
            .type = {
                .compressed = 0,
                .comm_layer = comm_layer_type,
                .encryption = GMC_ENC_NONE,
                .format = GMC_FORMAT_BINARY,  // format=0 for data[4]=0x22
                .version = GMC_VERSION_2},     // version=2 for data[4]=0x22
            .control = {.fend = 1, .ack = 1, .fragment = 0, .msg_set = GMC_CTRL_MSG_NORMAL},  // 单包不分片, 0xC2
            .frame_id = current_frame_id++,
            .packet_id = 0,
            .data_len = json_len,
            .data = (uint8_t *)json_str};

        // Responses should ONLY go back to the requester (Phone), not forwarded to Audio Device
        ret = send_frame(&frame, false);
    }
    else
    {
        // 数据超过130字节，需要分包发送
        uint16_t frame_id = current_frame_id++;
        uint16_t total_packets = (json_len + GMC_MAX_JSON_FRAGMENT_SIZE - 1) / GMC_MAX_JSON_FRAGMENT_SIZE;
        
        ESP_LOGD(TAG, "Response JSON分包发送: 总长度=%u 字节, 分成 %u 个包", (unsigned)json_len, total_packets);

        for (uint16_t packet_id = 0; packet_id < total_packets; packet_id++)
        {
            size_t offset = packet_id * GMC_MAX_JSON_FRAGMENT_SIZE;
            size_t fragment_len = json_len - offset;
            if (fragment_len > GMC_MAX_JSON_FRAGMENT_SIZE)
            {
                fragment_len = GMC_MAX_JSON_FRAGMENT_SIZE;
            }

            // 最后一个包fend=1，其他包fend=0
            uint8_t fend = (packet_id == total_packets - 1) ? 1 : 0;
            uint8_t fragment = fend ? 0 : 1;  // 未结束时fragment=1
            uint8_t msg_set = fend ? GMC_CTRL_MSG_NORMAL : GMC_CTRL_MSG_RESERVED;  // 0xC2/0x66

            gmc_frame_t frame = {
                .header = GMC_HEADER,
                .type = {
                    .compressed = 0,
                    .comm_layer = comm_layer_type,
                    .encryption = GMC_ENC_NONE,
                    .format = GMC_FORMAT_BINARY,  // format=0 for data[4]=0x22
                    .version = GMC_VERSION_2},     // version=2 for data[4]=0x22
                .control = {.fend = fend, .ack = 1, .fragment = fragment, .msg_set = msg_set},  // 0xC2/0x66
                .frame_id = frame_id,
                .packet_id = packet_id,
                .data_len = fragment_len,
                .data = (uint8_t *)(json_str + offset)};

            ESP_LOGD(TAG, "发送Response分包 %u/%u: fend=%u fragment_len=%u", 
                     packet_id + 1, total_packets, fend, (unsigned)fragment_len);

            ret = send_frame(&frame, false);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "发送Response分包 %u/%u 失败", packet_id + 1, total_packets);
                break;
            }

            // 添加小延迟以避免接收方缓冲区溢出
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    xSemaphoreGive(tx_mutex);
    free(json_str);

    return ret;
}

esp_err_t gmc_send_ack(uint16_t frame_id, bool success)
{
    // ACK frame has minimal data
    uint8_t ack_data[1] = {success ? 0x01 : 0x00};

    gmc_frame_t frame = {
        .header = GMC_HEADER,
        .type = {
            .compressed = 0,
            .comm_layer = comm_layer_type,
            .encryption = GMC_ENC_NONE,
            .format = GMC_FORMAT_BINARY,
            .version = GMC_VERSION_2},  // 使用VERSION_2 (0x22)
        .control = {.fend = 1, .ack = 1, .fragment = 0, .msg_set = GMC_CTRL_MSG_NORMAL},  // ACK帧不分片, 0xC2
        .frame_id = frame_id,
        .packet_id = 0,
        .data_len = 1,
        .data = ack_data};

    if (xSemaphoreTake(tx_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }
// ACK should ONLY go back to the requester (Phone)
    esp_err_t ret = send_frame(&frame, false);


    xSemaphoreGive(tx_mutex);

    return ret;
}

// Convenience functions
esp_err_t gmc_send_device_info(const char *name, const char *model, const char *sn,
                               const char *version_name, uint8_t version_code, const char *mac)
{
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "name", name);
    cJSON_AddStringToObject(data, "model", model);
    cJSON_AddStringToObject(data, "sn", sn);
    cJSON_AddStringToObject(data, "version_name", version_name);
    cJSON_AddNumberToObject(data, "version_code", version_code);
    cJSON_AddStringToObject(data, "mac", mac);

    esp_err_t ret = gmc_send_response(GMC_MODE_NOTIFY, GMC_TYPE_SETTINGS,
                                     "getDeviceInformation", 0, "success", data);
    cJSON_Delete(data);

    return ret;
}

esp_err_t gmc_notify_volume_changed(uint8_t channel_id, uint8_t volume)
{
    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "id", channel_id);
    cJSON_AddNumberToObject(data, "value", volume);

    esp_err_t ret = gmc_send_message(GMC_MODE_NOTIFY, GMC_TYPE_AUDIO_CONTROL,
                                     "audioChannelVolumeChanged", data, false);
    cJSON_Delete(data);

    return ret;
}

/**
 * @brief UART receive callback handler
 * Parse received UART data as GMC frames
 */
void gmc_uart_rx_handler(const uint8_t *data, size_t len)
{
    ESP_LOGI(TAG, "UART RX: %zu bytes", len);

    // Parse GMC frame - use existing parser
    parse_gmc_frame(data, len);
}

/**
 * @brief UART error callback handler
 */
void gmc_uart_error_handler(esp_err_t error_code)
{
    ESP_LOGE(TAG, "UART error: %s", esp_err_to_name(error_code));

    // Could implement recovery logic here
    // e.g., flush buffers, reset statistics, etc.
}
