#pragma once
#include <stdint.h>

// ── Audio & Control Rates ─────────────────────────────────────────────────────
static constexpr int SYNTH_AUDIO_RATE   = 44100; // Hz — I2S sample rate
static constexpr int SYNTH_CONTROL_RATE = 100;   // Hz — Mozzi control/envelope update rate
// Number of audio samples between each control-rate update (441 @ 44.1 kHz / 100 Hz)
static constexpr int CTRL_DIVIDER = SYNTH_AUDIO_RATE / SYNTH_CONTROL_RATE;

// ── Audio Buffer ──────────────────────────────────────────────────────────────
static constexpr int AUDIO_BUFFER_SAMPLES = 256; // Frames per DMA write

// ── Hardware: NeoPixel LED ────────────────────────────────────────────────────
// Built-in WS2812 on the ESP32-S3-DevKitC-1
static constexpr int LED_PIN = 38;

// ── Hardware: Pmod I2S2 DAC Pins ─────────────────────────────────────────────
static constexpr int PIN_MCLK = 4;
static constexpr int PIN_LRCK = 5;
static constexpr int PIN_SCLK = 6;
static constexpr int PIN_SDIN = 7; // Audio stream out to DAC

// ── Voice & Button Mapping ────────────────────────────────────────────────────
static constexpr int NUM_VOICES = 8;

// Button GPIO pins (left to right on protoboard)
static constexpr int BUTTON_PINS[NUM_VOICES] = {15, 16, 17, 18, 8, 3, 46, 9};

// Double Harmonic Major scale in C (one octave: C4 → C5)
static constexpr float NOTE_FREQS[NUM_VOICES] = {
    261.63f, // C4
    277.18f, // Db4
    329.63f, // E4
    349.23f, // F4
    392.00f, // G4
    415.30f, // Ab4
    493.88f, // B4
    523.25f, // C5
};

// ── Envelope (ADSR) Parameters ────────────────────────────────────────────────
static constexpr unsigned int ENV_ATTACK_MS  = 40;
static constexpr unsigned int ENV_DECAY_MS   = 120;
static constexpr unsigned int ENV_SUSTAIN_MS = 50000;
static constexpr unsigned int ENV_RELEASE_MS = 250;
static constexpr uint8_t      ENV_ATTACK_LEVEL = 255;
static constexpr uint8_t      ENV_DECAY_LEVEL  = 200;

// ── Synthesis Mix ─────────────────────────────────────────────────────────────
// Post-mix gain applied before the 8-bit envelope normalization (>> ENV_SHIFT).
// A value of 30 prevents clipping when all 8 voices play simultaneously.
static constexpr int MIX_GAIN  = 30;
static constexpr int ENV_SHIFT = 8; // Right-shift amount to normalize 8-bit envelope (÷256)

// ── Button Debounce ───────────────────────────────────────────────────────────
// Minimum duration (ms) a pin level must be stable before it is committed.
static constexpr unsigned long DEBOUNCE_MS = 10;
