#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "media.h"

static const char* TAG = "MAIN";

// Task to handle audio loopback
void audio_loopback_task(void *pvParameters) {
    while(1) {
        audio_loopback();
        // No delay needed as the I2S functions already block
    }
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Starting audio debug application...");
    
    // Initialize audio system
    init_audio_capture();
    
    // Initialize Opus encoder and decoder
    init_audio_encoder();
    init_audio_decoder();
    
    ESP_LOGI(TAG, "Creating audio loopback task...");
    
    // Create audio loopback task with higher priority and stack
    xTaskCreate(audio_loopback_task, "audio_loopback", 8192, NULL, 5, NULL);
    
    // Main loop - for monitoring
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG, "System running...");
    }
} 