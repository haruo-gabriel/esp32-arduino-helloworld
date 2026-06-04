#include "web_server.h"
#include "synth_config.h"
#include <AMY-Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>

// ─────────────────────────────────────────────────────────────────────────────
// Extern references to sequencer state in main.cpp
// ─────────────────────────────────────────────────────────────────────────────
extern bool hihatSteps[NUM_VOICES];
extern bool kickSteps[NUM_VOICES];
extern bool snareSteps[NUM_VOICES];
extern float currentBPM;
extern volatile uint8_t triggeredStep;

// ─────────────────────────────────────────────────────────────────────────────
// Server & WebSocket instances
// ─────────────────────────────────────────────────────────────────────────────
static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

// ─────────────────────────────────────────────────────────────────────────────
// WebSocket helpers
// ─────────────────────────────────────────────────────────────────────────────

void sendState(AsyncWebSocketClient *client) {
  char buf[256];
  snprintf(buf, sizeof(buf),
           "{\"type\":\"state\","
           "\"kick\":[%d,%d,%d,%d,%d,%d,%d,%d],"
           "\"snare\":[%d,%d,%d,%d,%d,%d,%d,%d],"
           "\"hihat\":[%d,%d,%d,%d,%d,%d,%d,%d],"
           "\"bpm\":%d,\"step\":%d}",
           kickSteps[0], kickSteps[1], kickSteps[2], kickSteps[3],
           kickSteps[4], kickSteps[5], kickSteps[6], kickSteps[7],
           snareSteps[0], snareSteps[1], snareSteps[2], snareSteps[3],
           snareSteps[4], snareSteps[5], snareSteps[6], snareSteps[7],
           hihatSteps[0], hihatSteps[1], hihatSteps[2], hihatSteps[3],
           hihatSteps[4], hihatSteps[5], hihatSteps[6], hihatSteps[7],
           (int)currentBPM, (int)triggeredStep);
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

// Defined in main.cpp — updates an AMY sequencer event for a given voice/step
extern void updateAmyStep(int voiceType, int step, bool active);

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
          if (step >= 0 && step < NUM_VOICES) {
            bool *steps = (voice == 0) ? kickSteps
                          : (voice == 1) ? snareSteps
                                         : hihatSteps;
            steps[step] = !steps[step];
            updateAmyStep(voice, step, steps[step]);
            Serial.printf("WS toggle: voice=%d step=%d → %s\n", voice, step,
                          steps[step] ? "ON" : "OFF");
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
            currentBPM = (float)bpm;
            amy_event e = amy_default_event();
            e.tempo = currentBPM;
            amy_add_event(&e);
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
