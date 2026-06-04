#pragma once
#include <stdint.h>
#include "synth_config.h"

// Opaque voice descriptor — one per drum sound
struct DrumVoice {
    const char* name;      // "kick", "snare", "hihat"
    uint8_t     amyOsc;    // AMY oscillator index
    uint8_t     tagOffset; // First sequence tag (e.g. 0, 8, 16)
    float       gain;
    bool        steps[NUM_STEPS];
};

// Callback types
typedef void (*StateChangeCb)();
typedef void (*PlayheadTickCb)(uint8_t step);

// ── Public API ────────────────────────────────────────────────────────────────
void drumMachineInit(StateChangeCb onState, PlayheadTickCb onTick);
void drumMachineUpdate();         // Call from loop() — debounce + LED + AMY
void drumMachineSetStep(uint8_t voice, uint8_t step, bool active);
void drumMachineToggleStep(uint8_t voice, uint8_t step);
void drumMachineSetBPM(float bpm);

uint8_t      drumMachineGetNumVoices();
const char*  drumMachineGetVoiceName(uint8_t voice);
const bool*  drumMachineGetSteps(uint8_t voice);    // ptr to NUM_STEPS element array
float        drumMachineGetBPM();
uint8_t      drumMachineGetStep(); // Current playhead step

// AMY ISR Sequencer Hook function
void my_sequencer_hook(uint32_t tick_count);
