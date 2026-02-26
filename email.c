#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include "gmc_email.h"
#include "gmc_protocol.h"

static const char *TAG = "gmc_email";

static email_received_callback_t email_callback = NULL;

esp_err_t gmc_email_init(void)
{
    ESP_LOGI(TAG, "GMC email module initialized");
    return ESP_OK;
}

void gmc_email_register_callback(email_received_callback_t callback)
{
    email_callback = callback;
    ESP_LOGI(TAG, "Email callback registered");
}

email_data_t *gmc_email_create(const char *id, const char *title, const char *content)
{
    if (id == NULL || title == NULL || content == NULL)
    {
        ESP_LOGE(TAG, "Invalid parameters");
        return NULL;
    }

    email_data_t *email = (email_data_t *)heap_caps_malloc(sizeof(email_data_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (email == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for email");
        return NULL;
    }

    memset(email, 0, sizeof(email_data_t));

    // Copy fixed-size fields
    strncpy(email->id, id, sizeof(email->id) - 1);
    strncpy(email->title, title, sizeof(email->title) - 1);

    // Allocate and copy content
    size_t content_len = strlen(content);
    email->content = (char *)heap_caps_malloc(content_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (email->content == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for content");
        free(email);
        return NULL;
    }
    strcpy(email->content, content);

    return email;
}

void gmc_email_free(email_data_t *email)
{
    if (email != NULL)
    {
        if (email->content != NULL)
        {
            free(email->content);
        }
        free(email);
    }
}

esp_err_t gmc_email_handle_readmail(const void *email_json)
{
    if (email_json == NULL)
    {
        ESP_LOGE(TAG, "email_json is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    const cJSON *json = (const cJSON *)email_json;

    // Extract email fields
    cJSON *id = cJSON_GetObjectItem(json, "id");
    cJSON *title = cJSON_GetObjectItem(json, "title");
    cJSON *content = cJSON_GetObjectItem(json, "content");

    if (!cJSON_IsString(id) || !cJSON_IsString(title) || !cJSON_IsString(content))
    {
        ESP_LOGE(TAG, "Invalid email data format");
        return ESP_ERR_INVALID_ARG;
    }

    // Use printf to preserve UTF-8 encoding for Chinese characters
    printf("I (%lu) %s: Received email: id=%s, title=%s\n",
           esp_log_timestamp(), TAG, id->valuestring, title->valuestring);
    printf("D (%lu) %s: Email content: %s\n",
           esp_log_timestamp(), TAG, content->valuestring);

    // Create email structure
    email_data_t *email = gmc_email_create(
        id->valuestring,
        title->valuestring,
        content->valuestring);

    if (email == NULL)
    {
        ESP_LOGE(TAG, "Failed to create email");
        return ESP_ERR_NO_MEM;
    }

    // Call registered callback to process email
    if (email_callback != NULL)
    {
        ESP_LOGI(TAG, "Calling email callback for processing");
        email_callback(email);
    }
    else
    {
        ESP_LOGW(TAG, "No email callback registered, email not processed");
    }

    // Note: We don't free the email here - callback is responsible for:
    // 1. Processing the email (TTS, translation, etc.)
    // 2. Generating a reply
    // 3. Calling gmc_email_send_reply()
    // 4. Freeing the email with gmc_email_free()

    // However, if no callback, we should free it
    if (email_callback == NULL)
    {
        gmc_email_free(email);
    }

    return ESP_OK;
}

esp_err_t gmc_email_send_reply(const char *id, const char *title, const char *content)
{
    if (id == NULL || title == NULL || content == NULL)
    {
        ESP_LOGE(TAG, "Invalid reply parameters");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Sending email reply: id=%s, title=%s", id, title);
    ESP_LOGD(TAG, "Reply content: %s", content);

    // Create data JSON object
    cJSON *data = cJSON_CreateObject();
    if (data == NULL)
    {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(data, "id", id);
    cJSON_AddStringToObject(data, "title", title);
    cJSON_AddStringToObject(data, "content", content);

    // Send notify message via GMC protocol
    esp_err_t ret = gmc_send_message(
        GMC_MODE_NOTIFY,
        GMC_TYPE_MAIL,
        "replymail",
        data,
        false // No ACK needed for notifications
    );

    cJSON_Delete(data);

    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "Email reply sent successfully");
    }
    else
    {
        ESP_LOGE(TAG, "Failed to send email reply: %s", esp_err_to_name(ret));
    }

    return ret;
}
