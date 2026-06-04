#pragma once
#include <cstdint>

class AsyncWebSocketClient;

// Initialize WiFi in Access Point mode
void setupWifi();

// Mount LittleFS, register WebSocket handler, start HTTP server
void setupWebServer();

// Call from loop() — cleans up disconnected WebSocket clients
void cleanupWebSocket();

// Broadcast full sequencer state to one client (or all if nullptr)
void sendState(AsyncWebSocketClient *client = nullptr);

// Broadcast current playhead step to all clients
void sendPlayhead(uint8_t step);
