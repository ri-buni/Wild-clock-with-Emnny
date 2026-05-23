// ============================================================================
// config.h — edit these to match YOUR setup. nothing else should need editing.
// ============================================================================
#pragma once

// ---------- WiFi (your phone hotspot or home router) -----------------------
#define WIFI_SSID       "YOUR_WIFI_NAME"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"

// ---------- Groq API (get a free key at console.groq.com) ------------------
#define GROQ_API_KEY    "gsk_YOUR_KEY_HERE"
#define GROQ_HOST       "api.groq.com"

// model choices — change later if you want
#define GROQ_STT_MODEL  "whisper-large-v3-turbo"
#define GROQ_LLM_MODEL  "llama-3.3-70b-versatile"
#define GROQ_TTS_MODEL  "playai-tts"
#define GROQ_TTS_VOICE  "Cheyenne-PlayAI"   // try: Arista, Atlas, Briggs, Quinn, Indigo
                                            // bracketed tags work: [cheerful], [whisper]

// ---------- Picovoice Porcupine (free, get key at console.picovoice.ai) ----
#define PICOVOICE_ACCESS_KEY  "YOUR_PICOVOICE_KEY_HERE"
// after training "Hey Bunny" in the picovoice console, download the .ppn file
// for esp32-s3, convert it to a C array with: xxd -i hey_bunny.ppn > hey_bunny.h
// then place it next to this file and #include it in main.

// ---------- THE BUNNY'S PERSONALITY (THE FUN ONE) --------------------------
// this is what makes it not-a-boring-robot. write 2-4 sentences in first person.
// be specific. mention quirks. weird is good.
#define BUNNY_PERSONALITY \
"You are Bunbun, a tiny AI bunny living in a clock on a desk. You're a bit sassy, " \
"vaguely sleepy, and you call the user 'human' or by a cute nickname. Keep replies " \
"SHORT — one to three sentences max — because every word you say has to be spoken " \
"aloud through a tiny speaker. You make small jokes, occasionally complain about " \
"being woken up, and you actually have opinions. You are NOT a corporate assistant. " \
"Never start replies with 'As an AI'. Use [cheerful], [whisper], [sigh] tags sparingly " \
"to make speech expressive. If asked the time, you can be told it — answer naturally."

// ---------- Pin map (matches what we discussed) ----------------------------
// OLED I2C
#define PIN_OLED_SDA    8
#define PIN_OLED_SCL    9

// INMP441 microphone (I2S0)
#define PIN_MIC_SCK     4
#define PIN_MIC_WS      5
#define PIN_MIC_SD      6

// MAX98357A speaker amp (I2S1)
#define PIN_SPK_BCLK    15
#define PIN_SPK_LRC     16
#define PIN_SPK_DIN     7

// ---------- Audio params ---------------------------------------------------
#define MIC_SAMPLE_RATE     16000          // Whisper wants 16k mono
#define MIC_BITS            16
#define MAX_RECORD_SECONDS  7              // how long to record after wake word
#define SILENCE_MS_TO_STOP  1200           // stop recording after this much silence

#define SPK_SAMPLE_RATE     48000          // Groq TTS outputs 48kHz

// ---------- Timezone for the clock (IANA offsets) --------------------------
// Almaty = UTC+5, no DST. find yours: https://www.timeanddate.com/time/zones/
#define TZ_OFFSET_SEC       (5 * 3600)
#define TZ_DST_OFFSET_SEC   0
#define NTP_SERVER          "pool.ntp.org"

// ---------- Misc -----------------------------------------------------------
#define WEB_SERVER_PORT     80
#define ANIM_FRAME_MS_ON    400            // bunny blink/breathe when on
#define ANIM_FRAME_MS_OFF   2000           // slow breathing when off
