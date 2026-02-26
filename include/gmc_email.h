#ifndef GMC_EMAIL_H
#define GMC_EMAIL_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Email data structure
 */
typedef struct {
    char id[64];            // Email ID
    char title[256];        // Email subject/title
    char *content;          // Email content (dynamically allocated)
} email_data_t;

/**
 * @brief Callback function type for received emails
 * 
 * This callback is called when device receives an email from APP.
 * The callback should process the email (TTS, translation, etc.)
 * and generate a reply.
 * 
 * @param email Pointer to received email data
 */
typedef void (*email_received_callback_t)(const email_data_t *email);

/**
 * @brief Initialize email module
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_email_init(void);

/**
 * @brief Register callback for received emails
 * 
 * @param callback Function to call when email is received
 */
void gmc_email_register_callback(email_received_callback_t callback);

/**
 * @brief Send email reply notification to APP
 * 
 * This function sends the reply email content back to APP via GMC protocol.
 * 
 * @param id Email ID
 * @param title Reply email subject
 * @param content Reply email content
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_email_send_reply(const char *id, const char *title, const char *content);

/**
 * @brief Handle received "readmail" command from APP
 * 
 * This is called internally when GMC protocol receives readmail command.
 * 
 * @param email_json JSON object containing email data
 * @return esp_err_t ESP_OK on success
 */
esp_err_t gmc_email_handle_readmail(const void *email_json);

/**
 * @brief Create email data structure
 * 
 * @param id Email ID
 * @param title Email subject
 * @param content Email content
 * @return email_data_t* Pointer to allocated email (caller must free)
 */
email_data_t* gmc_email_create(const char *id, const char *title, const char *content);

/**
 * @brief Free email data structure
 * 
 * @param email Pointer to email to free
 */
void gmc_email_free(email_data_t *email);

#ifdef __cplusplus
}
#endif

#endif // GMC_EMAIL_H
