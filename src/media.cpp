#include <driver/i2s.h>
#include <opus.h>

#include "main.h"

// Buffer and sampling configuration
#define OPUS_OUT_BUFFER_SIZE \
  1276  // Maximum size for Opus encoded data (recommended by opus_encode)
#define SAMPLE_RATE 8000    // Audio sampling rate in Hz
#define BUFFER_SAMPLES 320  // Number of samples per buffer (40ms at 8kHz)

// MAX98357A amplifier pin configuration
#define MCLK_PIN 0        // Master clock
#define DAC_BCLK_PIN 20   // Bit clock
#define DAC_LRCLK_PIN 21  // Word select / Left-right clock
#define DAC_DATA_PIN 19   // Data output

// INMP441 microphone pin configuration
#define ADC_BCLK_PIN 47   // Bit clock
#define ADC_LRCLK_PIN 41  // Word select / Left-right clock
#define ADC_DATA_PIN 45   // Data input

// Opus codec configuration
#define OPUS_ENCODER_BITRATE 30000  // Encoding bitrate in bits per second
#define OPUS_ENCODER_COMPLEXITY 0   // Lower complexity for better performance

// Function declarations for audio processing
void init_audio_capture();  // Initialize I2S for input/output
void init_audio_decoder();  // Set up Opus decoder
void audio_decode(uint8_t* data, size_t size);     // Process incoming audio
void init_audio_encoder();                         // Set up Opus encoder
void send_audio(PeerConnection* peer_connection);  // Process and send audio

// Initialize I2S drivers for audio input (INMP441) and output (MAX98357A)
void init_audio_capture() {
  // Configure I2S output for MAX98357A amplifier
  i2s_config_t i2s_config_out = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,  // Stereo output
      .communication_format = I2S_COMM_FORMAT_I2S_MSB,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,             // Number of DMA buffers
      .dma_buf_len = BUFFER_SAMPLES,  // Samples per DMA buffer
      .use_apll = 1,                  // Use APLL for better quality
      .tx_desc_auto_clear = true,     // Auto-clear DMA buffers
  };
  if (i2s_driver_install(I2S_NUM_0, &i2s_config_out, 0, NULL) != ESP_OK) {
    printf("Failed to configure I2S driver for audio output");
    return;
  }

  // Set up output pins for MAX98357A
  i2s_pin_config_t pin_config_out = {
      .mck_io_num = MCLK_PIN,
      .bck_io_num = DAC_BCLK_PIN,
      .ws_io_num = DAC_LRCLK_PIN,
      .data_out_num = DAC_DATA_PIN,
      .data_in_num = I2S_PIN_NO_CHANGE,
  };
  if (i2s_set_pin(I2S_NUM_0, &pin_config_out) != ESP_OK) {
    printf("Failed to set I2S pins for audio output");
    return;
  }
  i2s_zero_dma_buffer(I2S_NUM_0);  // Clear output buffer

  // Configure I2S input for INMP441 microphone
  i2s_config_t i2s_config_in = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format =
          I2S_CHANNEL_FMT_ONLY_LEFT,  // Mono input from mic (one channel input
                                      // only for inmp441)
      .communication_format = I2S_COMM_FORMAT_I2S_MSB,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,  // Optimized buffer count
      .dma_buf_len = BUFFER_SAMPLES,
      .use_apll = 1,
      .fixed_mclk = 0};
  if (i2s_driver_install(I2S_NUM_1, &i2s_config_in, 0, NULL) != ESP_OK) {
    printf("Failed to configure I2S driver for audio input");
    return;
  }

  // Set up input pins for INMP441
  i2s_pin_config_t pin_config_in = {
      .mck_io_num = MCLK_PIN,
      .bck_io_num = ADC_BCLK_PIN,
      .ws_io_num = ADC_LRCLK_PIN,
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = ADC_DATA_PIN,
  };
  if (i2s_set_pin(I2S_NUM_1, &pin_config_in) != ESP_OK) {
    printf("Failed to set I2S pins for audio input");
    return;
  }
}

// Buffers and decoder for audio output
opus_int16* output_buffer = NULL;
OpusDecoder* opus_decoder = NULL;

// Initialize Opus decoder for incoming audio
void init_audio_decoder() {
  int decoder_error = 0;
  opus_decoder =
      opus_decoder_create(SAMPLE_RATE, 2, &decoder_error);  // Stereo output
  if (decoder_error != OPUS_OK) {
    printf("Failed to create OPUS decoder");
    return;
  }

  output_buffer = (opus_int16*)malloc(BUFFER_SAMPLES * sizeof(opus_int16));
}

// Process incoming audio data and output to MAX98357A
void audio_decode(uint8_t* data, size_t size) {
  // Decode Opus audio data to PCM
  int decoded_size =
      opus_decode(opus_decoder, data, size, output_buffer, BUFFER_SAMPLES, 0);

  if (decoded_size > 0) {
    // Output decoded audio through I2S
    size_t bytes_written = 0;
    i2s_write(I2S_NUM_0, output_buffer, BUFFER_SAMPLES * sizeof(opus_int16),
              &bytes_written, portMAX_DELAY);
  }
}

// Buffers and encoder for audio input
OpusEncoder* opus_encoder = NULL;
opus_int16* encoder_input_buffer = NULL;
uint8_t* encoder_output_buffer = NULL;

// Initialize Opus encoder for outgoing audio
void init_audio_encoder() {
  int encoder_error;
  // Create mono encoder optimized for voice
  opus_encoder = opus_encoder_create(SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP,
                                     &encoder_error);
  if (encoder_error != OPUS_OK) {
    printf("Failed to create OPUS encoder");
    return;
  }

  if (opus_encoder_init(opus_encoder, SAMPLE_RATE, 1, OPUS_APPLICATION_VOIP) !=
      OPUS_OK) {
    printf("Failed to initialize OPUS encoder");
    return;
  }

  // Configure encoder parameters
  opus_encoder_ctl(opus_encoder,
                   OPUS_SET_BITRATE(30000));  // Set bitrate for quality
  opus_encoder_ctl(opus_encoder,
                   OPUS_SET_COMPLEXITY(0));  // Low complexity for performance
  opus_encoder_ctl(opus_encoder,
                   OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));  // Optimize for voice
  opus_encoder_ctl(opus_encoder,
                   OPUS_SET_GAIN(500));  // Apply gain to boost volume

  // Allocate buffers for audio processing
  encoder_input_buffer =
      (opus_int16*)malloc(BUFFER_SAMPLES * sizeof(opus_int16));
  encoder_output_buffer = (uint8_t*)malloc(OPUS_OUT_BUFFER_SIZE);
}

// Capture audio from INMP441, encode, and send through WebRTC
void send_audio(PeerConnection* peer_connection) {
  size_t bytes_read = 0;

  // Read audio data from I2S
  i2s_read(I2S_NUM_1, encoder_input_buffer, BUFFER_SAMPLES, &bytes_read,
           portMAX_DELAY);

  // Encode audio data using Opus
  auto encoded_size =
      opus_encode(opus_encoder, encoder_input_buffer, BUFFER_SAMPLES / 2,
                  encoder_output_buffer, OPUS_OUT_BUFFER_SIZE);

  // Send encoded audio through WebRTC
  peer_connection_send_audio(peer_connection, encoder_output_buffer,
                             encoded_size);
}