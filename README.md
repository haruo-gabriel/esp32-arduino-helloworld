# ESP32-S3 AMY Drum Sequencer

An 8-step hardware drum sequencer running on the **ESP32-S3-DevKitC-1** with the **AMY synthesizer engine** and a **Digilent Pmod I2S2** stereo DAC. It exposes a web UI over Wi-Fi for remote control and pattern editing.

---

## Features

- 3-voice, 8-step drum sequencer (Kick, Snare, Hi-Hat) powered by [AMY](https://github.com/shorepine/amy)
- Stereo I2S audio output via the Digilent Pmod I2S2 (CS4344 DAC chip)
- 8 physical step-toggle buttons + 1 voice-switcher button
- NeoPixel RGB LED feedback (per-step color reflects active voices)
- Wi-Fi Access Point + WebSocket-driven web UI (served from LittleFS)
- BPM control (60–600 BPM), adjustable from both hardware and web UI
- PSRAM used for AMY allocations to preserve internal SRAM for Wi-Fi/WebServer

---

## Hardware Requirements

| Component | Notes |
|---|---|
| [ESP32-S3-DevKitC-1-N8R8](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/hw-reference/esp32s3/user-guide-devkitc-1.html) | 8 MB Flash, 8 MB PSRAM (OPI) |
| [Digilent Pmod I2S2](https://digilent.com/reference/pmod/pmodi2s2/start) | CS4344 stereo DAC + CS5343 ADC |
| 8 × push buttons | Momentary, wired to GND (uses internal pull-up) |
| 1 × push button | Voice switcher, same wiring |

### GPIO Pin Mapping

| Signal | GPIO | Notes |
|---|---|---|
| I2S MCLK | 4 | Master clock |
| I2S LRCK | 5 | Left/Right clock (WS) |
| I2S SCLK | 6 | Bit clock (BCLK) |
| I2S SDIN | 7 | Serial data out to Pmod DAC |
| NeoPixel LED | 38 | Built-in RGB LED on DevKitC-1 |
| Step buttons | 15, 16, 17, 18, 8, 3, 46, 9 | Steps 1–8 (left to right) |
| Voice switcher | 10 | Cycles Kick → Snare → Hi-Hat |

> All button pins use `INPUT_PULLUP`. Connect one side of the button to the GPIO, the other to GND.

---

## Software Requirements

### Tools

- [PlatformIO](https://platformio.org/) (CLI or VS Code extension)
- Python 3.x (required by PlatformIO)

Install PlatformIO CLI:

```bash
pip install platformio
```

Or use the [VS Code PlatformIO IDE extension](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide).

### PlatformIO Dependencies

All dependencies are declared in [`platformio.ini`](platformio.ini) and installed automatically on first build:

| Library | Version |
|---|---|
| [AMY Synthesizer](https://github.com/shorepine/amy) | `1.2.5` |
| [ESPAsyncWebServer](https://github.com/ESP32Async/ESPAsyncWebServer) | `^3.9.2` |
| [AsyncTCP](https://github.com/ESP32Async/AsyncTCP) | latest |

The ESP32 platform is pulled from the [pioarduino fork](https://github.com/pioarduino/platform-espressif32), which tracks the latest Espressif Arduino Core:

```ini
platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip
```

---

## Project Structure

```
esp32-arduino-helloworld/
├── data/
│   └── index.html          # Web UI (uploaded to LittleFS)
├── include/
│   ├── drum_machine.h      # Drum machine public API
│   ├── synth_config.h      # Pin definitions, BPM, gain constants
│   └── web_server.h        # Wi-Fi & WebSocket server API
├── lib/
│   └── PmodI2S2/           # Custom I2S2 DAC driver (local library)
│       └── src/
│           ├── PmodI2S2.h
│           └── PmodI2S2.cpp
├── src/
│   ├── main.cpp            # Entry point: AMY init, audio task, loop
│   ├── drum_machine.cpp    # Sequencer logic, patches, button handling
│   └── web_server.cpp      # HTTP + WebSocket server, LittleFS serving
├── partitions.csv          # Custom partition table (2 MB app + ~2 MB LittleFS)
└── platformio.ini          # Build configuration
```

---

## Building & Flashing

### 1. Clone the repository

```bash
git clone <your-repo-url>
cd esp32-arduino-helloworld
```

### 2. Build the firmware

```bash
platformio run
```

### 3. Upload the filesystem (web UI)

The web UI (`data/index.html`) must be uploaded to the board's LittleFS partition **before** or **after** flashing firmware:

```bash
platformio run --target uploadfs
```

### 4. Flash the firmware

```bash
platformio run --target upload
```

### 5. Monitor serial output

```bash
platformio device monitor --baud 115200
```

> On this project, the `platformio` binary may live at:  
> `/home/<user>/.platformio/penv/bin/platformio`  
> Substitute the full path if `platformio` isn't on your `PATH`.

---

## Using the Web UI

1. Power on the board.
2. On your phone or laptop, connect to the Wi-Fi network **`ESP32-Sequencer`** (no password).
3. Open a browser and navigate to **`http://192.168.4.1/`**.
4. The UI shows the 8-step grid for all three voices, a BPM slider, and a playhead indicator that updates in real time over WebSocket.

### WebSocket Protocol (port 80, path `/ws`)

| Direction | Message (JSON) | Description |
|---|---|---|
| Client → Board | `{"type":"toggle","voice":0,"step":3}` | Toggle a step on/off |
| Client → Board | `{"type":"bpm","value":130}` | Set BPM (60–600) |
| Client → Board | `{"type":"select_voice","voice":1}` | Select active voice |
| Client → Board | `{"type":"get_state"}` | Request full state |
| Board → Client | `{"type":"state", ...}` | Full sequencer state broadcast |
| Board → Client | `{"type":"step","index":4}` | Playhead tick |

---

## Architecture Notes

### Audio Pipeline

```
AMY engine (Core 1, background task)
    ↓  amy_simple_fill_buffer()  →  int16_t[AMY_BLOCK_SIZE × 2]
    ↓  left-shift 16 bits        →  int32_t[AMY_BLOCK_SIZE × 2]
    ↓  PmodI2S2.write()          →  I2S DMA  →  CS4344 DAC  →  Audio Out
```

The I2S bus is configured for **32-bit slot width** (`I2S_DATA_BIT_WIDTH_32BIT`). This is required for the CS4344 to lock onto the clock correctly. AMY's native 16-bit samples are left-shifted into the MSB of each 32-bit word.

### AMY Configuration Highlights

- `audio = AMY_AUDIO_IS_NONE` — AMY's internal audio driver is disabled; we drive I2S manually.
- `multithread = 0`, `multicore = 0` — Rendering is done in our own `audioTask`.
- All heap-heavy AMY buffers (`events`, `synth`, `delay`, `sample`, `sysex`) are allocated from **PSRAM** (`MALLOC_CAP_SPIRAM`) to leave internal SRAM free for Wi-Fi and the web server.
- `max_sequencer_tags = 32` — Required to cover tag offsets: Hi-Hat @ 0, Kick @ 8, Snare @ 16.

### Sequencer Timing

The AMY sequencer runs at **48 PPQ** (pulses per quarter note). Each 8th-note step = 24 ticks. A full 8-step bar = 192 ticks. The `my_sequencer_hook` ISR callback fires on every tick and sets a flag every 24 ticks to advance the playhead in `loop()`.

### Drum Patches

All patches are synthesized in software (no samples):

| Voice | Wave | Osc | Technique |
|---|---|---|---|
| **Hi-Hat** | NOISE | 0 | BPF @ 11 kHz, resonance 6, 30 ms decay |
| **Kick** | SINE | 1 | 45 Hz base + pitch envelope sweep (40 ms), 180 ms amp decay |
| **Snare** | NOISE | 2 | Broad noise, 250 ms amp decay |

---

## Customization

| What | Where |
|---|---|
| GPIO pin assignments | [`include/synth_config.h`](include/synth_config.h) |
| Default BPM & per-voice gain | [`include/synth_config.h`](include/synth_config.h) |
| Default step patterns | [`src/drum_machine.cpp`](src/drum_machine.cpp) — `voices[]` array |
| AMY resource limits | [`src/main.cpp`](src/main.cpp) — `amy_config` struct |
| Web UI | [`data/index.html`](data/index.html) |

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---|---|---|
| No audio / DAC not locking | Wrong bit width | Ensure `I2S_DATA_BIT_WIDTH_32BIT` is used |
| Silent output despite patches | `amp_coefs[COEF_CONST]` is 0 | Set it to `1.0f` in patch setup |
| Steps not triggering | `max_sequencer_tags` too low | Must be ≥ 32 |
| LittleFS mount failed | Filesystem not uploaded | Run `platformio run --target uploadfs` |
| Web UI not loading | Board rebooting / OOM | Check heap with serial monitor; reduce AMY limits |
| OOM / crash on boot | PSRAM not enabled | Confirm `BOARD_HAS_PSRAM` flag and `qio_opi` memory type in `platformio.ini` |
