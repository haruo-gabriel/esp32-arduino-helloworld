# esp32-sonzera

An 8-step hardware drum sequencer on the **ESP32-S3-DevKitC-1-N8R8**, using the [AMY synthesizer engine](https://github.com/shorepine/amy) for audio synthesis and a **Digilent Pmod I2S2** (CS4344) as the stereo DAC. A web UI served over Wi-Fi lets you edit patterns and BPM from any browser.

---

## Hardware

| Component | Detail |
|---|---|
| ESP32-S3-DevKitC-1-N8R8 | 8 MB Flash, 8 MB OPI PSRAM |
| Digilent Pmod I2S2 | CS4344 DAC — stereo audio out |
| 8 × momentary buttons | Step toggles (wired to GND, internal pull-up) |
| 1 × momentary button | Voice switcher |

### Pin Mapping

| Signal | GPIO |
|---|---|
| I2S MCLK | 4 |
| I2S LRCK (WS) | 5 |
| I2S SCLK (BCLK) | 6 |
| I2S SDIN → DAC | 7 |
| Built-in NeoPixel | 38 |
| Step buttons 1–8 | 15, 16, 17, 18, 8, 3, 46, 9 |
| Voice switcher | 10 |

---

## Setup

### Requirements

- [PlatformIO](https://platformio.org/) (CLI or VS Code extension)

### Build & Flash

```bash
# Clone
git clone https://github.com/haruo-gabriel/esp32-sonzera
cd esp32-sonzera

# Flash firmware
platformio run --target upload

# Upload web UI to LittleFS (do this once, or after editing data/index.html)
platformio run --target uploadfs

# Monitor serial output
platformio device monitor --baud 115200
```

> If `platformio` is not on your PATH, use the full path:
> `~/.platformio/penv/bin/platformio`

Dependencies are installed automatically on first build (declared in `platformio.ini`):
- `shorepine/AMY Synthesizer @ 1.2.5`
- `ESP32Async/ESPAsyncWebServer @ ^3.9.2`
- `ESP32Async/AsyncTCP`

---

## Usage

1. Power on the board.
2. Connect to the Wi-Fi AP **`ESP32-Sequencer`** (no password).
3. Open **`http://192.168.4.1`** in a browser.

The web UI shows the 8-step grid for Kick, Snare, and Hi-Hat, a BPM control, and a live playhead. All changes sync instantly over WebSocket.

Physical buttons toggle steps for the currently selected voice. The voice switcher cycles Kick → Snare → Hi-Hat; the built-in LED flashes a distinct color per voice.

---

## Project Structure

```
esp32-sonzera/
├── data/index.html        # Web UI (uploaded to LittleFS)
├── include/
│   ├── synth_config.h     # Pin definitions, BPM, gain constants
│   ├── drum_machine.h
│   └── web_server.h
├── lib/PmodI2S2/          # Custom I2S2 driver (local library)
├── src/
│   ├── main.cpp           # AMY init, audio task, main loop
│   ├── drum_machine.cpp   # Sequencer, patches, button handling
│   └── web_server.cpp     # HTTP + WebSocket server
└── platformio.ini
```

---

## Key Design Notes

- **I2S is 32-bit** — the CS4344 requires `I2S_DATA_BIT_WIDTH_32BIT` to lock the clock. AMY's 16-bit samples are left-shifted 16 bits before writing.
- **Audio runs on Core 1** — a dedicated FreeRTOS task calls `amy_simple_fill_buffer()` in a tight loop. No mutexes.
- **PSRAM for AMY** — all AMY heap buffers are allocated from PSRAM (`MALLOC_CAP_SPIRAM`), keeping internal SRAM free for Wi-Fi and the web server.
- **Drum patches are synthesized**, not sampled: Kick = SINE + pitch envelope, Snare = NOISE + amp envelope, Hi-Hat = NOISE + band-pass filter.
