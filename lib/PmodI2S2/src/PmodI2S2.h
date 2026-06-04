#ifndef PMOD_I2S2_H
#define PMOD_I2S2_H

#include <Arduino.h>
#include <ESP_I2S.h>

/**
 * @class PmodI2S2
 * @brief Driver class for the Digilent Pmod I2S2 stereo DAC and ADC module.
 *
 * This class wraps the ESP32 Arduino ESP_I2S library to provide clean,
 * reusable initialization, and stereo reading/writing operations.
 */
class PmodI2S2 {
public:
  PmodI2S2();
  ~PmodI2S2();

  /**
   * @brief Initialize the I2S peripheral for the Digilent Pmod I2S2.
   *
   * @param sclk Serial clock (BCLK) pin.
   * @param lrck Left/Right clock (WS / Word Select) pin.
   * @param dout Serial Data Out from ESP32 to DAC SDIN pin. Set to -1 if
   * unused.
   * @param din Serial Data In to ESP32 from ADC SDOUT pin. Set to -1 if unused.
   * @param mclk Master clock pin. Set to -1 if unused.
   * @param rate Sample rate in Hz (default: 44100).
   * @param bits Per-sample bit width (default: 16-bit).
   * @return true on success, false on failure.
   */
  bool begin(int sclk, int lrck, int dout, int din = -1, int mclk = -1,
             uint32_t rate = 44100,
             i2s_data_bit_width_t bits = I2S_DATA_BIT_WIDTH_16BIT);

  /**
   * @brief Stop the I2S bus.
   */
  void end();

  /**
   * @brief Write stereo/mono audio samples to the DAC.
   * Blocks if the I2S DMA queue is full.
   *
   * @param buffer Pointer to the sample buffer.
   * @param size Size of the buffer in bytes.
   * @return Number of bytes written.
   */
  size_t write(const uint8_t *buffer, size_t size);

  /**
   * @brief Write a single stereo frame (left + right channel) to the DAC.
   * Blocks if the I2S DMA queue is full.
   *
   * @param left Left channel sample.
   * @param right Right channel sample.
   * @return true if successfully written, false otherwise.
   */
  bool writeSample(int16_t left, int16_t right);

  /**
   * @brief Read raw stereo audio samples from the ADC.
   * Blocks if no data is available in the DMA queue.
   *
   * @param buffer Pointer to the destination buffer.
   * @param size Size to read in bytes.
   * @return Number of bytes read.
   */
  size_t read(uint8_t *buffer, size_t size);

  /**
   * @brief Read a single stereo frame (left + right channel) from the ADC.
   * Blocks if no data is available in the DMA queue.
   *
   * @param left Reference to store the left channel sample.
   * @param right Reference to store the right channel sample.
   * @return true if successfully read, false otherwise.
   */
  bool readSample(int16_t &left, int16_t &right);

  /**
   * @brief Access the underlying I2SClass instance directly if needed.
   *
   * @return Reference to the I2SClass instance.
   */
  I2SClass &getI2S() { return _i2s; }

private:
  I2SClass _i2s;
  bool _initialized;
};

#endif // PMOD_I2S2_H
