#include <Arduino.h>
#include <ESP_I2S.h>

// Mozzi headers for synthesis
#include <Oscil.h>
#include <tables/sin2048_int8.h>

// On the ESP32-S3-DevKitC-1, the built-in NeoPixel is on GPIO 38 or 48.
// The user's template code explicitly defines it as 38.
#define LED_PIN 38

// Instantiate a Mozzi oscillator running at 100 Hz update rate for the LED.
Oscil<SIN2048_NUM_CELLS, 100> ledOsc(SIN2048_DATA);

// Instantiate a Mozzi oscillator running at 44100 Hz update rate for audio output.
Oscil<SIN2048_NUM_CELLS, 44100> audioOsc(SIN2048_DATA);

// I2S Class instance for the audio bus
I2SClass i2s;

// Dedicated Playback Pin Configuration (Pmod I2S2 DAC)
const int pin_mclk = 4; 
const int pin_lrck = 5; 
const int pin_sclk = 6; 
const int pin_sdin = 7; // Digital synth audio stream out to the Pmod DAC

// Background FreeRTOS task to synthesize audio samples in real-time
void audioTask(void *parameter)
{
  const int numSamples = 256;
  int16_t buffer[numSamples * 2]; // Stereo interleaved buffer (L, R, L, R...)

  while (true) {
    for (int i = 0; i < numSamples; i++) {
      // Get the next audio sample from the 440Hz oscillator (ranges -128 to 127).
      // Scale it to 16-bit range (-32768 to 32767).
      // We multiply by 150 to get a healthy, clear signal level (127 * 150 = 19050).
      int16_t sampleVal = (int16_t)audioOsc.next() * 150;

      buffer[i * 2]     = sampleVal; // Left Channel
      buffer[i * 2 + 1] = sampleVal; // Right Channel
    }
    // Write buffer to I2S DMA queue. This blocks automatically when DMA is full,
    // ensuring the synthesis task runs at exactly the DAC's sample rate.
    i2s.write((const uint8_t *)buffer, sizeof(buffer));
  }
}

void setup()
{
  // WS2812 does not require pinMode configuration when using rgbLedWrite
  rgbLedWrite(LED_PIN, 0, 0, 0); // Start turned off

  // Initialize the LED oscillator at 1 Hz frequency (1 cycle per second)
  ledOsc.setFreq(1);

  // Initialize the audio oscillator at 440 Hz (A4 note)
  audioOsc.setFreq(440);

  // Setup I2S pins: bclk, ws, dout, din (-1), mclk
  i2s.setPins(pin_sclk, pin_lrck, pin_sdin, -1, pin_mclk);

  // Initialize the I2S bus at 44.1 kHz, 16-bit per sample, Stereo
  if (i2s.begin(I2S_MODE_STD, 44100, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    // Create the background audio synthesis task on Core 1
    xTaskCreatePinnedToCore(
      audioTask,      // Function implementing the task
      "audioTask",    // Name of the task
      4096,           // Stack size (words)
      NULL,           // Parameters
      10,             // Task priority (high priority to prevent buffer underflow/stutter)
      NULL,           // Task handle
      1               // Core 1
    );
  }
}

void loop()
{
  // Get the next value from the sine oscillator.
  // The table sin2048_int8 has signed 8-bit values from -128 to 127.
  int8_t oscVal = ledOsc.next();

  // Map the -128 to 127 range to a positive brightness range of 0 to 64.
  // We can do this by adding 128 (making it 0 to 255) and shifting right by 2 (making it 0 to 63).
  uint8_t brightness = (oscVal + 128) >> 2;

  // Set the LED to the calculated brightness (Red color)
  rgbLedWrite(LED_PIN, brightness, 0, 0);

  // Maintain the 100 Hz update rate (10 ms delay)
  delay(10);
}