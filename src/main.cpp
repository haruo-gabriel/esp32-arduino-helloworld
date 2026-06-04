#include "synth_config.h"
#include <AMY-Arduino.h>
#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
// Sequencer State & Hook variables
// ─────────────────────────────────────────────────────────────────────────────
// Sequencer active step grid (8 steps)
static bool stepActive[NUM_VOICES] = {false};

// Thread-safe volatile flags for step boundary synchronization
volatile bool stepTriggered = false;
volatile uint8_t triggeredStep = 0;

// Callback triggered by AMY's background hardware timer clock
void my_sequencer_hook(uint32_t tick_count) {
  // Each step represents an 8th note.
  // At 48 PPQ (ticks per quarter note), an 8th note is 24 ticks.
  // The total period for 8 steps is 8 * 24 = 192 ticks.
  if (tick_count % 24 == 0) {
    triggeredStep = (tick_count % 192) / 24;
    stepTriggered = true;
  }
}

// Configure AMY oscillator 0 to synthesize a sharp analog-style closed hi-hat
void setupHiHatPatch() {
  amy_event e = amy_default_event();
  e.osc = 0;
  e.wave = NOISE;
  e.filter_type = FILTER_BPF;
  e.filter_freq_coefs[COEF_CONST] = 11000.0f; // 11 kHz band-pass filter
  e.resonance = 6.0f;             // High resonance for metallic sizzle
  e.amp_coefs[COEF_CONST] = 0.0f; // Base amplitude is 0
  e.amp_coefs[COEF_EG0] = 1.0f;   // Amplitude modulated by Envelope 0

  // EG0: Rapid exponential decay for a sharp hi-hat sound
  e.eg0_times[0] = 0;
  e.eg0_values[0] = 1.0f; // Instant attack (1.0 level)
  e.eg0_times[1] = 30;
  e.eg0_values[1] = 0.0f; // Rapid decay to 0.0 in 30ms
  e.eg0_times[2] = 0;
  e.eg0_values[2] = 0.0f; // End breakpoint
  e.bp_is_set[0] = 1;     // Enable EG0

  amy_add_event(&e);
  Serial.println("Synthesized hi-hat patch configured on oscillator 0.");
}

// ─────────────────────────────────────────────────────────────────────────────
// setup()
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("ESP32-S3 AMY Sequencer Starting...");

  // NeoPixel Setup: start dark
  rgbLedWrite(LED_PIN, 0, 0, 0);

  // Configure all button pins with internal pull-up resistors
  for (int i = 0; i < NUM_VOICES; i++) {
    pinMode(BUTTON_PINS[i], INPUT_PULLUP);
    Serial.printf("GPIO %d configured as INPUT_PULLUP (Step %d Toggle)\n",
                  BUTTON_PINS[i], i + 1);
  }

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

  // 3. Reset the engine and initialize Hi-Hat patch configuration
  amy_event e = amy_default_event();
  e.reset_osc = RESET_AMY;
  amy_add_event(&e);
  delay(50); // Let the reset complete

  setupHiHatPatch();

  // 4. Set the AMY sequencer tempo to 120 BPM
  e = amy_default_event();
  e.tempo = 120.0f;
  amy_add_event(&e);
  Serial.println("Sequencer tempo configured to 120 BPM.");
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

  for (int i = 0; i < NUM_VOICES; i++) {
    const int reading = digitalRead(BUTTON_PINS[i]);

    if (reading == stableState[i]) {
      pendingState[i] = reading;
    } else if (reading != pendingState[i]) {
      pendingState[i] = reading;
      pendingTime[i] = now;
    } else if ((now - pendingTime[i]) >= DEBOUNCE_MS) {
      stableState[i] = reading;

      if (reading == LOW) {
        // Toggle the step state
        stepActive[i] = !stepActive[i];
        Serial.printf("Button %d (GPIO %d) pressed  → Step %d toggled %s\n",
                      i + 1, BUTTON_PINS[i], i + 1,
                      stepActive[i] ? "ON" : "OFF");

        // Update the sequencer event in AMY
        amy_event e = amy_default_event();
        e.sequence[SEQUENCE_TAG] = i;
        if (stepActive[i]) {
          e.sequence[SEQUENCE_PERIOD] =
              192; // 8 steps * 24 ticks = 192 total period
          e.sequence[SEQUENCE_TICK] = i * 24; // Trigger tick offset
          e.osc = 0;                          // Trigger hi-hat on oscillator 0
          e.velocity = 1.0f;                  // Trigger velocity
        } else {
          // Setting period and tick to 0 removes the event from the sequencer
          e.sequence[SEQUENCE_PERIOD] = 0;
          e.sequence[SEQUENCE_TICK] = 0;
        }
        amy_add_event(&e);
      }
    }
  }

  // ── NeoPixel LED feedback ─────────────────────────────────────────────────
  // Flash violet on active hi-hat triggers, dim green on silent steps, and fade
  // out smoothly.
  static float ledR = 0.0f;
  static float ledG = 0.0f;
  static float ledB = 0.0f;

  if (stepTriggered) {
    stepTriggered = false;
    uint8_t step = triggeredStep;
    if (step < NUM_VOICES) {
      if (stepActive[step]) {
        // Vibrant violet for active hit
        ledR = 63.0f;
        ledG = 0.0f;
        ledB = 63.0f;
      } else {
        // Faint green for metronome tick
        ledR = 0.0f;
        ledG = 10.0f;
        ledB = 0.0f;
      }
    }
  } else {
    // Decay values to fade out LED
    ledR -= 1.0f;
    if (ledR < 0.0f)
      ledR = 0.0f;
    ledG -= 0.2f;
    if (ledG < 0.0f)
      ledG = 0.0f;
    ledB -= 1.0f;
    if (ledB < 0.0f)
      ledB = 0.0f;
  }

  rgbLedWrite(LED_PIN, (uint8_t)ledR, (uint8_t)ledG, (uint8_t)ledB);

  // Call AMY update function to run the sequencer and event queue processing
  amy_update();

  // Maintain ~100 Hz loop rate
  delay(10);
}