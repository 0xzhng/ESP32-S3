#include <driver/i2s.h>
#include <esp_event.h>
#include <esp_log.h>
#include <opus.h>
#include <string.h>

#include "main.h"

// External function declarations from media.cpp for audio handling
void init_audio_encoder();
void send_audio(PeerConnection* peer_connection);
void audio_decode(uint8_t* data, size_t size);

// External function declaration from http.cpp for fetching
void http_request(char* offer, char* answer);

// Configuration constants
#define LOG_DATACHANNEL_MESSAGES  // Enable logging of data channel messages
#define TICK_INTERVAL 15          // WebRTC loop interval in milliseconds
// Initial greeting message sent when data channel opens
#define GREETING                                                             \
  "{\"type\": \"response.create\", \"response\": {\"modalities\": "          \
  "[\"audio\", \"text\"], \"instructions\": \" say 'Hello There, Im your "   \
  "personal voice assistant, running on a ESP32-S3 embedded device,' Hello " \
  "There, be a friendly assistant, speak english unless told "               \
  "specifically'\"}}"

// Global peer connection instance
PeerConnection* peer_connection = NULL;

// Task configuration for audio publishing
StaticTask_t task_buffer;

// Audio publisher task - continuously sends audio data over WebRTC
// Runs as a separate task to ensure real-time audio streaming
void audio_publisher_task(void* user_data) {
  init_audio_encoder();

  while (1) {
    send_audio(peer_connection);
    vTaskDelay(pdMS_TO_TICKS(TICK_INTERVAL));
  }
}

// Handles incoming messages on the data channel
// msg: received message content
// len: message length
// userdata: user context (unused)
// sid: stream ID
static void handle_datachannel_message(char* msg, size_t len, void* userdata,
                                       uint16_t sid) {
#ifdef LOG_DATACHANNEL_MESSAGES
  ESP_LOGI(LOG_TAG, "DataChannel Message: %s", msg);
#endif
}

// Handles data channel open event
// Creates a reliable data channel and sends initial greeting
static void handle_datachannel_open(void* userdata) {
  if (peer_connection_create_datachannel(peer_connection, DATA_CHANNEL_RELIABLE,
                                         0, 0, (char*)"events",
                                         (char*)"") != -1) {
    ESP_LOGI(LOG_TAG, "DataChannel created");
    // Send initial greeting message
    peer_connection_datachannel_send(peer_connection, (char*)GREETING,
                                     strlen(GREETING));
  } else {
    ESP_LOGE(LOG_TAG, "Datachannel failed to create");
  }
}

// Handles WebRTC connection state changes
// Restarts ESP on disconnect, starts audio task on connect
static void handle_connection_state_change(PeerConnectionState state,
                                           void* user_data) {
  ESP_LOGI(LOG_TAG, "PeerConnectionState: %s",
           peer_connection_state_to_string(state));

  if (state == PEER_CONNECTION_DISCONNECTED ||
      state == PEER_CONNECTION_CLOSED) {
    esp_restart();  // Restart ESP on connection loss
  } else if (state == PEER_CONNECTION_CONNECTED) {
    // Create audio publisher task in PSRAM with high priority
    StackType_t* stack_memory = (StackType_t*)heap_caps_malloc(
        20000 * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
    xTaskCreateStaticPinnedToCore(audio_publisher_task, "audio_publisher",
                                  20000, NULL, 7, stack_memory, &task_buffer,
                                  0);
  }
}

// Handles ICE candidate events
// Performs HTTP request for signaling and sets remote description
static void handle_ice_candidate(char* description, void* user_data) {
  char local_buffer[MAX_HTTP_OUTPUT_BUFFER + 1] = {0};
  http_request(description, local_buffer);
  peer_connection_set_remote_description(peer_connection, local_buffer);
}

// Main WebRTC initialization and event loop
void webrtc() {
  // Configure WebRTC peer connection
  PeerConfiguration peer_connection_config = {
      .ice_servers = {},                   // No STUN/TURN servers needed
      .audio_codec = CODEC_OPUS,           // Using Opus for audio
      .video_codec = CODEC_NONE,           // No video support
      .datachannel = DATA_CHANNEL_STRING,  // Text-based data channel
      .onaudiotrack = [](uint8_t* data, size_t size, void* userdata) -> void {
        audio_decode(data, size);  // Handle incoming audio
      },
      .onvideotrack = NULL,
      .on_request_keyframe = NULL,
      .user_data = NULL,
  };

  // Create peer connection
  peer_connection = peer_connection_create(&peer_connection_config);
  if (peer_connection == NULL) {
    ESP_LOGE(LOG_TAG, "peer connection failed to create");
    esp_restart();
  }

  // Set up event handlers
  peer_connection_oniceconnectionstatechange(peer_connection,
                                             handle_connection_state_change);
  peer_connection_onicecandidate(peer_connection, handle_ice_candidate);
  peer_connection_ondatachannel(peer_connection, handle_datachannel_message,
                                handle_datachannel_open, NULL);

  // Start connection process
  peer_connection_create_offer(peer_connection);

  // Main WebRTC event loop
  while (1) {
    peer_connection_loop(peer_connection);
    vTaskDelay(pdMS_TO_TICKS(TICK_INTERVAL));
  }
}
