#include "main.h"

#include <esp_event.h>
#include <esp_log.h>
#include <peer.h>

#include "nvs_flash.h"

// Main application entry point
// Initializes system components and starts the WebRTC communication
extern "C" void app_main(void) {
  // Initialize non-volatile storage (NVS) for storing system configuration
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // If NVS partition is full or version mismatch, erase and reinitialize
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Create default event loop for system events
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  // Initialize system components in sequence:
  peer_init();           // Initialize WebRTC peer connection system
  init_audio_capture();  // Set up I2S audio interfaces
  init_audio_decoder();  // Initialize Opus decoder for incoming audio
  wifi();                // Connect to WiFi network
  webrtc();              // Start WebRTC session (this call blocks indefinitely)
}
