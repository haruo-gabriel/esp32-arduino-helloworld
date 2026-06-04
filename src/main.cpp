#include "synth_config.h"
#include "web_server.h"
#include "drum_machine.h"
#include <AMY-Arduino.h>
#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
// setup()
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  // Wait up to 4 seconds for native USB CDC serial port to connect
  while (!Serial && millis() < 4000) {
    delay(10);
  }
  Serial.println("\nESP32-S3 AMY Sequencer Starting...");

  // NeoPixel Setup: start dark
  rgbLedWrite(LED_PIN, 0, 0, 0);

  // ── WiFi & Web Server ─────────────────────────────────────────────────────
  setupWifi();
  setupWebServer();

  // 1. Initialize AMY engine configuration
  amy_config_t amy_config = amy_default_config();
  amy_config.i2s_bclk = PIN_SCLK;
  amy_config.i2s_lrc = PIN_LRCK;
  amy_config.i2s_dout = PIN_SDIN;
  amy_config.i2s_mclk = PIN_MCLK;
  amy_config.i2s_din = -1;
  amy_config.audio = AMY_AUDIO_IS_I2S;
  amy_config.features.default_synths = 0; // No need for Juno patches
  // Register the external sequencer hook callback
  amy_config.amy_external_sequencer_hook = my_sequencer_hook;

  // 2. Start AMY engine (this launches background rendering task)
  amy_start(amy_config);
  Serial.println("AMY synthesis engine started.");

  // 3. Initialize the Drum Machine (configures pins, patches, and tempo)
  drumMachineInit(handleDrumMachineStateChange, sendPlayhead);
}

// ─────────────────────────────────────────────────────────────────────────────
// loop() (~100 Hz update rate)
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  // Update drum machine (polls/debounces buttons, updates NeoPixel and AMY)
  drumMachineUpdate();

  // Clean up disconnected WebSocket clients (prevents memory leaks)
  cleanupWebSocket();

  // Maintain ~100 Hz loop rate
  delay(10);
}