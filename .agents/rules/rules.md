---
trigger: always_on
---

Using an Espressif ESP32-S3-DEVKITC-1-N8R8

Using a Digilent Pmod I2S2 as DAC and ADC. Use the Custom DAC Driver Setup on `PmodI2S2.h`

Use `/home/haruo/.platformio/penv/bin/platformio` instead of `platformio` command on terminal.


## Mozzi docs

Use the Mozzi docs as reference in https://sensorium.github.io/Mozzi/doc/html/


## AMY library docs

Use the AMY docs as reference in https://github.com/shorepine/amy/tree/main/docs


## Core Synthesis & Hardware Configuration Constraints

- **I2S Slot Configuration**: The Digilent Pmod I2S2 (CS4344 DAC) requires 32-bit slot width configuration on ESP32-S3 to successfully lock/auto-detect clock rates. Always initialize the DAC with `I2S_DATA_BIT_WIDTH_32BIT`.
- **Audio Output Scaling**: AMY generates 16-bit signed audio samples. Scale them to 32-bit by left-shifting 16 bits (`((int32_t)sample) << 16`) before writing them to the 32-bit DAC buffer.
- **Base Amplitude Coefficient (`COEF_CONST`)**: In AMY, amplitude controls (like constant gain, velocity, and envelopes) are combined logarithmically. Setting a custom patch's `e.amp_coefs[COEF_CONST]` to `0.0f` results in a total mute. It **must** be set to `1.0f` (representing standard reference level / 0dB log) so velocity and envelopes can scale it down.
- **Sequencer Tag Limits**: `max_sequencer_tags` must be set to at least `32` to accommodate tag offsets of multiple voices (e.g. Kick at 8, Snare at 16, Hi-Hat at 0). Reducing this limit will cause step triggers to be rejected.
- **Render Threading**: Render audio frames using a dedicated background task on Core 1 by calling `amy_simple_fill_buffer()`. Keep this loop lock-free and avoid introducing mutexes around it.