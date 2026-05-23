# Bunbun — ESP32-S3 AI Clock

Tiny AI bunny that lives in your clock. Talks to you, sasses you, sleeps when
you tell it to from your phone.

## Hardware

| Part | Notes |
|------|-------|
| ESP32-S3-WROOM-1 | dev board with USB (Devkit-C-1 or similar). PSRAM required. |
| 2.42" OLED 128×64 | SSD1309 (or SH1106) over I²C |
| INMP441 | I²S MEMS microphone |
| MAX98357A | I²S DAC + class-D amp |
| Speaker | 4Ω or 8Ω small speaker, ~1W |

## Wiring (matches `config.h`)

```
OLED:        VCC→3V3, GND→GND, SDA→GPIO8,  SCL→GPIO9
INMP441:     VDD→3V3, GND→GND, L/R→GND, SCK→GPIO4, WS→GPIO5, SD→GPIO6
MAX98357A:   VIN→5V,  GND→GND, BCLK→GPIO15, LRC→GPIO16, DIN→GPIO7
             GAIN: leave floating for ~9dB, tie to GND for max
             SD: tie to VIN (or to a GPIO if you want to mute via code)
Speaker:     to MAX98357A speaker pads
```

The INMP441's L/R pin determines which I²S slot the mic uses. Tie it to GND
(left). If you tie it to VDD instead, change `I2S_CHANNEL_FMT_ONLY_LEFT` to
`I2S_CHANNEL_FMT_ONLY_RIGHT` in `audio.cpp`.

## First-time setup

1. **Install PlatformIO** in VS Code (search for "PlatformIO IDE" in the
   extensions panel). Arduino IDE will *not* work for this project because
   Porcupine's static library needs PlatformIO's build system.

2. **Get free API keys:**
   - **Groq** — go to <https://console.groq.com>, sign up, copy your key
     (free tier: 2000 Whisper requests/day, plenty of Llama and TTS)
   - **Picovoice** — go to <https://console.picovoice.ai>, sign up, copy your
     AccessKey (free for personal use)

3. **Train your "Hey Bunny" wake word** at the Picovoice Console:
   - Console → Porcupine → "Create Wake Word"
   - phrase: "Hey Bunny"
   - platform: **ESP32-S3**
   - download the `.ppn` file
   - convert it to a C header: `xxd -i hey_bunny_esp32.ppn > hey_bunny_ppn.h`
   - drop `hey_bunny_ppn.h` into `lib/porcupine/`

4. **Get the Porcupine ESP32-S3 SDK** from the Picovoice GitHub
   (<https://github.com/Picovoice/porcupine>), place the static library
   (`libpv_porcupine.a`) and headers (`pv_porcupine.h`, etc.) into
   `lib/porcupine/`.

5. **Edit `include/config.h`:**
   - `WIFI_SSID`, `WIFI_PASSWORD` — your network (or phone hotspot)
   - `GROQ_API_KEY` — from step 2
   - `PICOVOICE_ACCESS_KEY` — from step 2
   - `BUNNY_PERSONALITY` — write the bunny's voice in a few sentences
   - `TZ_OFFSET_SEC` — your timezone (default is UTC+5 / Almaty)

6. **Flip `USE_PORCUPINE` to 1** in `src/wake_word.cpp` once steps 3 and 4
   are done. Until then the project runs with a fallback: shout or clap loudly
   to "wake" it (good for testing everything else).

7. **Build and upload:** in PlatformIO sidebar, click the checkmark to build,
   then the arrow to upload. Open the serial monitor at 115200 baud.

## Using it

- Once it boots, it'll print the IP address. Open `http://<that-ip>/` on your
  phone (same WiFi). You'll see a pink toggle. Tap to turn AI on/off.
- Say "hey bunny" (or shout in fallback mode), then ask your question.
- It'll record until you stop talking (or ~7s max), transcribe via Whisper,
  reply via Llama with your personality prompt, and speak it via PlayAI.
- The OLED shows time and the bunny throughout. Clock stays on always.

## Tuning

- **mic too quiet** → in `audio.cpp`, decrease the `raw[i] >> 14` shift to
  `>> 12` (louder) or `>> 16` (quieter)
- **records too long / never stops** → lower `2000` energy threshold in
  `audio_record_to_wav`
- **replies too long for tiny speaker** → lower `max_tokens` in `groq.cpp`
  or tighten `BUNNY_PERSONALITY`'s "keep replies SHORT" line
- **voice not expressive enough** → try `Cheyenne-PlayAI`, `Indigo-PlayAI`,
  `Quinn-PlayAI`. tell the personality prompt to use `[whisper]`, `[cheerful]`,
  `[sigh]` tags

## Files

```
bunny_clock/
├── platformio.ini       PlatformIO config
├── include/
│   ├── config.h         all editable settings ← look here first
│   └── bitmaps.h        bunny pixel data (will update once you re-send images)
├── lib/porcupine/       (you create — see step 4 above)
└── src/
    ├── main.cpp         setup/loop + state machine
    ├── display.h/.cpp   OLED
    ├── audio.h/.cpp     I²S mic + speaker
    ├── wake_word.h/.cpp Porcupine + energy fallback
    ├── groq.h/.cpp      STT + LLM + TTS
    └── webserver.h/.cpp phone toggle page
```

## Known limitations / things you may need to fix on real hardware

- HTTPS to Groq uses `setInsecure()`. Fine for personal use; for proper
  security pin the Groq root CA in `groq.cpp`.
- The fallback wake-word detector (energy spike) will false-trigger on any
  loud sound. Install Porcupine for the real wake word.
- Recording uses a fixed 16kHz buffer in PSRAM. If your board lacks PSRAM,
  reduce `MAX_RECORD_SECONDS` and the buffer will go in regular RAM.
- The OLED constructor in `display.cpp` is `SSD1309`. If yours is `SH1106`,
  swap the commented-out line.
- INMP441 needs ~1 second after power-up to be stable. If the very first
  recording is garbage, add `delay(1000)` after `audio_begin()`.
