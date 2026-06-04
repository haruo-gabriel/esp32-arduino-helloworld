#include <Arduino.h>
#include <Oscil.h>
#include <PmodI2S2.h>
#include <tables/triangle2048_int8.h>

#include "synth_config.h"
#include "voice.h"

// ─────────────────────────────────────────────────────────────────────────────
// Globals
// ─────────────────────────────────────────────────────────────────────────────

// Owns all 8 voices, trigger flags, and the shared envelope level.
VoiceBank bank;

// Driver for the Pmod I2S2.
PmodI2S2 pmod;

// Idle LED pulse oscillator — triangle wave at 1 Hz on a 100 Hz update budget.
Oscil<TRIANGLE2048_NUM_CELLS, SYNTH_CONTROL_RATE> ledOsc(TRIANGLE2048_DATA);

// ─────────────────────────────────────────────────────────────────────────────
// Audio Task  (Core 1)
//
// Fills a stereo-interleaved DMA buffer each cycle. The i2s.write() call
// blocks naturally when the DMA queue is full, pacing synthesis to the exact
// DAC sample rate without a sleep loop.
// ─────────────────────────────────────────────────────────────────────────────
void audioTask(void * /*parameter*/) {
  int16_t buffer[AUDIO_BUFFER_SAMPLES * 2]; // Stereo interleaved: L,R,L,R,...
  int controlCounter = 0;

  while (true) {
    // ── Trigger handling (once per buffer, not per sample) ────────────────
    // Button events arrive at ~100 Hz; checking once per buffer (~5.8 ms
    // latency @ 256 frames) is indistinguishable from per-sample checking.
    bank.processTriggers();

    // ── Sample generation loop ────────────────────────────────────────────
    for (int i = 0; i < AUDIO_BUFFER_SAMPLES; i++) {
      // Advance envelopes at CONTROL_RATE Hz
      if (++controlCounter >= CTRL_DIVIDER) {
        controlCounter = 0;
        bank.updateEnvelopes();
      }

      int16_t sample = bank.nextSample();
      buffer[i * 2] = sample;     // Left channel
      buffer[i * 2 + 1] = sample; // Right channel (mono mix to stereo)
    }

    // Block until the DMA queue has room; this is the synthesis clock.
    pmod.write((const uint8_t *)buffer, sizeof(buffer));
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// setup()
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("ESP32-S3 Mozzi ADSR 8-Voice Polyphonic Synth Starting...");

  // NeoPixel: no pinMode needed with rgbLedWrite; start dark.
  rgbLedWrite(LED_PIN, 0, 0, 0);

  // Configure all button pins with internal pull-up resistors.
  for (int i = 0; i < NUM_VOICES; i++) {
    pinMode(BUTTON_PINS[i], INPUT_PULLUP);
    Serial.printf("GPIO %d configured as INPUT_PULLUP\n", BUTTON_PINS[i]);
  }

  // Idle LED pulse at 1 Hz.
  ledOsc.setFreq(1);

  // Initialise all voices (wave table, frequencies, ADSR shape).
  bank.init();

  // Initialise Pmod I2S2 driver: sclk, lrck, dout, din (unused -> -1), mclk,
  // sample rate.
  if (pmod.begin(PIN_SCLK, PIN_LRCK, PIN_SDIN, -1, PIN_MCLK,
                 SYNTH_AUDIO_RATE)) {
    Serial.println("I2S bus initialised successfully.");
    xTaskCreatePinnedToCore(audioTask,   // Task function
                            "audioTask", // Debug name
                            4096,        // Stack depth (words)
                            nullptr,     // Parameters
                            10,      // Priority (high — audio must not glitch)
                            nullptr, // Task handle (not needed)
                            1        // Pin to Core 1
    );
    Serial.println("Audio task pinned to Core 1.");
  } else {
    Serial.println("ERROR: Failed to initialise I2S bus!");
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// loop()  (Core 0, ~100 Hz)
//
// Polls buttons with hardware debouncing and updates the NeoPixel LED.
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  // ── Button state tracking (static locals — no global pollution) ───────────
  // Three arrays implement a clean debouncer:
  //   stableState  — the last committed pin level (the "truth")
  //   pendingState — the level we saw most recently (may still be bouncing)
  //   pendingTime  — millis() when the pending state first appeared
  static int stableState[NUM_VOICES];
  static int pendingState[NUM_VOICES];
  static unsigned long pendingTime[NUM_VOICES];
  static bool debounceInit = false;

  if (!debounceInit) {
    debounceInit = true;
    for (int i = 0; i < NUM_VOICES; i++) {
      // INPUT_PULLUP → idle line is HIGH; seed both states to avoid a
      // spurious noteOff event on the very first loop iteration.
      stableState[i] = HIGH;
      pendingState[i] = HIGH;
      pendingTime[i] = 0;
    }
  }

  const unsigned long now = millis();

  for (int i = 0; i < NUM_VOICES; i++) {
    const int reading = digitalRead(BUTTON_PINS[i]);

    if (reading == stableState[i]) {
      // Pin is back to its committed level — reset any in-flight pending.
      pendingState[i] = reading;
    } else if (reading != pendingState[i]) {
      // New transition detected — start the debounce timer.
      pendingState[i] = reading;
      pendingTime[i] = now;
    } else if ((now - pendingTime[i]) >= DEBOUNCE_MS) {
      // The new level has been stable for DEBOUNCE_MS — commit it.
      stableState[i] = reading;

      if (reading == LOW) {
        Serial.printf("Button %d (GPIO %d) pressed  → %.2f Hz\n", i + 1,
                      BUTTON_PINS[i], NOTE_FREQS[i]);
        bank.triggerOn(i);
      } else {
        Serial.printf("Button %d (GPIO %d) released → note off\n", i + 1,
                      BUTTON_PINS[i]);
        bank.triggerOff(i);
      }
    }
  }

  // ── NeoPixel LED feedback ─────────────────────────────────────────────────
  // Active: vibrant violet, brightness proportional to the loudest envelope.
  // Idle:   faint green pulse driven by ledOsc.
  const uint8_t envVal = bank.maxEnvLevel;
  if (envVal > 0) {
    const uint8_t brightness = envVal >> 2;          // Scale 0–255 → 0–63
    rgbLedWrite(LED_PIN, brightness, 0, brightness); // Violet
  } else {
    const int8_t oscVal = ledOsc.next();
    const uint8_t pulseBright = (uint8_t)(oscVal + 128) >> 4; // 0–15 (faint)
    rgbLedWrite(LED_PIN, 0, pulseBright, 0);                  // Green pulse
  }

  // Maintain the ~100 Hz loop rate (10 ms per iteration).
  delay(10);
}