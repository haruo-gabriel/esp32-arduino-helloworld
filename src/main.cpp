#include "synth_config.h"
#include "web_server.h"
#include "drum_machine.h"
#include "PmodI2S2.h"
#include <AMY-Arduino.h>
#include <Arduino.h>
#include <freertos/semphr.h>

static PmodI2S2 pmod;
SemaphoreHandle_t amy_mutex = NULL;

// Thread-safe wrapper for adding events to the AMY queue
void safe_amy_add_event(amy_event *e) {
  if (amy_mutex) {
    if (xSemaphoreTake(amy_mutex, portMAX_DELAY) == pdTRUE) {
      amy_add_event(e);
      xSemaphoreGive(amy_mutex);
    }
  } else {
    amy_add_event(e);
  }
}

// Background task on Core 1 to render AMY audio and stream to I2S DAC
void audioTask(void *parameter) {
  while (true) {
    int16_t *block = NULL;
    if (amy_mutex && xSemaphoreTake(amy_mutex, portMAX_DELAY) == pdTRUE) {
      block = amy_simple_fill_buffer();
      xSemaphoreGive(amy_mutex);
    }

    if (block) {
      pmod.write((const uint8_t *)block, AMY_BLOCK_SIZE * AMY_NCHANS * sizeof(int16_t));
    }
  }
}

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

  // Initialize the AMY thread-safety mutex
  amy_mutex = xSemaphoreCreateMutex();

  // NeoPixel Setup: start dark
  rgbLedWrite(LED_PIN, 0, 0, 0);

  // ── WiFi & Web Server ─────────────────────────────────────────────────────
  setupWifi();
  setupWebServer();

  // 1. Initialize Pmod I2S2 DAC
  if (!pmod.begin(PIN_SCLK, PIN_LRCK, PIN_SDIN, -1, PIN_MCLK, SYNTH_AUDIO_RATE)) {
    Serial.println("Failed to initialize Pmod I2S2!");
  } else {
    Serial.println("Pmod I2S2 initialized successfully.");
  }

  // 2. Initialize AMY engine configuration (bypassing internal audio drivers and tasks)
  amy_config_t amy_config = amy_default_config();
  amy_config.audio = AMY_AUDIO_IS_NONE;
  amy_config.platform.multithread = 0;
  amy_config.platform.multicore = 0;
  amy_config.features.default_synths = 0; // No need for Juno patches
  // Register the external sequencer hook callback
  amy_config.amy_external_sequencer_hook = my_sequencer_hook;

  // Optimize AMY memory configuration to free internal SRAM for the web server/WiFi
  amy_config.max_oscs = 16;
  amy_config.max_sequencer_tags = 16;
  amy_config.max_voices = 8;
  amy_config.max_synths = 8;
  amy_config.max_memory_patches = 4;

  // Use SPIRAM (PSRAM) for AMY allocations to preserve internal SRAM
  amy_config.ram_caps_events = MALLOC_CAP_SPIRAM;
  amy_config.ram_caps_synth = MALLOC_CAP_SPIRAM;
  amy_config.ram_caps_delay = MALLOC_CAP_SPIRAM;
  amy_config.ram_caps_sample = MALLOC_CAP_SPIRAM;
  amy_config.ram_caps_sysex = MALLOC_CAP_SPIRAM;


  // 3. Start AMY engine (runs synchronously under our custom audioTask)
  amy_start(amy_config);
  Serial.println("AMY synthesis engine started.");

  // 4. Create the background audio task on Core 1
  xTaskCreatePinnedToCore(
    audioTask,
    "audioTask",
    4096,  // Increased stack size from 3072 to 4096 words (16 KB) for safety
    NULL,
    10,    // high priority
    NULL,
    1      // Core 1
  );

  // 5. Initialize the Drum Machine (configures pins, patches, and tempo)
  drumMachineInit(handleDrumMachineStateChange, sendPlayhead);
}

// ─────────────────────────────────────────────────────────────────────────────
// loop() (~100 Hz update rate)
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint >= 5000) {
    lastPrint = millis();
    Serial.printf("Loop running. Free Heap: %d bytes\n", ESP.getFreeHeap());
  }

  // Update drum machine (polls/debounces buttons, updates NeoPixel and AMY)
  drumMachineUpdate();

  // Clean up disconnected WebSocket clients (prevents memory leaks)
  cleanupWebSocket();

  // Maintain ~100 Hz loop rate
  delay(10);
}