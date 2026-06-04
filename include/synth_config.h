#pragma once
#include <stdint.h>

// ── Audio Rates
// ─────────────────────────────────────────────────────
static constexpr int SYNTH_AUDIO_RATE = 44100; // Hz — I2S sample rate

// ── Sequencer Configurations
// ──────────────────────────────────────────────────
#define DEFAULT_BPM 120.0f

// ── Voice Gain Parameters
// ──────────────────────────────────────────────────
#define KICK_GAIN 1.0f
#define SNARE_GAIN 0.5f
#define HIHAT_GAIN 1.0f

// ── Hardware: NeoPixel LED
// ──────────────────────────────────────────────────── Built-in WS2812 on the
// ESP32-S3-DevKitC-1
static constexpr int LED_PIN = 38;

// ── Hardware: Pmod I2S2 DAC Pins ─────────────────────────────────────────────
static constexpr int PIN_MCLK = 4;
static constexpr int PIN_LRCK = 5;
static constexpr int PIN_SCLK = 6;
static constexpr int PIN_SDIN = 7; // Audio stream out to DAC

// ── Voice & Button Mapping
// ────────────────────────────────────────────────────
static constexpr int NUM_STEPS = 8;

// Button GPIO pins (left to right on protoboard)
static constexpr int BUTTON_PINS[NUM_STEPS] = {15, 16, 17, 18, 8, 3, 46, 9};
static constexpr int SWITCHER_BUTTON_PIN = 10;


// ── Button Debounce
// ─────────────────────────────────────────────────────────── Minimum duration
// (ms) a pin level must be stable before it is committed.
static constexpr unsigned long DEBOUNCE_MS = 10;
