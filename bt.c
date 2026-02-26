#include "gmc_bt.h"
#include "gmc_protocol.h"
#include "gmc_settings.h"
#include <string.h>
#include <stdio.h>

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "nvs.h"

// NimBLE includes
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/ble_gatt.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "gmc_bt";

// BLE Service and Characteristic UUIDs (128-bit, little-endian for NimBLE)
static const ble_uuid128_t gmc_svc_uuid =
    BLE_UUID128_INIT(0xda, 0x6d, 0x0f, 0xf1, 0x0d, 0x18, 0x44, 0x2c,
                     0xba, 0xbe, 0xf8, 0x5b, 0x5b, 0xaa, 0x6f, 0x11);

static const ble_uuid128_t gmc_chr_uuid =
    BLE_UUID128_INIT(0xda, 0x6d, 0x0f, 0xf3, 0x0d, 0x18, 0x44, 0x2c,
                     0xba, 0xbe, 0xf8, 0x5b, 0x5b, 0xaa, 0x6f, 0x11);

#define TEST_DEVICE_NAME "GMC-BLE"
#define GMC_CHR_MAX_LEN 512

// Global state - Peripheral (GMC-BLE Server)
static uint16_t gmc_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool gmc_connected = false;
static gmc_bt_state_t gmc_bt_state = GMC_BT_STATE_IDLE;
static uint8_t gmc_remote_addr[6] = {0};
static bool gmc_notify_enabled = false;

// Global state - Central (连接到音响)
static uint16_t audio_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool audio_connected = false;
static bool scanning = false;
static uint16_t audio_chr_val_handle = 0;  // 音响设备的特性句柄
static bool audio_svc_found = false;
static bool ble_prefix_missing_logged = false;

static gmc_bt_rx_callback_t rx_callback = NULL;
static gmc_bt_state_callback_t state_callback = NULL;
static SemaphoreHandle_t tx_mutex = NULL;

static uint16_t gmc_chr_val_handle;
static char ble_device_name[DEVICE_NAME_MAX_LEN] = TEST_DEVICE_NAME;

// Characteristic access callback
static int gmc_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc;

    switch (ctxt->op)
    {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        ESP_LOGI(TAG, "Characteristic read");
        // Return empty data for read
        rc = os_mbuf_append(ctxt->om, NULL, 0);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        ESP_LOGD(TAG, "Characteristic write, len=%d", OS_MBUF_PKTLEN(ctxt->om));

        // Extract data from mbuf
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len > 0 && len <= GMC_CHR_MAX_LEN)
        {
            uint8_t *data = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (data)
            {
                rc = ble_hs_mbuf_to_flat(ctxt->om, data, len, &len);
                if (rc == 0)
                {
                    ESP_LOGD(TAG, "PHONE_RX Binary (%d bytes):", len);
                    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_DEBUG);
                    
                    if (rx_callback)
                    {
                        rx_callback(data, len);
                    }
                }
                free(data);
            }
        }
        return 0;

    case BLE_GATT_ACCESS_OP_READ_DSC:
    case BLE_GATT_ACCESS_OP_WRITE_DSC:
        // Handle CCC descriptor
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_DSC)
        {
            uint16_t ccc_val;
            rc = ble_hs_mbuf_to_flat(ctxt->om, &ccc_val, sizeof(ccc_val), NULL);
            if (rc == 0)
            {
                gmc_notify_enabled = (ccc_val & BLE_GATT_CHR_F_NOTIFY) != 0;
                                uint8_t *data = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                                if (data == NULL)
                                {
                                    data = (uint8_t *)malloc(len);
                                }
                ESP_LOGI(TAG, "Notifications %s (CCC=0x%04x)",
                         gmc_notify_enabled ? "ENABLED" : "DISABLED", ccc_val);
                ESP_LOGI(TAG, "========================================");
                if (gmc_notify_enabled)
                {
                    ESP_LOGI(TAG, "BLE device can now receive responses!");
                }
                else
                {
                    ESP_LOGW(TAG, "BLE device will NOT receive responses!");
                }
            }
        }
        return 0;

    default:
        ESP_LOGW(TAG, "Unexpected characteristic operation: %d", ctxt->op);
        return BLE_ATT_ERR_UNLIKELY;
    }
}

// GATT service definition
static const struct ble_gatt_svc_def gmc_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gmc_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &gmc_chr_uuid.u,
                .access_cb = gmc_chr_access,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE |
                         BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &gmc_chr_val_handle,
            },
            {0} // No more characteristics
        },
    },
    {0} // No more services
};

// GATT客户端：特性发现回调
static int on_audio_chr_discovered(uint16_t conn_handle,
                                  const struct ble_gatt_error *error,
                                  const struct ble_gatt_chr *chr,
                                  void *arg)
{
    if (error->status == 0) {
        if (chr != NULL) {
            char uuid_str[BLE_UUID_STR_LEN];
            ble_uuid_to_str(&chr->uuid.u, uuid_str);
            ESP_LOGI(TAG, "  - Found Characteristic: %s (handle=%d)", uuid_str, chr->val_handle);

            // 检查是否是目标特性
            if (ble_uuid_cmp(&chr->uuid.u, &gmc_chr_uuid.u) == 0) {
                audio_chr_val_handle = chr->val_handle;
                ESP_LOGI(TAG, "✓ Found GMC characteristic, handle=%d", audio_chr_val_handle);
            }
        }
    } else if (error->status == BLE_HS_EDONE) {
        ESP_LOGI(TAG, "Characteristic discovery complete");
        if (audio_chr_val_handle == 0) {
            ESP_LOGE(TAG, "❌ Target Characteristic not found. Disconnecting...");
            ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
    } else {
        ESP_LOGE(TAG, "Characteristic discovery error; status=%d", error->status);
    }
    return 0;
}

// GATT客户端：服务发现回调
static int on_audio_svc_discovered(uint16_t conn_handle,
                                 const struct ble_gatt_error *error,
                                 const struct ble_gatt_svc *service,
                                 void *arg)
{
    if (error->status == 0) {
        if (service != NULL) {
            // 检查是否是GMC服务
            if (ble_uuid_cmp(&service->uuid.u, &gmc_svc_uuid.u) == 0) {
                ESP_LOGI(TAG, "✓ Found GMC service, discovering characteristics...");
                audio_svc_found = true;
                // 发现该服务的特性
                ble_gattc_disc_all_chrs(conn_handle, service->start_handle,
                                       service->end_handle, on_audio_chr_discovered, NULL);
            }
        }
    } else if (error->status == BLE_HS_EDONE) {
        ESP_LOGI(TAG, "Service discovery complete");
        if (!audio_svc_found) {
            ESP_LOGE(TAG, "❌ Target Service not found. Disconnecting...");
            ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        }
    } else {
        ESP_LOGE(TAG, "Service discovery error; status=%d", error->status);
    }
    return 0;
}

// 启动音响设备的GATT服务发现
static void discover_audio_services(uint16_t conn_handle)
{
    ESP_LOGI(TAG, "🔍 Starting GATT service discovery for Audio device...");
    int rc = ble_gattc_disc_all_svcs(conn_handle, on_audio_svc_discovered, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to discover services; rc=%d", rc);
    }
}

// Forward declaration
static void start_audio_scan(void);

// Central角色：扫描回调
static int __attribute__((unused)) ble_scan_callback(struct ble_gap_event *event, void *arg)
{
    struct ble_hs_adv_fields fields;
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        // Optional Debug: Print Packet Type
        if (event->disc.event_type == BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP) {
             // ESP_LOGI(TAG, "Packet Type: SCAN_RSP (Addr: %02x:%02x...)", event->disc.addr.val[5], event->disc.addr.val[4]);
        }

        // Manual Parsing for AD Type 0x09 (Complete Local Name) as per user request
        const uint8_t *data = event->disc.data;
        uint8_t len = event->disc.length_data;
        char parsed_name[33] = {0};
        bool found_name = false;

        for (int i = 0; i < len;) {
            uint8_t ad_len = data[i];
            if (ad_len == 0) break;
            
            if (i + 1 >= len) break; // Safety check
            uint8_t ad_type = data[i + 1];

            if (ad_type == 0x09) { // Complete Local Name
                uint8_t name_len = ad_len - 1;
                if (name_len > 32) name_len = 32;
                memcpy(parsed_name, &data[i + 2], name_len);
                parsed_name[name_len] = '\0';
                ESP_LOGD(TAG, "-> Parsed Name from Scan Response: '%s'", parsed_name);
                found_name = true;
            }

            i += (ad_len + 1);
        }

        // Initialize fields to avoid garbage if parsing fails
        memset(&fields, 0, sizeof(fields));
        rc = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
        if (rc != 0 && !found_name) {
            // Only return if standard parsing failed AND we didn't manually find the name
            return 0;
        }

        // Use manually parsed name if available; otherwise fallback to standard parser
        const char *target_name = NULL;
        size_t target_name_len = 0;

        if (found_name) {
            target_name = parsed_name;
            target_name_len = strlen(parsed_name);
        } else if (fields.name != NULL && fields.name_len > 0) {
            target_name = (char *)fields.name;
            target_name_len = fields.name_len;
        }

        // 检查设备名称是否匹配
        if (target_name != NULL && target_name_len > 0) {
            char name[33] = {0};
            memcpy(name, target_name, target_name_len < 32 ? target_name_len : 32);
            name[32] = '\0'; // Ensure null termination
            
            // 使用前缀匹配
            bool match = false;
#ifdef CONFIG_GMC_BLE_TARGET_NAME_PREFIX
            match = strncmp(name, CONFIG_GMC_BLE_TARGET_NAME_PREFIX,
                            strlen(CONFIG_GMC_BLE_TARGET_NAME_PREFIX)) == 0;
#endif
            
            if (found_name && !match) {
                 // Log why we didn't match (debug)
                 // ESP_LOGI(TAG, "Name '%s' did not match prefix '%s'", name, CONFIG_GMC_BLE_TARGET_NAME_PREFIX);
            }

            if (match) {
                ESP_LOGI(TAG, "🎯 Found target device: %s", name);
                ESP_LOGI(TAG, "   Address: %02x:%02x:%02x:%02x:%02x:%02x",
                         event->disc.addr.val[5], event->disc.addr.val[4],
                         event->disc.addr.val[3], event->disc.addr.val[2],
                         event->disc.addr.val[1], event->disc.addr.val[0]);
                
                // 停止扫描
                ble_gap_disc_cancel();
                scanning = false;
                
                // 连接到设备
                rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &event->disc.addr,
                                    30000, NULL, ble_scan_callback, NULL);
                if (rc != 0) {
                    ESP_LOGE(TAG, "Error initiating connection; rc=%d", rc);
                } else {
                    ESP_LOGI(TAG, "Connecting to %s...", name);
                }
            }
        }
        return 0;
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            audio_conn_handle = event->connect.conn_handle;
            audio_connected = true;
            audio_chr_val_handle = 0;
            audio_svc_found = false;
            ESP_LOGI(TAG, "✅ Connected to Audio device (handle=%d)", audio_conn_handle);
            
            int rc = ble_gattc_exchange_mtu(audio_conn_handle, NULL, NULL);
            if (rc != 0) {
                ESP_LOGE(TAG, "Failed to exchange MTU; rc=%d", rc);
            }

            // 开始GATT服务发现
            discover_audio_services(audio_conn_handle);
        } else {
            ESP_LOGE(TAG, "❌ Audio connection failed; status=%d", event->connect.status);
            audio_connected = false;
            audio_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Audio device disconnected; reason=%d", event->disconnect.reason);
        audio_connected = false;
        audio_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        audio_chr_val_handle = 0;  // Clear characteristic handle
        
        // Restart scan using standard function (Active Scan) without blocking
        // A minimal delay is implicitly handled by the host task processing
        start_audio_scan();
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX:
        // Only forward notifications/indications from the connected audio peripheral
        if (!audio_connected || event->notify_rx.conn_handle != audio_conn_handle)
        {
            return 0;
        }

        if (event->notify_rx.om != NULL)
        {
            uint16_t len = OS_MBUF_PKTLEN(event->notify_rx.om);
            if (len > 0)
            {
                uint8_t *data = (uint8_t *)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (data == NULL)
                {
                    data = (uint8_t *)malloc(len);
                }

                if (data != NULL)
                {
                    int rc = ble_hs_mbuf_to_flat(event->notify_rx.om, data, len, &len);
                    if (rc == 0)
                    {
                        ESP_LOGD(TAG, "AUDIO_RX Binary (%d bytes):", len);
                        ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_DEBUG);
                        gmc_log_json_from_frame(data, len, "AUDIO_RX_NOTIFY");
                        // 不要回传给手机：转发给蓝牙外设的GMC内容的响应不回传
                        // Forward to phone (GMC BLE server) only if notifications enabled
                        // if (gmc_bt_can_send())
                        // {
                        //     gmc_log_json_from_frame(data, len, "PHONE_TX (FWD)");
                        //     gmc_bt_send(data, len);
                        // }
                        // else
                        // {
                        //     ESP_LOGD(TAG, "Phone not ready for notify; drop audio RX (%u bytes)", (unsigned)len);
                        // }
                        ESP_LOGD(TAG, "Audio RX not forwarded to phone (response to forwarded message)");
                    }
                    free(data);
                }
            }
        }
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        ESP_LOGI(TAG, "Scan complete; restarting scan...");
        scanning = false;
        // If not connected, continue scanning
        if (!audio_connected) {
             start_audio_scan();
        }
        return 0;
    }

    return 0;
}

// 启动音响设备扫描
static void start_audio_scan(void)
{
#ifdef CONFIG_GMC_BLE_TARGET_NAME_PREFIX
    if (!scanning && !audio_connected) {
        struct ble_gap_disc_params disc_params = {0};
        disc_params.filter_duplicates = 1; /* 过滤重复包，减少日志量 */
        disc_params.passive = 0;           /* 主动扫描，这样才能获取Scan Response中的名称 */
        disc_params.itvl = 0x80;           /* Window/Interval 相同 = 100% duty cycle */
        disc_params.window = 0x80;
        disc_params.filter_policy = 0;
        disc_params.limited = 0;

        int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &disc_params,
                              ble_scan_callback, NULL);
        if (rc != 0) {
            ESP_LOGE(TAG, "Error initiating scan; rc=%d", rc);
        } else {
            scanning = true;
            ESP_LOGI(TAG, "🔍 Scanning for Audio device with prefix: %s", CONFIG_GMC_BLE_TARGET_NAME_PREFIX);
        }
    }
#else
    if (!ble_prefix_missing_logged) {
        ble_prefix_missing_logged = true;
        ESP_LOGW(TAG, "Audio scan disabled: CONFIG_GMC_BLE_TARGET_NAME_PREFIX not set");
    }
#endif
}

// GAP event handler (Peripheral角色 - GMC-BLE Server)
static int gmc_gap_event(struct ble_gap_event *event, void *arg)
{
    struct ble_gap_conn_desc desc;
    int rc;

    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "Connection %s; status=%d",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.status);

        if (event->connect.status == 0)
        {
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            if (rc == 0)
            {
                gmc_conn_handle = event->connect.conn_handle;
                gmc_connected = true;
                gmc_bt_state = GMC_BT_STATE_CONNECTED;
                memcpy(gmc_remote_addr, desc.peer_id_addr.val, 6);
                ESP_LOGI(TAG, "Connected to %02x:%02x:%02x:%02x:%02x:%02x",
                         desc.peer_id_addr.val[5], desc.peer_id_addr.val[4],
                         desc.peer_id_addr.val[3], desc.peer_id_addr.val[2],
                         desc.peer_id_addr.val[1], desc.peer_id_addr.val[0]);
                ESP_LOGW(TAG, "===========================================");
                ESP_LOGW(TAG, "IMPORTANT: Enable notifications in your BLE client!");
                ESP_LOGW(TAG, "In nRF Connect: tap the notify icon (down arrow)");
                ESP_LOGW(TAG, "In LightBlue: tap 'Listen for notifications'");
                ESP_LOGW(TAG, "==========================================");

                // Notify state change - disable wake word
                if (state_callback)
                {
                    state_callback(GMC_BT_STATE_CONNECTED, true);
                }
            }
        }
        else
        {
            // Connection failed; resume advertising
            gmc_bt_start_advertising();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnect; reason=%d", event->disconnect.reason);
        gmc_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        gmc_connected = false;
        gmc_notify_enabled = false;
        gmc_bt_state = GMC_BT_STATE_IDLE;

        // Notify state change - re-enable wake word
        if (state_callback)
        {
            state_callback(GMC_BT_STATE_IDLE, false);
        }

        // Resume advertising
        gmc_bt_start_advertising();
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE:
        ESP_LOGI(TAG, "Connection updated; status=%d", event->conn_update.status);
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "Advertise complete; reason=%d", event->adv_complete.reason);
        gmc_bt_start_advertising();
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU update event; conn_handle=%d mtu=%d",
                 event->mtu.conn_handle, event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "Subscribe event; conn_handle=%d attr_handle=%d "
                      "reason=%d prevn=%d curn=%d previ=%d curi=%d",
                 event->subscribe.conn_handle,
                 event->subscribe.attr_handle,
                 event->subscribe.reason,
                 event->subscribe.prev_notify,
                 event->subscribe.cur_notify,
                 event->subscribe.prev_indicate,
                 event->subscribe.cur_indicate);
        gmc_notify_enabled = event->subscribe.cur_notify != 0;
        return 0;
    }

    return 0;
}

// Start advertising
esp_err_t gmc_bt_start_advertising(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields adv_fields;
    struct ble_hs_adv_fields rsp_fields;
    int rc;

    // Stop existing advertising if any
    ble_gap_adv_stop();

    memset(&adv_fields, 0, sizeof(adv_fields));
    memset(&rsp_fields, 0, sizeof(rsp_fields));

    // Advertising payload is limited to 31 bytes.
    // Keep ADV minimal (flags + 128-bit UUID), put the name in Scan Response.

    // Set flags (ADV)
    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    // Set service UUID (ADV)
    adv_fields.uuids128 = &gmc_svc_uuid;
    adv_fields.num_uuids128 = 1;
    adv_fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Error setting advertisement data; rc=%d (name_len=%u)", rc,
                 (unsigned)strlen(ble_device_name));
        return ESP_FAIL;
    }

    // Set device name (SCAN RSP)
    const size_t name_len_full = strlen(ble_device_name);
    size_t name_len_rsp = name_len_full;
    if (name_len_rsp > 29) {
        name_len_rsp = 29; // 31 bytes total - (len + type)
    }
    rsp_fields.name = (uint8_t *)ble_device_name;
    rsp_fields.name_len = name_len_rsp;
    rsp_fields.name_is_complete = (name_len_rsp == name_len_full);

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Error setting scan response data; rc=%d (name_len=%u)", rc,
                 (unsigned)name_len_rsp);
        return ESP_FAIL;
    }

    // Set advertising parameters
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = BLE_GAP_ADV_FAST_INTERVAL1_MIN;
    adv_params.itvl_max = BLE_GAP_ADV_FAST_INTERVAL1_MAX;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, gmc_gap_event, NULL);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Error enabling advertisement; rc=%d", rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Advertising started");
    gmc_bt_state = GMC_BT_STATE_ADVERTISING;
    return ESP_OK;
}

// Stop advertising
esp_err_t gmc_bt_stop_advertising(void)
{
    int rc = ble_gap_adv_stop();
    if (rc == 0)
    {
        ESP_LOGI(TAG, "Advertising stopped");
        gmc_bt_state = GMC_BT_STATE_IDLE;
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Failed to stop advertising; rc=%d", rc);
    return ESP_FAIL;
}

// NimBLE host configuration callback
static void gmc_ble_on_sync(void)
{
    int rc;

    ESP_LOGI(TAG, "BLE Host synced");

    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Error setting address; rc=%d", rc);
        return;
    }

    // Give stack time to stabilize
    vTaskDelay(pdMS_TO_TICKS(100));

    // Start advertising (Peripheral角色 - GMC-BLE)
    rc = gmc_bt_start_advertising();
    if (rc != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start advertising");
    }
    
    // Start scanning for Audio device (Central角色)
    vTaskDelay(pdMS_TO_TICKS(500));
    start_audio_scan();
}

// NimBLE host reset callback
static void gmc_ble_on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE host reset; reason=%d", reason);
}

// NimBLE host task
static void gmc_ble_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE Host Task Started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// Initialize BLE
esp_err_t gmc_bt_init(void)
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Initializing NimBLE");
    
    // Suppress verbose NimBLE logs (GATT procedure initiated, etc.)
    esp_log_level_set("NimBLE", ESP_LOG_WARN);

    // Create transmit mutex
    tx_mutex = xSemaphoreCreateMutex();
    if (!tx_mutex)
    {
        ESP_LOGE(TAG, "Failed to create TX mutex");
        return ESP_ERR_NO_MEM;
    }

    // Initialize NimBLE
    ret = nimble_port_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to init nimble %d", ret);
        return ret;
    }

    // Configure host
    ble_hs_cfg.reset_cb = gmc_ble_on_reset;
    ble_hs_cfg.sync_cb = gmc_ble_on_sync;
    ble_hs_cfg.gatts_register_cb = NULL;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    // Set device name from NVS (direct read to avoid mutex dependency)
    nvs_handle_t nvs_handle;
    esp_err_t nvs_ret = nvs_open("settings", NVS_READONLY, &nvs_handle);
    if (nvs_ret == ESP_OK)
    {
        size_t len = DEVICE_NAME_MAX_LEN;
        nvs_ret = nvs_get_str(nvs_handle, "dev_name", ble_device_name, &len);
        nvs_close(nvs_handle);
        if (nvs_ret == ESP_OK)
        {
            ESP_LOGI(TAG, "Setting BLE device name from NVS: %s", ble_device_name);
        }
        else
        {
            strncpy(ble_device_name, TEST_DEVICE_NAME, DEVICE_NAME_MAX_LEN);
            ESP_LOGI(TAG, "No device name in NVS, using default: %s", ble_device_name);
        }
    }
    else
    {
        ESP_LOGW(TAG, "Failed to open NVS, using default name: %s", ble_device_name);
    }

    ret = ble_svc_gap_device_name_set(ble_device_name);
    if (ret != 0)
    {
        ESP_LOGE(TAG, "Error setting device name; rc=%d", ret);
        return ESP_FAIL;
    }

    // Initialize GATT services
    ble_svc_gap_init();
    ble_svc_gatt_init();

    // Register GMC GATT service
    ret = ble_gatts_count_cfg(gmc_gatt_svcs);
    if (ret != 0)
    {
        ESP_LOGE(TAG, "Error counting GATT services; rc=%d", ret);
        return ESP_FAIL;
    }

    ret = ble_gatts_add_svcs(gmc_gatt_svcs);
    if (ret != 0)
    {
        ESP_LOGE(TAG, "Error adding GATT services; rc=%d", ret);
        return ESP_FAIL;
    }

    // Start NimBLE host task
    nimble_port_freertos_init(gmc_ble_host_task);

    ESP_LOGI(TAG, "BLE initialized successfully");
    return ESP_OK;
}

esp_err_t gmc_bt_set_device_name(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    size_t max_gap_len = 31;
#ifdef CONFIG_BT_NIMBLE_GAP_DEVICE_NAME_MAX_LEN
    max_gap_len = CONFIG_BT_NIMBLE_GAP_DEVICE_NAME_MAX_LEN;
#endif
    if (max_gap_len > (sizeof(ble_device_name) - 1)) {
        max_gap_len = sizeof(ble_device_name) - 1;
    }

    snprintf(ble_device_name, sizeof(ble_device_name), "%.*s", (int)max_gap_len, name);

    int rc = ble_svc_gap_device_name_set(ble_device_name);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error setting device name; rc=%d", rc);
        return ESP_FAIL;
    }

    if (gmc_bt_state == GMC_BT_STATE_ADVERTISING) {
        (void)gmc_bt_start_advertising();
    }

    return ESP_OK;
}

// Register RX callback
void gmc_bt_register_rx_callback(gmc_bt_rx_callback_t callback)
{
    rx_callback = callback;
}

// Send data via BLE notification
esp_err_t gmc_bt_send(const uint8_t *data, size_t length)
{
    if (!gmc_connected || !gmc_notify_enabled)
    {
        ESP_LOGW(TAG, "Cannot send: not connected or notifications disabled");
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(tx_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to acquire TX mutex");
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGD(TAG, "PHONE_TX Binary (%d bytes):", length);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, length, ESP_LOG_DEBUG);

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, length);
    if (!om)
    {
        xSemaphoreGive(tx_mutex);
        ESP_LOGE(TAG, "Failed to allocate mbuf");
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(gmc_conn_handle, gmc_chr_val_handle, om);
    xSemaphoreGive(tx_mutex);

    if (rc != 0)
    {
        ESP_LOGE(TAG, "Error sending notification; rc=%d", rc);
        return ESP_FAIL;
    }

    return ESP_OK;
}

// Get connection state
gmc_bt_state_t gmc_bt_get_state(void)
{
    return gmc_bt_state;
}

bool gmc_bt_can_send(void)
{
    return gmc_connected && gmc_notify_enabled;
}

bool gmc_bt_audio_connected(void)
{
    return audio_connected;
}

// 向音响设备发送数据 (Central角色)
esp_err_t gmc_bt_send_to_audio(const uint8_t *data, size_t length)
{
    if (!audio_connected)
    {
        ESP_LOGD(TAG, "Audio device not connected");
        return ESP_ERR_INVALID_STATE;
    }

    if (audio_chr_val_handle == 0)
    {
        ESP_LOGW(TAG, "Audio characteristic handle not discovered yet");
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(tx_mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to acquire TX mutex for Audio");
        return ESP_ERR_TIMEOUT;
    }

    gmc_log_json_from_frame(data, length, "AUDIO_TX");
    ESP_LOGD(TAG, "AUDIO_TX Binary (%d bytes):", length);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, length, ESP_LOG_DEBUG);

    // Use Write With Response (request) to ensure reliability and handle larger data automatically
    int rc = ble_gattc_write_flat(audio_conn_handle, audio_chr_val_handle,
                                  data, length, NULL, NULL);
    
    xSemaphoreGive(tx_mutex);

    if (rc != 0)
    {
        ESP_LOGD(TAG, "Audio device rejected write (rc=%d, possibly unsupported command)", rc);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "✓ Sent %d bytes to Audio device", length);
    return ESP_OK;
}

// Get remote device address
void gmc_bt_get_remote_addr(uint8_t *addr)
{
    if (addr)
    {
        memcpy(addr, gmc_remote_addr, 6);
    }
}

// Register state change callback
void gmc_bt_register_state_callback(gmc_bt_state_callback_t callback)
{
    state_callback = callback;
}
