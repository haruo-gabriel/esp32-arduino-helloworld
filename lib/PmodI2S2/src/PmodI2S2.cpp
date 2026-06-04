#include "PmodI2S2.h"

PmodI2S2::PmodI2S2() : _initialized(false) {}

PmodI2S2::~PmodI2S2() { end(); }

bool PmodI2S2::begin(int sclk, int lrck, int dout, int din, int mclk,
                     uint32_t rate, i2s_data_bit_width_t bits) {
  if (_initialized) {
    end();
  }

  _i2s.setPins(sclk, lrck, dout, din, mclk);

  // Pmod I2S2 operates in standard I2S (STD) mode.
  if (_i2s.begin(I2S_MODE_STD, rate, bits, I2S_SLOT_MODE_STEREO)) {
    _initialized = true;
    return true;
  }
  return false;
}

void PmodI2S2::end() {
  if (_initialized) {
    _i2s.end();
    _initialized = false;
  }
}

size_t PmodI2S2::write(const uint8_t *buffer, size_t size) {
  if (!_initialized)
    return 0;
  return _i2s.write(buffer, size);
}

bool PmodI2S2::writeSample(int16_t left, int16_t right) {
  int16_t frame[2] = {left, right};
  return write((const uint8_t *)frame, sizeof(frame)) == sizeof(frame);
}

size_t PmodI2S2::read(uint8_t *buffer, size_t size) {
  if (!_initialized)
    return 0;
  return _i2s.readBytes((char *)buffer, size);
}

bool PmodI2S2::readSample(int16_t &left, int16_t &right) {
  int16_t frame[2];
  if (read((uint8_t *)frame, sizeof(frame)) == sizeof(frame)) {
    left = frame[0];
    right = frame[1];
    return true;
  }
  return false;
}
