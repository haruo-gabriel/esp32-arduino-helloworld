#include <Arduino.h>
#include <ESP_I2S.h>

// Mozzi headers for synthesis
#include <ADSR.h>
#include <Oscil.h>
#include <tables/sin2048_int8.h>

// On the ESP32-S3-DevKitC-1, the built-in NeoPixel is on GPIO 38.
#define LED_PIN 38

// Button GPIO Pin Definitions (from left to right in my protoboard)
#define BTN_1 15
#define BTN_2 16
#define BTN_3 17
#define BTN_4 18
#define BTN_5 8 
#define BTN_6 3 
#define BTN_7 46 
#define BTN_8 9

#define NUM_VOICES 8

// Frequencies for notes (C4, D4, E4, F4, G4, A4, B4, C5)
#define NOTE_C4_FREQ 261.63f
#define NOTE_D4_FREQ 293.66f
#define NOTE_E4_FREQ 329.63f
#define NOTE_F4_FREQ 349.23f
#define NOTE_G4_FREQ 392.00f
#define NOTE_A4_FREQ 440.00f
#define NOTE_B4_FREQ 493.88f
#define NOTE_C5_FREQ 523.25f

// Map arrays for pins and frequencies
const int buttonPins[NUM_VOICES] = {BTN_1, BTN_2, BTN_3, BTN_4, BTN_5, BTN_6, BTN_7, BTN_8};
const float noteFreqs[NUM_VOICES] = {NOTE_C4_FREQ, NOTE_D4_FREQ, NOTE_E4_FREQ, NOTE_F4_FREQ, NOTE_G4_FREQ, NOTE_A4_FREQ, NOTE_B4_FREQ, NOTE_C5_FREQ};

// Struct to represent a synthesizer voice
struct Voice {
  Oscil<SIN2048_NUM_CELLS, 44100> osc;
  ADSR<100, 44100> env;
};

// Instantiate voices using the default constructor (table will be assigned in setup)
Voice voices[NUM_VOICES];

// Instantiate a Mozzi oscillator running at 100 Hz update rate for the idle LED pulse.
Oscil<SIN2048_NUM_CELLS, 100> ledOsc(SIN2048_DATA);

// I2S Class instance for the audio bus
I2SClass i2s;

// Dedicated Playback Pin Configuration (Pmod I2S2 DAC)
const int pin_mclk = 4;
const int pin_lrck = 5;
const int pin_sclk = 6;
const int pin_sdin = 7; // Digital synth audio stream out to the Pmod DAC

// Thread-safe communication flags
volatile bool voiceTriggerOn[NUM_VOICES] = {false};
volatile bool voiceTriggerOff[NUM_VOICES] = {false};
volatile uint8_t currentEnvLevel = 0;

// Background FreeRTOS task to synthesize audio samples in real-time
void audioTask(void *parameter) {
  const int numSamples = 256;
  int16_t buffer[numSamples * 2]; // Stereo interleaved buffer (L, R, L, R...)
  int controlCounter = 0;

  while (true) {
    for (int i = 0; i < numSamples; i++) {
      // Execute thread-safe synth control triggers
      for (int v = 0; v < NUM_VOICES; v++) {
        if (voiceTriggerOn[v]) {
          voiceTriggerOn[v] = false;
          voices[v].osc.setFreq(noteFreqs[v]);
          voices[v].env.noteOn(true); // reset envelope to 0 on trigger
        }
        if (voiceTriggerOff[v]) {
          voiceTriggerOff[v] = false;
          voices[v].env.noteOff();
        }
      }

      // Update control rate elements (envelope update) at 100 Hz.
      // 44100 / 100 = 441 audio samples per control step.
      controlCounter++;
      if (controlCounter >= 441) {
        controlCounter = 0;
        for (int v = 0; v < NUM_VOICES; v++) {
          voices[v].env.update();
        }
      }

      // Synthesize and sum the active voices
      int32_t summedSample = 0;
      uint8_t maxEnv = 0;
      
      for (int v = 0; v < NUM_VOICES; v++) {
        int16_t oscSample = voices[v].osc.next();
        uint8_t envLevel = voices[v].env.next();
        summedSample += (int32_t)oscSample * envLevel;
        if (envLevel > maxEnv) {
          maxEnv = envLevel;
        }
      }
      currentEnvLevel = maxEnv; // Share maximum envelope level with LED visualizer

      // Scale by 30 to prevent clipping when multiple keys are pressed, divided by 256 for envelope scaling
      int16_t sampleVal = (int16_t)((summedSample * 30) >> 8);

      buffer[i * 2] = sampleVal;     // Left Channel
      buffer[i * 2 + 1] = sampleVal; // Right Channel
    }
    // Write buffer to I2S DMA queue. This blocks automatically when DMA is
    // full, ensuring the synthesis task runs at exactly the DAC's sample rate.
    i2s.write((const uint8_t *)buffer, sizeof(buffer));
  }
}

void setup() {
  // Initialize Serial for hardware debugging
  Serial.begin(115200);
  delay(1000); // Give serial monitor time to connect
  Serial.println("ESP32-S3 Mozzi ADSR 8-Voice Polyphonic Synth Starting...");

  // WS2812 does not require pinMode configuration when using rgbLedWrite
  rgbLedWrite(LED_PIN, 0, 0, 0); // Start turned off

  // Setup button inputs with internal pull-up resistors
  for (int i = 0; i < NUM_VOICES; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
    Serial.printf("GPIO %d configured as INPUT_PULLUP\n", buttonPins[i]);
  }

  // Initialize the LED oscillator at 1 Hz frequency (1 cycle per second)
  ledOsc.setFreq(1);

  // Initialize wave table, frequencies, and envelope parameters for all voices
  for (int v = 0; v < NUM_VOICES; v++) {
    voices[v].osc.setTable(SIN2048_DATA); // Assign the sine table
    voices[v].osc.setFreq(noteFreqs[v]);
    voices[v].env.setTimes(40, 120, 50000, 250);
    voices[v].env.setADLevels(255, 200);
  }

  // Setup I2S pins: bclk, ws, dout, din (-1), mclk
  i2s.setPins(pin_sclk, pin_lrck, pin_sdin, -1, pin_mclk);

  // Initialize the I2S bus at 44.1 kHz, 16-bit per sample, Stereo
  if (i2s.begin(I2S_MODE_STD, 44100, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("I2S Bus successfully initialized!");
    // Create the background audio synthesis task on Core 1
    xTaskCreatePinnedToCore(audioTask,   // Function implementing the task
                            "audioTask", // Name of the task
                            4096,        // Stack size (words)
                            NULL,        // Parameters
                            10,          // Task priority
                            NULL,        // Task handle
                            1            // Core 1
    );
    Serial.println("Audio task pinned to Core 1.");
  } else {
    Serial.println("Failed to initialize I2S bus!");
  }
}

int lastButtonStates[NUM_VOICES] = {-1, -1, -1, -1, -1, -1, -1, -1};

void loop() {
  // Poll each button state
  for (int i = 0; i < NUM_VOICES; i++) {
    int currentPinState = digitalRead(buttonPins[i]);

    if (currentPinState != lastButtonStates[i]) {
      if (currentPinState == LOW) {
        Serial.printf("Button %d (GPIO %d) pressed: Triggering note freq %f Hz!\n", i + 1, buttonPins[i], noteFreqs[i]);
        voiceTriggerOn[i] = true;
      } else {
        Serial.printf("Button %d (GPIO %d) released: Releasing note!\n", i + 1, buttonPins[i]);
        voiceTriggerOff[i] = true;
      }
      lastButtonStates[i] = currentPinState;
    }
  }

  // Visual LED Feedback:
  // - Faint pulsing green when idle.
  // - Vibrant magenta/violet proportional to the envelope level when a note is active.
  uint8_t envVal = currentEnvLevel;
  if (envVal > 0) {
    uint8_t glowBrightness = envVal >> 2; // Scale 0-255 down to 0-63
    rgbLedWrite(LED_PIN, glowBrightness, 0, glowBrightness); // Vibrant Violet
  } else {
    int8_t oscVal = ledOsc.next();
    uint8_t pulseBrightness = (oscVal + 128) >> 4; // Faint pulse: 0 to 15
    rgbLedWrite(LED_PIN, 0, pulseBrightness, 0);   // Pulse Green
  }

  // Maintain the 100 Hz update rate (10 ms delay)
  delay(10);
}