#ifndef MAIN_H
#define MAIN_H

#include <peer.h>

// Project identification and logging
#define LOG_TAG "ESP32S3-embedded-TEJ4"  // Tag used for ESP logging system

// Buffer configuration
#define MAX_HTTP_OUTPUT_BUFFER 2048  // Maximum size for HTTP response data

// Network and connectivity functions
void wifi(void);  // Initialize and connect WiFi
void webrtc();    // Set up and manage WebRTC connection
void http_request(char* offer,
                  char* answer);  // Handle HTTP communication with OpenAI API

// Audio system initialization
void init_audio_capture(void);  // Set up I2S for INMP441 and MAX98357A
void init_audio_decoder(void);  // Initialize Opus decoder for incoming audio
void init_audio_encoder();      // Initialize Opus encoder for outgoing audio

// Audio processing functions
void send_audio(
    PeerConnection* peer_connection);  // Capture and send audio through WebRTC
void audio_decode(uint8_t* data,
                  size_t size);  // Process and play received audio

#endif  // MAIN_H
