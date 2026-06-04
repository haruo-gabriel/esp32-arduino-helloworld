#include "web_server.h"
#include "synth_config.h"
#include "drum_machine.h"
#include <AMY-Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>

// ─────────────────────────────────────────────────────────────────────────────
// Server & WebSocket instances
// ─────────────────────────────────────────────────────────────────────────────
static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

// ─────────────────────────────────────────────────────────────────────────────
// WebSocket helpers
// ─────────────────────────────────────────────────────────────────────────────

void sendState(AsyncWebSocketClient *client) {
  const bool* kick = drumMachineGetSteps(0);
  const bool* snare = drumMachineGetSteps(1);
  const bool* hihat = drumMachineGetSteps(2);
  int bpm = (int)drumMachineGetBPM();
  int step = (int)drumMachineGetStep();

  char buf[256];
  snprintf(buf, sizeof(buf),
           "{\"type\":\"state\","
           "\"kick\":[%d,%d,%d,%d,%d,%d,%d,%d],"
           "\"snare\":[%d,%d,%d,%d,%d,%d,%d,%d],"
           "\"hihat\":[%d,%d,%d,%d,%d,%d,%d,%d],"
           "\"bpm\":%d,\"step\":%d}",
           kick ? kick[0] : 0, kick ? kick[1] : 0, kick ? kick[2] : 0, kick ? kick[3] : 0,
           kick ? kick[4] : 0, kick ? kick[5] : 0, kick ? kick[6] : 0, kick ? kick[7] : 0,
           snare ? snare[0] : 0, snare ? snare[1] : 0, snare ? snare[2] : 0, snare ? snare[3] : 0,
           snare ? snare[4] : 0, snare ? snare[5] : 0, snare ? snare[6] : 0, snare ? snare[7] : 0,
           hihat ? hihat[0] : 0, hihat ? hihat[1] : 0, hihat ? hihat[2] : 0, hihat ? hihat[3] : 0,
           hihat ? hihat[4] : 0, hihat ? hihat[5] : 0, hihat ? hihat[6] : 0, hihat ? hihat[7] : 0,
           bpm, step);
  if (client) {
    client->text(buf);
  } else {
    ws.textAll(buf);
  }
}

void sendPlayhead(uint8_t step) {
  char buf[32];
  snprintf(buf, sizeof(buf), "{\"type\":\"step\",\"index\":%d}", step);
  ws.textAll(buf);
}

// ─────────────────────────────────────────────────────────────────────────────
// WebSocket event handler
// ─────────────────────────────────────────────────────────────────────────────

static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data,
                      size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WebSocket client #%u connected from %s\n", client->id(),
                  client->remoteIP().toString().c_str());
    sendState(client);
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->final && info->index == 0 && info->len == len &&
        info->opcode == WS_TEXT) {
      data[len] = 0; // Null-terminate

      // Minimal JSON parsing (avoid heavy library to save memory)
      String msg = (char *)data;

      if (msg.indexOf("\"toggle\"") >= 0) {
        // Parse voice and step from: {"type":"toggle","voice":0,"step":3}
        int vi = msg.indexOf("\"voice\":");
        int si = msg.indexOf("\"step\":");
        if (vi >= 0 && si >= 0) {
          int voice = msg.substring(vi + 8).toInt();
          int step = msg.substring(si + 7).toInt();
          if (step >= 0 && step < NUM_STEPS) {
            drumMachineToggleStep(voice, step);
            Serial.printf("WS toggle: voice=%d step=%d\n", voice, step);
            sendState(); // Broadcast to all clients
          }
        }
      } else if (msg.indexOf("\"bpm\"") >= 0 &&
                 msg.indexOf("\"get_state\"") < 0) {
        // Parse BPM from: {"type":"bpm","value":120}
        int vi = msg.indexOf("\"value\":");
        if (vi >= 0) {
          int bpm = msg.substring(vi + 8).toInt();
          if (bpm >= 60 && bpm <= 600) {
            drumMachineSetBPM((float)bpm);
            Serial.printf("WS BPM changed to %d\n", bpm);
            sendState(); // Broadcast new BPM
          }
        }
      } else if (msg.indexOf("\"get_state\"") >= 0) {
        sendState(client);
      }
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void setupWifi() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32-Sequencer");
  Serial.println("WiFi Access Point 'ESP32-Sequencer' started.");
  Serial.printf("Connect your device to it and open: http://%s/\n",
                WiFi.softAPIP().toString().c_str());
}

void setupWebServer() {
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed!");
  } else {
    Serial.println("LittleFS mounted.");
  }

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.begin();
  Serial.println("Web server started on port 80.");
}

void cleanupWebSocket() { ws.cleanupClients(); }

void handleDrumMachineStateChange() {
  sendState();
}
