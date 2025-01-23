// This is a test program to test the microphone (INMP441) on the ESP32S3
// Not apart of the final project, just a test to see if the microphone works
#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "esp_system.h"
#include "esp_log.h"

#define SAMPLE_RATE 8000
#define BUFFER_SAMPLES 320

#define MCLK_PIN 0
#define ADC_BCLK_PIN 38
#define ADC_LRCLK_PIN 39
#define ADC_DATA_PIN 40

// Sound level thresholds
#define NOISE_THRESHOLD 500    // Below this is considered noise/quiet
#define QUIET_THRESHOLD 2000   // Below this is considered quiet
#define MEDIUM_THRESHOLD 5000  // Below this is considered medium
#define LOUD_THRESHOLD 10000   // Above this is considered loud

static const char* TAG = "MIC_TEST";
static i2s_chan_handle_t rx_handle = NULL;

void init_microphone() {
    // I2S Configuration for Microphone
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    
    // Create RX channel only (we only need microphone input)
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = MCLK_PIN,
            .bclk = ADC_BCLK_PIN,
            .ws = ADC_LRCLK_PIN,
            .dout = I2S_GPIO_UNUSED,
            .din = ADC_DATA_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));

    ESP_LOGI(TAG, "Microphone initialized successfully");
    ESP_LOGI(TAG, "Sound level thresholds:");
    ESP_LOGI(TAG, "  < %d: Background noise", NOISE_THRESHOLD);
    ESP_LOGI(TAG, "  < %d: Quiet", QUIET_THRESHOLD);
    ESP_LOGI(TAG, "  < %d: Medium", MEDIUM_THRESHOLD);
    ESP_LOGI(TAG, "  >= %d: Loud", LOUD_THRESHOLD);
}

const char* get_sound_level_str(int32_t amplitude) {
    if (amplitude < NOISE_THRESHOLD) return "NOISE";
    if (amplitude < QUIET_THRESHOLD) return "QUIET";
    if (amplitude < MEDIUM_THRESHOLD) return "MEDIUM";
    if (amplitude < LOUD_THRESHOLD) return "LOUD";
    return "VERY LOUD";
}

void test_microphone() {
    int16_t audio_buffer[BUFFER_SAMPLES];
    size_t bytes_read = 0;
    uint32_t sample_count = 0;

    while (1) {
        // Read microphone data
        esp_err_t ret = i2s_channel_read(rx_handle, audio_buffer, sizeof(audio_buffer), &bytes_read, portMAX_DELAY);
        if (ret == ESP_OK && bytes_read > 0) {
            // Calculate average amplitude
            int32_t sum = 0;
            for (int i = 0; i < BUFFER_SAMPLES; i++) {
                sum += abs(audio_buffer[i]);
            }
            int32_t average_amplitude = sum / BUFFER_SAMPLES;
            
            // Print sample number and sound level
            sample_count++;
            const char* level = get_sound_level_str(average_amplitude);
            ESP_LOGI(TAG, "Sample #%" PRIu32 " - Level: %s (Amplitude: %" PRId32 ")", 
                    sample_count, level, average_amplitude);

            // Print a visual indicator for very loud sounds
            if (average_amplitude >= LOUD_THRESHOLD) {
                ESP_LOGI(TAG, "!!! LOUD SOUND DETECTED !!!");
            }
        } else {
            ESP_LOGE(TAG, "Microphone read failed: %s", esp_err_to_name(ret));
        }
        
        // Small delay to make the output readable
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main() {
    ESP_LOGI(TAG, "Starting microphone test application");
    init_microphone();
    test_microphone();
} 