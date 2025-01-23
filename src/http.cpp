#include <esp_http_client.h>
#include <esp_log.h>
#include <string.h>

#include "main.h"

// Utility macro for safe minimum value calculation
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

// HTTP event handler for managing the complete HTTP request lifecycle
// Handles events like connection, data reception, errors, and disconnection
// evt: HTTP client event structure containing event type and data
esp_err_t http_event_handler(esp_http_client_event_t* evt) {
  static int output_len;  // Tracks total response data length

  switch (evt->event_id) {
    case HTTP_EVENT_REDIRECT:
      ESP_LOGD(LOG_TAG, "HTTP_EVENT_REDIRECT");
      // Set headers for redirect
      esp_http_client_set_header(evt->client, "From", "rynn.zhng@gmail.com");
      esp_http_client_set_header(evt->client, "Accept", "text/html");
      esp_http_client_set_redirection(evt->client);
      break;

    case HTTP_EVENT_ERROR:
      ESP_LOGD(LOG_TAG, "HTTP_EVENT_ERROR");
      break;

    case HTTP_EVENT_ON_CONNECTED:
      ESP_LOGD(LOG_TAG, "HTTP_EVENT_ON_CONNECTED");
      break;

    case HTTP_EVENT_HEADER_SENT:
      ESP_LOGD(LOG_TAG, "HTTP_EVENT_HEADER_SENT");
      break;

    case HTTP_EVENT_ON_HEADER:
      ESP_LOGD(LOG_TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s",
               evt->header_key, evt->header_value);
      break;

    case HTTP_EVENT_ON_DATA: {
      ESP_LOGD(LOG_TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);

      // Reject chunked responses as they're not supported
      if (esp_http_client_is_chunked_response(evt->client)) {
        ESP_LOGE(LOG_TAG, "Chunked HTTP response not supported");
        esp_restart();
      }

      // Clear user buffer on first data chunk
      if (output_len == 0 && evt->user_data) {
        memset(evt->user_data, 0, MAX_HTTP_OUTPUT_BUFFER);
      }

      // Copy response data to user buffer if available
      int copy_len = 0;
      if (evt->user_data) {
        // Calculate safe copy length to prevent buffer overflow
        // Reserve last byte for null terminator
        copy_len = MIN(evt->data_len, (MAX_HTTP_OUTPUT_BUFFER - output_len));
        if (copy_len) {
          memcpy(((char*)evt->user_data) + output_len, evt->data, copy_len);
        }
      }
      output_len += copy_len;
      break;
    }

    case HTTP_EVENT_ON_FINISH:
      ESP_LOGD(LOG_TAG, "HTTP_EVENT_ON_FINISH");
      output_len = 0;  // Reset output length counter
      break;

    case HTTP_EVENT_DISCONNECTED:
      ESP_LOGI(LOG_TAG, "HTTP_EVENT_DISCONNECTED");
      output_len = 0;  // Reset output length counter
      break;
  }
  return ESP_OK;
}

// Makes an HTTP POST request to the OpenAI API with WebRTC signaling data
// offer: SDP offer to send to the API
// answer: Buffer to store the API response (SDP answer)
void http_request(char* offer, char* answer) {
  // Initialize HTTP client configuration
  esp_http_client_config_t config;
  memset(&config, 0, sizeof(esp_http_client_config_t));

  // Set API endpoint and event handlers
  config.url = OPENAI_REALTIMEAPI;
  config.event_handler = http_event_handler;
  config.user_data = answer;  // Response will be stored in answer buffer

  // Prepare authorization header with API key
  snprintf(answer, MAX_HTTP_OUTPUT_BUFFER, "Bearer %s", OPENAI_API_KEY);

  // Initialize HTTP client and configure request
  esp_http_client_handle_t client = esp_http_client_init(&config);
  esp_http_client_set_method(client, HTTP_METHOD_POST);
  esp_http_client_set_header(client, "Content-Type", "application/sdp");
  esp_http_client_set_header(client, "Authorization", answer);
  esp_http_client_set_post_field(client, offer, strlen(offer));

  // Perform HTTP request and check response
  esp_err_t err = esp_http_client_perform(client);
  if (err != ESP_OK || esp_http_client_get_status_code(client) != 201) {
    ESP_LOGE(LOG_TAG, "Error perform http request %s", esp_err_to_name(err));
    esp_restart();  // Restart ESP on HTTP error
  }

  // Clean up HTTP client
  esp_http_client_cleanup(client);
}
