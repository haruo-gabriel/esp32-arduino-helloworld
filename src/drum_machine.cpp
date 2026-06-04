#include "drum_machine.h"
#include <Arduino.h>
#include <AMY-Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
// Drum Machine Private State
// ─────────────────────────────────────────────────────────────────────────────
static DrumVoice voices[] = {
    { "kick", 1, 8, KICK_GAIN, {true, false, false, false, true, false, false, false} },
    { "snare", 2, 16, SNARE_GAIN, {false, false, true, false, false, false, true, false} },
    { "hihat", 0, 0, HIHAT_GAIN, {true, false, true, false, true, false, true, false} }
};
static const uint8_t NUM_DRUM_VOICES = sizeof(voices) / sizeof(voices[0]);

static StateChangeCb stateChangeCallback = nullptr;
static PlayheadTickCb playheadTickCallback = nullptr;

static volatile bool stepTriggered = false;
static volatile uint8_t triggeredStep = 0;
static float currentBPM = DEFAULT_BPM;
static uint8_t selectedVoice = 0;

static float ledR = 0.0f;
static float ledG = 0.0f;
static float ledB = 0.0f;

static void flashSelectedVoiceLED() {
  if (selectedVoice == 0) {
    // High red/orange flash for Kick selection
    ledR = 150.0f;
    ledG = 0.0f;
    ledB = 0.0f;
  } else if (selectedVoice == 1) {
    // High green flash for Snare selection
    ledR = 0.0f;
    ledG = 150.0f;
    ledB = 0.0f;
  } else {
    // High blue/purple flash for Hi-Hat selection
    ledR = 20.0f;
    ledG = 0.0f;
    ledB = 150.0f;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// AMY patch recipes
// ─────────────────────────────────────────────────────────────────────────────

static void setupHiHatPatch() {
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

static void setupKickPatch() {
  amy_event e = amy_default_event();
  e.osc = 1;
  e.wave = SINE;

  // EG0: Amplitude Envelope — exponential decay (180ms)
  e.amp_coefs[COEF_CONST] = 0.0f; // Base amplitude is 0
  e.amp_coefs[COEF_EG0] = 1.0f;   // Amplitude modulated by Envelope 0

  e.eg0_times[0] = 0;
  e.eg0_values[0] = 1.0f; // Instant attack (1.0 level)
  e.eg0_times[1] = 180;
  e.eg0_values[1] = 0.0f; // Decay to 0.0 in 180ms
  e.eg0_times[2] = 0;
  e.eg0_values[2] = 0.0f; // End breakpoint
  e.bp_is_set[0] = 1;     // Enable EG0

  // EG1: Pitch Envelope — rapid exponential pitch sweep (40ms)
  e.freq_coefs[COEF_CONST] = 45.0f; // Base frequency 45 Hz
  e.freq_coefs[COEF_EG1] = 2.0f;    // Pitch sweep depth: +3.5 octaves

  e.eg1_times[0] = 0;
  e.eg1_values[0] = 1.8f; // Start at max pitch sweep
  e.eg1_times[1] = 40;
  e.eg1_values[1] = 0.0f; // Rapid pitch decay in 40ms
  e.eg1_times[2] = 0;
  e.eg1_values[2] = 0.0f; // End breakpoint
  e.bp_is_set[1] = 1;     // Enable EG1

  amy_add_event(&e);
  Serial.println("Synthesized kick patch configured on oscillator 1.");

}

static void setupSnarePatch() {
  amy_event e = amy_default_event();
  e.osc = 2;
  e.wave = NOISE;
  e.amp_coefs[COEF_CONST] = 0.0f; // Base amplitude is 0
  e.amp_coefs[COEF_EG0] = 1.0f;   // Amplitude modulated by Envelope 0

  // EG0: Exponential decay for a snare sound (250ms decay)
  e.eg0_times[0] = 0;
  e.eg0_values[0] = 1.0f; // Instant attack (1.0 level)
  e.eg0_times[1] = 250;
  e.eg0_values[1] = 0.0f; // Decay to 0.0 in 250ms
  e.eg0_times[2] = 0;
  e.eg0_values[2] = 0.0f; // End breakpoint
  e.bp_is_set[0] = 1;     // Enable EG0

  amy_add_event(&e);
  Serial.println("Synthesized snare patch configured on oscillator 2.");

}

// ─────────────────────────────────────────────────────────────────────────────
// Internal AMY step helper
// ─────────────────────────────────────────────────────────────────────────────
static void updateAmyStepInternal(uint8_t voiceIdx, uint8_t step, bool active) {
  if (voiceIdx >= NUM_DRUM_VOICES) return;
  const auto& voice = voices[voiceIdx];
  amy_event e = amy_default_event();
  e.sequence[SEQUENCE_TAG] = step + voice.tagOffset;
  if (active) {
    e.sequence[SEQUENCE_PERIOD] = 192;
    e.sequence[SEQUENCE_TICK] = step * 24;
    e.osc = voice.amyOsc;
    e.velocity = voice.gain;
  } else {
    e.sequence[SEQUENCE_PERIOD] = 0;
    e.sequence[SEQUENCE_TICK] = 0;
  }
  amy_add_event(&e);
}


// ─────────────────────────────────────────────────────────────────────────────
// AMY sequencer hook callback (called from hardware timer ISR context)
// ─────────────────────────────────────────────────────────────────────────────
void my_sequencer_hook(uint32_t tick_count) {
  static uint32_t lastPrint = 0;
  if (tick_count - lastPrint >= 48) { // print once per second (approx)
    lastPrint = tick_count;
    Serial.printf("my_sequencer_hook called with tick_count: %u\n", tick_count);
  }
  // Each step represents an 8th note.
  // At 48 PPQ (ticks per quarter note), an 8th note is 24 ticks.
  // The total period for 8 steps is 8 * 24 = 192 ticks.
  if (tick_count % 24 == 0) {
    triggeredStep = (tick_count % 192) / 24;
    stepTriggered = true;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API Implementation
// ─────────────────────────────────────────────────────────────────────────────

void drumMachineInit(StateChangeCb onState, PlayheadTickCb onTick) {
  stateChangeCallback = onState;
  playheadTickCallback = onTick;

  // Configure all step button pins with internal pull-up resistors
  for (int i = 0; i < NUM_STEPS; i++) {
    pinMode(BUTTON_PINS[i], INPUT_PULLUP);
    Serial.printf("GPIO %d configured as INPUT_PULLUP (Step %d Toggle)\n",
                  BUTTON_PINS[i], i + 1);
  }

  // Configure switcher button pin with internal pull-up resistor
  pinMode(SWITCHER_BUTTON_PIN, INPUT_PULLUP);
  Serial.printf("GPIO %d configured as INPUT_PULLUP (Voice Switcher)\n",
                SWITCHER_BUTTON_PIN);

  // Reset the engine and initialize patch configurations
  amy_event e = amy_default_event();
  e.reset_osc = RESET_AMY;
  amy_add_event(&e);
  delay(50); // Let the reset complete

  setupHiHatPatch();
  setupKickPatch();
  setupSnarePatch();

  // Set the AMY sequencer tempo using the defined BPM
  e = amy_default_event();
  e.tempo = currentBPM;
  amy_add_event(&e);
  Serial.printf("Sequencer tempo configured to %.1f BPM.\n", currentBPM);

  // Schedule any initially active steps in the AMY sequencer
  for (uint8_t v = 0; v < NUM_DRUM_VOICES; v++) {
    for (uint8_t s = 0; s < NUM_STEPS; s++) {
      if (voices[v].steps[s]) {
        updateAmyStepInternal(v, s, true);
      }
    }
  }
}

void drumMachineUpdate() {
  // ── Button state tracking with hardware debouncing ────────────────────────
  static int stableState[NUM_STEPS];
  static int pendingState[NUM_STEPS];
  static unsigned long pendingTime[NUM_STEPS];

  static int switcherStableState = HIGH;
  static int switcherPendingState = HIGH;
  static unsigned long switcherPendingTime = 0;

  static bool debounceInit = false;

  if (!debounceInit) {
    debounceInit = true;
    for (int i = 0; i < NUM_STEPS; i++) {
      stableState[i] = HIGH;
      pendingState[i] = HIGH;
      pendingTime[i] = 0;
    }
  }

  const unsigned long now = millis();

  // ── Switcher button logic ──────────────────────────────────────────────────
  const int switcherReading = digitalRead(SWITCHER_BUTTON_PIN);

  if (switcherReading == switcherStableState) {
    switcherPendingState = switcherReading;
  } else if (switcherReading != switcherPendingState) {
    switcherPendingState = switcherReading;
    switcherPendingTime = now;
  } else if ((now - switcherPendingTime) >= DEBOUNCE_MS) {
    switcherStableState = switcherReading;

    if (switcherReading == LOW) {
      // Switch the active voice sequencer
      selectedVoice = (selectedVoice + 1) % NUM_DRUM_VOICES;
      Serial.printf("Switcher (GPIO %d) pressed → Selected Sequencer: %s\n",
                    SWITCHER_BUTTON_PIN,
                    voices[selectedVoice].name);

      // Flash LED to confirm selection
      flashSelectedVoiceLED();

      if (stateChangeCallback) {
        stateChangeCallback();
      }
    }
  }

  // ── 8 Step buttons logic ───────────────────────────────────────────────────
  for (int i = 0; i < NUM_STEPS; i++) {
    const int reading = digitalRead(BUTTON_PINS[i]);

    if (reading == stableState[i]) {
      pendingState[i] = reading;
    } else if (reading != pendingState[i]) {
      pendingState[i] = reading;
      pendingTime[i] = now;
    } else if ((now - pendingTime[i]) >= DEBOUNCE_MS) {
      stableState[i] = reading;

      if (reading == LOW) {
        drumMachineToggleStep(selectedVoice, i);
        if (stateChangeCallback) {
          stateChangeCallback();
        }
      }
    }
  }

  // ── NeoPixel LED feedback ─────────────────────────────────────────────────
  if (stepTriggered) {
    stepTriggered = false;
    uint8_t step = triggeredStep;

    // Send playhead position to web clients
    if (playheadTickCallback) {
      playheadTickCallback(step);
    }

    if (step < NUM_STEPS) {
      const bool kick = voices[0].steps[step];
      const bool snare = voices[1].steps[step];
      const bool hh = voices[2].steps[step];
      if (kick && snare && hh) {
        ledR = 80.0f;
        ledG = 80.0f;
        ledB = 80.0f;
      } else if (kick && snare) {
        ledR = 80.0f;
        ledG = 0.0f;
        ledB = 40.0f;
      } else if (kick && hh) {
        ledR = 80.0f;
        ledG = 30.0f;
        ledB = 80.0f;
      } else if (snare && hh) {
        ledR = 0.0f;
        ledG = 40.0f;
        ledB = 80.0f;
      } else if (kick) {
        ledR = 80.0f;
        ledG = 15.0f;
        ledB = 0.0f;
      } else if (snare) {
        ledR = 0.0f;
        ledG = 80.0f;
        ledB = 80.0f;
      } else if (hh) {
        ledR = 40.0f;
        ledG = 0.0f;
        ledB = 80.0f;
      } else {
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
}

void drumMachineSetStep(uint8_t voice, uint8_t step, bool active) {
  if (voice < NUM_DRUM_VOICES && step < NUM_STEPS) {
    voices[voice].steps[step] = active;
    updateAmyStepInternal(voice, step, active);
  }
}

void drumMachineToggleStep(uint8_t voice, uint8_t step) {
  if (voice < NUM_DRUM_VOICES && step < NUM_STEPS) {
    voices[voice].steps[step] = !voices[voice].steps[step];
    updateAmyStepInternal(voice, step, voices[voice].steps[step]);
  }
}

void drumMachineSetBPM(float bpm) {
  if (bpm >= 60.0f && bpm <= 600.0f) {
    currentBPM = bpm;
    amy_event e = amy_default_event();
    e.tempo = currentBPM;
    amy_add_event(&e);
  }

}

uint8_t drumMachineGetNumVoices() {
  return NUM_DRUM_VOICES;
}

const char* drumMachineGetVoiceName(uint8_t voice) {
  if (voice < NUM_DRUM_VOICES) {
    return voices[voice].name;
  }
  return nullptr;
}

const bool* drumMachineGetSteps(uint8_t voice) {
  if (voice < NUM_DRUM_VOICES) {
    return voices[voice].steps;
  }
  return nullptr;
}

float drumMachineGetBPM() {
  return currentBPM;
}

uint8_t drumMachineGetStep() {
  return triggeredStep;
}

uint8_t drumMachineGetSelectedVoice() {
  return selectedVoice;
}

void drumMachineSetSelectedVoice(uint8_t voice) {
  if (voice < NUM_DRUM_VOICES) {
    selectedVoice = voice;
    // Flash LED to confirm selection
    flashSelectedVoiceLED();
    if (stateChangeCallback) {
      stateChangeCallback();
    }
  }
}
