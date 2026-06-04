#include <Arduino.h>
#include <ESP_I2S.h>

// Mozzi headers for synthesis
#include <ADSR.h>
#include <Oscil.h>
#include <tables/sin2048_int8.h>

// On the ESP32-S3-DevKitC-1, the built-in NeoPixel is on GPIO 38.
#define LED_PIN 38

// Single Button GPIO Pin Definition
#define BTN_PIN 15

// Frequency for the note C4
const float NOTE_FREQ = 261.63f;

// Instantiate a Mozzi oscillator running at 100 Hz update rate for the idle LED
// pulse.
Oscil<SIN2048_NUM_CELLS, 100> ledOsc(SIN2048_DATA);

// Instantiate a Mozzi oscillator running at 44100 Hz update rate for audio
// output.
Oscil<SIN2048_NUM_CELLS, 44100> audioOsc(SIN2048_DATA);

// Instantiate an ADSR envelope. Control update rate = 100Hz, Audio
// interpolation rate = 44100Hz.
ADSR<100, 44100> envelope;

// I2S Class instance for the audio bus
I2SClass i2s;

// Dedicated Playback Pin Configuration (Pmod I2S2 DAC)
const int pin_mclk = 4;
const int pin_lrck = 5;
const int pin_sclk = 6;
const int pin_sdin = 7; // Digital synth audio stream out to the Pmod DAC

// Thread-safe communication flags
volatile float targetFreq = NOTE_FREQ;
volatile bool triggerNoteOn = false;
volatile bool triggerNoteOff = false;
volatile uint8_t currentEnvLevel = 0;

// Background FreeRTOS task to synthesize audio samples in real-time
void audioTask(void *parameter) {
  const int numSamples = 256;
  int16_t buffer[numSamples * 2]; // Stereo interleaved buffer (L, R, L, R...)
  int controlCounter = 0;

  while (true) {
    for (int i = 0; i < numSamples; i++) {
      // Execute thread-safe synth control triggers
      if (triggerNoteOn) {
        triggerNoteOn = false;
        audioOsc.setFreq(targetFreq);
        envelope.noteOn(true); // reset envelope to 0 on trigger
      }
      if (triggerNoteOff) {
        triggerNoteOff = false;
        envelope.noteOff();
      }

      // Update control rate elements (envelope update) at 100 Hz.
      // 44100 / 100 = 441 audio samples per control step.
      controlCounter++;
      if (controlCounter >= 441) {
        controlCounter = 0;
        envelope.update();
      }

      // Synthesize sine wave sample (-128 to 127) and multiply by envelope
      // value (0 to 255)
      int16_t oscSample = audioOsc.next();
      uint8_t envLevel = envelope.next();
      currentEnvLevel = envLevel; // share envelope level with LED visualizer

      // Scale by 150 to match original output level config, divided by 256 for
      // envelope range scaling
      int16_t sampleVal = (int16_t)(((int32_t)oscSample * envLevel * 150) >> 8);

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
  Serial.println("ESP32-S3 Mozzi ADSR Synth Starting...");

  // WS2812 does not require pinMode configuration when using rgbLedWrite
  rgbLedWrite(LED_PIN, 0, 0, 0); // Start turned off

  // Setup single button input with internal pull-up resistor
  pinMode(BTN_PIN, INPUT_PULLUP);
  Serial.printf("GPIO %d configured as INPUT_PULLUP\n", BTN_PIN);

  // Initialize the LED oscillator at 1 Hz frequency (1 cycle per second)
  ledOsc.setFreq(1);

  // Initialize the audio oscillator frequency
  audioOsc.setFreq(NOTE_FREQ);

  // Set envelope Attack, Decay, Sustain, and Release parameters
  // Attack: 40ms, Decay: 120ms, Sustain Level: 200 (out of 255), Release: 250ms
  envelope.setTimes(40, 120, 50000, 250);
  envelope.setADLevels(255, 200);

  // Setup I2S pins: bclk, ws, dout, din (-1), mclk
  i2s.setPins(pin_sclk, pin_lrck, pin_sdin, -1, pin_mclk);

  // Initialize the I2S bus at 44.1 kHz, 16-bit per sample, Stereo
  if (i2s.begin(I2S_MODE_STD, 44100, I2S_DATA_BIT_WIDTH_16BIT,
                I2S_SLOT_MODE_STEREO)) {
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

int lastPinState = -1;

void loop() {
  // Read button state (active-LOW)
  int currentPinState = digitalRead(BTN_PIN);

  if (currentPinState != lastPinState) {
    if (currentPinState == LOW) {
      Serial.println("Button pressed: Triggering note C4!");
      triggerNoteOn = true;
    } else {
      Serial.println("Button released: Releasing note!");
      triggerNoteOff = true;
    }
    lastPinState = currentPinState;
  }

  // Visual LED Feedback:
  // - Faint pulsing green when idle.
  // - Vibrant magenta/violet proportional to the envelope level when a note is
  // active.
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