#include "synth_config.h"
#include <AMY-Arduino.h>
#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
// MIDI note mapping for the 8 buttons (matching the frequencies in
// synth_config.h) C4 (60), Db4 (61), E4 (64), F4 (65), G4 (67), Ab4 (68), B4
// (71), C5 (72)
// ─────────────────────────────────────────────────────────────────────────────
const uint8_t BUTTON_NOTES[NUM_VOICES] = {60, 61, 64, 65, 67, 68, 71, 72};

// ─────────────────────────────────────────────────────────────────────────────
// setup()
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("ESP32-S3 AMY Synthesizer Starting...");

  // NeoPixel Setup: start dark
  rgbLedWrite(LED_PIN, 0, 0, 0);

  // Configure all button pins with internal pull-up resistors
  for (int i = 0; i < NUM_VOICES; i++) {
    pinMode(BUTTON_PINS[i], INPUT_PULLUP);
    Serial.printf("GPIO %d configured as INPUT_PULLUP\n", BUTTON_PINS[i]);
  }

  // 1. Initialize AMY engine configuration
  amy_config_t amy_config = amy_default_config();
  amy_config.i2s_bclk = PIN_SCLK;
  amy_config.i2s_lrc = PIN_LRCK;
  amy_config.i2s_dout = PIN_SDIN;
  amy_config.i2s_mclk = PIN_MCLK;
  amy_config.i2s_din = -1;
  amy_config.audio = AMY_AUDIO_IS_I2S;
  amy_config.features.default_synths =
      0; // Do NOT auto-load defaults: alloc_osc() crashes before pool is ready

  // 2. Start AMY engine (this launches background rendering task)
  amy_start(amy_config);
  Serial.println("AMY synthesis engine started.");

  // 3. Reset the engine and initialize Juno voice configuration on channel 1
  // (synth 1)
  amy_event e = amy_default_event();
  e.reset_osc = RESET_AMY;
  amy_add_event(&e);
  delay(50); // Let the reset complete

  e = amy_default_event();
  e.synth = 1;
  e.patch_number = 0; // Juno-6 patch 0 (rich chorus synth)
  e.num_voices = 8;
  amy_add_event(&e);
  Serial.println("Juno-6 patch 0 allocated with 8 voices on synth 1.");
}

// ─────────────────────────────────────────────────────────────────────────────
// loop() (~100 Hz update rate)
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  // ── Button state tracking with hardware debouncing ────────────────────────
  static int stableState[NUM_VOICES];
  static int pendingState[NUM_VOICES];
  static unsigned long pendingTime[NUM_VOICES];
  static bool debounceInit = false;

  if (!debounceInit) {
    debounceInit = true;
    for (int i = 0; i < NUM_VOICES; i++) {
      stableState[i] = HIGH;
      pendingState[i] = HIGH;
      pendingTime[i] = 0;
    }
  }

  const unsigned long now = millis();
  bool anyButtonPressed = false;

  for (int i = 0; i < NUM_VOICES; i++) {
    const int reading = digitalRead(BUTTON_PINS[i]);

    if (reading == stableState[i]) {
      pendingState[i] = reading;
    } else if (reading != pendingState[i]) {
      pendingState[i] = reading;
      pendingTime[i] = now;
    } else if ((now - pendingTime[i]) >= DEBOUNCE_MS) {
      stableState[i] = reading;

      // Create an event to trigger note on or note off
      amy_event e = amy_default_event();
      e.synth = 1;
      e.midi_note = BUTTON_NOTES[i];

      if (reading == LOW) {
        Serial.printf("Button %d (GPIO %d) pressed  → MIDI %d (Note On)\n",
                      i + 1, BUTTON_PINS[i], BUTTON_NOTES[i]);
        e.velocity = 1.0f;
        amy_add_event(&e);
      } else {
        Serial.printf("Button %d (GPIO %d) released → MIDI %d (Note Off)\n",
                      i + 1, BUTTON_PINS[i], BUTTON_NOTES[i]);
        e.velocity = 0.0f;
        amy_add_event(&e);
      }
    }

    if (stableState[i] == LOW) {
      anyButtonPressed = true;
    }
  }

  // ── NeoPixel LED feedback ─────────────────────────────────────────────────
  // Active: vibrant violet, fading out after release.
  // Idle:   faint green pulse.
  static float ledBrightness = 0.0f;
  static uint32_t pulseTime = 0;

  if (anyButtonPressed) {
    ledBrightness = 63.0f; // Maximum active brightness scale (0-63)
  } else {
    ledBrightness -= 0.5f; // Decay speed
    if (ledBrightness < 0.0f)
      ledBrightness = 0.0f;
  }

  if (ledBrightness > 0.0f) {
    rgbLedWrite(LED_PIN, (uint8_t)ledBrightness, 0,
                (uint8_t)ledBrightness); // Violet
  } else {
    // Generate a 1 Hz triangle wave pulse for idle feedback
    pulseTime++;
    uint32_t step = pulseTime % 100; // 100 steps per cycle (1 Hz @ 100 Hz loop)
    float val;
    if (step < 50) {
      val = step * 5.1f;
    } else {
      val = (100 - step) * 5.1f;
    }
    const uint8_t pulseBright = (uint8_t)val >> 4; // Faint green (0-15)
    rgbLedWrite(LED_PIN, 0, pulseBright, 0);
  }

  // Call AMY update function to run the sequencer and event queue processing
  amy_update();

  // Maintain ~100 Hz loop rate
  delay(10);
}