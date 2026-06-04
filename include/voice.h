#pragma once
#include <Arduino.h>
#include <ADSR.h>
#include <Oscil.h>
#include <tables/triangle2048_int8.h>
#include "synth_config.h"

// ─────────────────────────────────────────────────────────────────────────────
// Voice
//
// Encapsulates one synthesizer voice: a triangle-wave oscillator paired with
// an ADSR amplitude envelope. All template parameters are drawn from
// synth_config.h so they stay in sync with the I2S clock and control rate.
// ─────────────────────────────────────────────────────────────────────────────
struct Voice {
    Oscil<TRIANGLE2048_NUM_CELLS, SYNTH_AUDIO_RATE>   osc;
    ADSR<SYNTH_CONTROL_RATE, SYNTH_AUDIO_RATE>        env;

    // Assign the wave table, set the initial frequency, and configure the
    // ADSR shape. Call once per voice during setup().
    void init(float freq) {
        osc.setTable(TRIANGLE2048_DATA);
        osc.setFreq(freq);
        env.setTimes(ENV_ATTACK_MS, ENV_DECAY_MS, ENV_SUSTAIN_MS, ENV_RELEASE_MS);
        env.setADLevels(ENV_ATTACK_LEVEL, ENV_DECAY_LEVEL);
    }

    // Begin the attack phase. Resets the envelope to zero so re-triggering
    // a held note restarts cleanly instead of clicking.
    void noteOn(float freq) {
        osc.setFreq(freq);
        env.noteOn(/*reset=*/true);
    }

    // Begin the release phase.
    void noteOff() {
        env.noteOff();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// VoiceBank
//
// Owns the full array of voices plus the thread-safe trigger flags and the
// shared envelope-level value that drives the LED visualiser.
//
// The audio task (Core 1) calls processTriggersAndMix() each buffer cycle.
// The main loop (Core 0) calls triggerOn() / triggerOff() on button events.
// ─────────────────────────────────────────────────────────────────────────────
struct VoiceBank {
    Voice   voices[NUM_VOICES];

    // Written by Core 0, read (and cleared) by Core 1.
    volatile bool triggerOnFlags[NUM_VOICES];
    volatile bool triggerOffFlags[NUM_VOICES];

    // Written by Core 1, read by Core 0 for LED feedback.
    // Single-byte writes/reads on Xtensa are atomic, so no mutex needed.
    volatile uint8_t maxEnvLevel;

    VoiceBank() : maxEnvLevel(0) {
        for (int v = 0; v < NUM_VOICES; v++) {
            triggerOnFlags[v]  = false;
            triggerOffFlags[v] = false;
        }
    }

    // Initialise all voices. Call from setup() after I2S is ready.
    void init() {
        for (int v = 0; v < NUM_VOICES; v++) {
            voices[v].init(NOTE_FREQS[v]);
        }
    }

    // ── Core 0 API ───────────────────────────────────────────────────────────

    void triggerOn(int v)  { triggerOnFlags[v]  = true; }
    void triggerOff(int v) { triggerOffFlags[v] = true; }

    // ── Core 1 API ───────────────────────────────────────────────────────────

    // Consume any pending trigger flags. Call once per buffer fill, before the
    // sample loop, so trigger latency is bounded by one buffer (~5.8 ms at
    // 44.1 kHz / 256 frames) rather than potentially running per-sample.
    void processTriggers() {
        for (int v = 0; v < NUM_VOICES; v++) {
            if (triggerOnFlags[v]) {
                triggerOnFlags[v] = false;
                voices[v].noteOn(NOTE_FREQS[v]);
            }
            if (triggerOffFlags[v]) {
                triggerOffFlags[v] = false;
                voices[v].noteOff();
            }
        }
    }

    // Advance all envelope generators by one control step.
    // Must be called at CONTROL_RATE Hz.
    void updateEnvelopes() {
        for (int v = 0; v < NUM_VOICES; v++) {
            voices[v].env.update();
        }
    }

    // Compute one stereo-interleaved audio sample by summing all active voices.
    // Returns the scaled, clipped 16-bit sample and updates maxEnvLevel.
    int16_t nextSample() {
        int32_t summed = 0;
        uint8_t peakEnv = 0;

        for (int v = 0; v < NUM_VOICES; v++) {
            int16_t s = voices[v].osc.next();
            uint8_t e = voices[v].env.next();
            summed += (int32_t)s * e;
            if (e > peakEnv) peakEnv = e;
        }

        maxEnvLevel = peakEnv;

        // Apply post-mix gain then normalise the 8-bit envelope contribution.
        return (int16_t)((summed * MIX_GAIN) >> ENV_SHIFT);
    }
};
