#include <assert.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"

// Global flag to track if we're connected to WiFi
static bool g_wifi_connected = false;

// WiFi event handler - processes WiFi connection events and IP address
// acquisition arg: unused context data event_base: type of event (WIFI_EVENT or
// IP_EVENT) event_id: specific event identifier event_data: data associated
// with the event
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
  static int s_retry_num = 0;  // Counter for connection retry attempts

  // Handle WiFi disconnection events
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    if (s_retry_num < 5) {  // Maximum 5 retry attempts
      esp_wifi_connect();
      s_retry_num++;
      ESP_LOGI(LOG_TAG, "retry to connect to the Access Point");
    }
    ESP_LOGI(LOG_TAG, "connection to the Access Point failed");
  }
  // Handle successful IP address acquisition
  else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
    ESP_LOGI(LOG_TAG, "obtained IP:" IPSTR, IP2STR(&event->ip_info.ip));
    g_wifi_connected = true;  // Set global connection flag
  }
}

// Main WiFi initialization and connection function
// - Sets up WiFi in Station (STA) mode
// - Registers event handlers for WiFi events
// - Initializes TCP/IP stack and WiFi configuration
// - Connects to the configured Access Point
// - Blocks until connection is established
void wifi(void) {
  // Register event handlers for WiFi and IP events
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             &wifi_event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             &wifi_event_handler, NULL));

  // Initialize TCP/IP stack and create default WiFi station
  ESP_ERROR_CHECK(esp_netif_init());
  esp_netif_t* sta_netif = esp_netif_create_default_wifi_sta();
  assert(sta_netif);

  // Initialize WiFi with default configuration
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));  // Set Station mode
  ESP_ERROR_CHECK(esp_wifi_start());

  // Configure WiFi connection parameters
  ESP_LOGI(LOG_TAG, "Connecting to WiFi SSID: %s", WIFI_SSID);
  wifi_config_t wifi_config;
  memset(&wifi_config, 0,
         sizeof(wifi_config));  // Clear configuration structure

  // Copy SSID and password from environment variables
  strncpy((char*)wifi_config.sta.ssid, (char*)WIFI_SSID,
          sizeof(wifi_config.sta.ssid));
  strncpy((char*)wifi_config.sta.password, (char*)WIFI_PASSWORD,
          sizeof(wifi_config.sta.password));

  // Set WiFi configuration and initiate connection
  ESP_ERROR_CHECK(esp_wifi_set_config(
      static_cast<wifi_interface_t>(ESP_IF_WIFI_STA), &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_connect());

  // Block until WiFi connection is established and IP is obtained
  while (!g_wifi_connected) {
    vTaskDelay(pdMS_TO_TICKS(200));  // 200ms delay between connection checks
  }
}
