#include <driver/i2s.h>
#include <opus.h>
#include "media.h"
#include "esp_log.h"
#include <cstring>

#define OPUS_OUT_BUFFER_SIZE 1276  // 1276 bytes is recommended by opus_encode
#define SAMPLE_RATE 16000  // Increased from 8000 for better quality
#define BUFFER_SAMPLES 480  // Increased from 320 for better range

// For the MAX98357A
#define MCLK_PIN 0
#define DAC_BCLK_PIN 20
#define DAC_LRCLK_PIN 21
#define DAC_DATA_PIN 19

// For the INMP441
#define ADC_BCLK_PIN 47 
#define ADC_LRCLK_PIN 41
#define ADC_DATA_PIN 45

#define OPUS_ENCODER_BITRATE 64000  // Increased from 30000 for better quality
#define OPUS_ENCODER_COMPLEXITY 10   // Increased from 0 for better quality

#define VOLUME_MULTIPLIER 16  // Maximum practical amplification without excessive distortion

// Echo cancellation parameters
#define ECHO_BUFFER_LENGTH (SAMPLE_RATE/4)  // 250ms echo buffer
#define ECHO_DECAY 0.7f                     // Echo decay factor
#define ECHO_THRESHOLD 1000                 // Minimum amplitude to consider as echo

static const char* TAG = "AUDIO_DEBUG";

// Buffers for audio processing
static opus_int16 *input_buffer = NULL;
static opus_int16 *output_buffer = NULL;
static uint8_t *encoded_buffer = NULL;
static OpusEncoder *opus_encoder = NULL;
static OpusDecoder *opus_decoder = NULL;

// Echo cancellation buffer
static opus_int16 *echo_buffer = NULL;
static int echo_write_pos = 0;

void init_audio_capture() {
    ESP_LOGI(TAG, "Initializing audio capture...");
    
    i2s_config_t i2s_config_out = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = BUFFER_SAMPLES,
        .use_apll = true,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };
    
    if (i2s_driver_install(I2S_NUM_0, &i2s_config_out, 0, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install I2S driver for output");
        return;
    }

    i2s_pin_config_t pin_config_out = {
        .mck_io_num = MCLK_PIN,
        .bck_io_num = DAC_BCLK_PIN,
        .ws_io_num = DAC_LRCLK_PIN,
        .data_out_num = DAC_DATA_PIN,
        .data_in_num = I2S_PIN_NO_CHANGE
    };
    
    if (i2s_set_pin(I2S_NUM_0, &pin_config_out) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set I2S pins for output");
        return;
    }
    i2s_zero_dma_buffer(I2S_NUM_0);

    i2s_config_t i2s_config_in = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,  
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = BUFFER_SAMPLES,
        .use_apll = true,
        .fixed_mclk = 0
    };

    if (i2s_driver_install(I2S_NUM_1, &i2s_config_in, 0, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install I2S driver for input");
        return;
    }

    i2s_pin_config_t pin_config_in = {
        .mck_io_num = I2S_PIN_NO_CHANGE,
        .bck_io_num = ADC_BCLK_PIN,
        .ws_io_num = ADC_LRCLK_PIN,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = ADC_DATA_PIN
    };

    if (i2s_set_pin(I2S_NUM_1, &pin_config_in) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set I2S pins for input");
        return;
    }

    // Initialize audio buffers
    input_buffer = (opus_int16 *)heap_caps_malloc(BUFFER_SAMPLES * sizeof(opus_int16), MALLOC_CAP_8BIT);
    output_buffer = (opus_int16 *)heap_caps_malloc(BUFFER_SAMPLES * sizeof(opus_int16), MALLOC_CAP_8BIT);
    encoded_buffer = (uint8_t *)heap_caps_malloc(OPUS_OUT_BUFFER_SIZE, MALLOC_CAP_8BIT);
    echo_buffer = (opus_int16 *)heap_caps_calloc(ECHO_BUFFER_LENGTH, sizeof(opus_int16), MALLOC_CAP_8BIT);

    if (!input_buffer || !output_buffer || !encoded_buffer || !echo_buffer) {
        ESP_LOGE(TAG, "Failed to allocate audio buffers");
        return;
    }

    ESP_LOGI(TAG, "Audio initialization complete with echo cancellation");
}

// Function to apply echo cancellation
static void apply_echo_cancellation(opus_int16* input, int length) {
    for(int i = 0; i < length; i++) {
        // Calculate echo buffer position for this sample
        int echo_pos = (echo_write_pos - SAMPLE_RATE/10 + i) % ECHO_BUFFER_LENGTH; // 100ms delay
        if(echo_pos < 0) echo_pos += ECHO_BUFFER_LENGTH;
        
        // Read the echo sample
        opus_int16 echo_sample = echo_buffer[echo_pos];
        
        // Only cancel if echo is above threshold
        if(abs(echo_sample) > ECHO_THRESHOLD) {
            // Subtract scaled echo from input
            int32_t result = input[i] - (int32_t)(echo_sample * ECHO_DECAY);
            // Clamp to 16-bit range
            if(result > 32767) result = 32767;
            if(result < -32768) result = -32768;
            input[i] = (opus_int16)result;
        }
        
        // Store current output for future echo cancellation
        echo_buffer[echo_write_pos] = input[i];
        echo_write_pos = (echo_write_pos + 1) % ECHO_BUFFER_LENGTH;
    }
}

void audio_loopback() {
    size_t bytes_read = 0;
    size_t bytes_written = 0;
    
    // Read from microphone
    if (i2s_read(I2S_NUM_1, input_buffer, BUFFER_SAMPLES * sizeof(int16_t), &bytes_read, portMAX_DELAY) != ESP_OK) {
        ESP_LOGE(TAG, "Error reading from microphone");
        return;
    }

    // Apply echo cancellation
    apply_echo_cancellation(input_buffer, BUFFER_SAMPLES);

    // Apply volume amplification
    for(int i = 0; i < BUFFER_SAMPLES; i++) {
        int32_t amplified = input_buffer[i] * VOLUME_MULTIPLIER;
        if(amplified > 32767) amplified = 32767;
        if(amplified < -32768) amplified = -32768;
        output_buffer[i] = (int16_t)amplified;
    }

    // Convert mono to stereo for MAX98357A
    int16_t stereo_buffer[BUFFER_SAMPLES * 2];
    for(int i = 0; i < BUFFER_SAMPLES; i++) {
        stereo_buffer[i*2] = output_buffer[i];     // Left channel
        stereo_buffer[i*2+1] = output_buffer[i];   // Right channel
    }

    // Write to speaker
    if (i2s_write(I2S_NUM_0, stereo_buffer, BUFFER_SAMPLES * 4, &bytes_written, portMAX_DELAY) != ESP_OK) {
        ESP_LOGE(TAG, "Error writing to speaker");
        return;
    }

    // Debug output
    static int debug_counter = 0;
    if (++debug_counter >= 100) {
        ESP_LOGI(TAG, "Audio: Read %d bytes, Wrote %d bytes", bytes_read, bytes_written);
        debug_counter = 0;
    }
}

void init_audio_encoder() {
    ESP_LOGI(TAG, "Initializing Opus encoder...");
    
    int encoder_error;
    opus_encoder = opus_encoder_create(SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP, &encoder_error);
    if (encoder_error != OPUS_OK) {
        ESP_LOGE(TAG, "Failed to create Opus encoder: %d", encoder_error);
        return;
    }

    opus_encoder_ctl(opus_encoder, OPUS_SET_BITRATE(OPUS_ENCODER_BITRATE));
    opus_encoder_ctl(opus_encoder, OPUS_SET_COMPLEXITY(OPUS_ENCODER_COMPLEXITY));
    opus_encoder_ctl(opus_encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(opus_encoder, OPUS_SET_GAIN(2000));  // Increased from 500 for better range
    opus_encoder_ctl(opus_encoder, OPUS_SET_VBR(1));  // Enable variable bitrate for better quality
    opus_encoder_ctl(opus_encoder, OPUS_SET_VBR_CONSTRAINT(0));  // Disable VBR constraint for better range
    opus_encoder_ctl(opus_encoder, OPUS_SET_FORCE_CHANNELS(1));  // Force mono for better range

    ESP_LOGI(TAG, "Opus encoder initialized with enhanced settings");
}

void init_audio_decoder() {
    ESP_LOGI(TAG, "Initializing Opus decoder...");
    
    int decoder_error;
    opus_decoder = opus_decoder_create(SAMPLE_RATE, 1, &decoder_error);
    if (decoder_error != OPUS_OK) {
        ESP_LOGE(TAG, "Failed to create Opus decoder: %d", decoder_error);
        return;
    }

    ESP_LOGI(TAG, "Opus decoder initialized");
} 