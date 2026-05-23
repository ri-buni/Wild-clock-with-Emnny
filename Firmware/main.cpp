// ============================================================================
// Bunbun — ESP32-S3 AI clock
// state machine: WIFI -> IDLE(listen) -> RECORDING -> THINKING -> SPEAKING -> IDLE
// phone web page toggles between active (listening) and OFF (clock only).
// ============================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "config.h"
#include "display.h"
#include "audio.h"
#include "wake_word.h"
#include "groq.h"
#include "webserver.h"

enum AppState {
    APP_BOOT,
    APP_WIFI,
    APP_IDLE,           // listening for wake word
    APP_RECORDING,
    APP_THINKING,
    APP_SPEAKING,
    APP_OFF             // toggled off from phone
};
static AppState g_state = APP_BOOT;

// frame buffer for wake-word feeding (Porcupine wants 512 samples)
static int16_t g_ww_frame[WAKE_WORD_FRAME_LEN];

static void enter(AppState s, const char* status_label = nullptr) {
    g_state = s;
    display_set_status_text(status_label);
    switch (s) {
        case APP_IDLE:      display_set_state(BUNNY_ON);        break;
        case APP_RECORDING: display_set_state(BUNNY_LISTENING); break;
        case APP_THINKING:  display_set_state(BUNNY_THINKING);  break;
        case APP_SPEAKING:  display_set_state(BUNNY_SPEAKING);  break;
        case APP_OFF:       display_set_state(BUNNY_OFF);       break;
        default: break;
    }
}

static void connect_wifi() {
    enter(APP_WIFI, "wifi...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("[wifi] connecting");
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 30000) {
        delay(250);
        Serial.print(".");
        display_tick();
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[wifi] %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n[wifi] FAILED — clock will still run");
    }
}

static void sync_time() {
    if (WiFi.status() != WL_CONNECTED) return;
    configTime(TZ_OFFSET_SEC, TZ_DST_OFFSET_SEC, NTP_SERVER);
    Serial.print("[ntp] syncing");
    uint32_t t0 = millis();
    while (time(nullptr) < 100000 && millis() - t0 < 10000) {
        delay(200);
        Serial.print(".");
    }
    Serial.println();
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n=== Bunbun booting ===");

    display_begin();
    audio_begin();
    wake_word_begin();
    connect_wifi();
    sync_time();
    webserver_begin();

    enter(APP_IDLE);
    Serial.println("[main] ready. say the wake word.");
}

void loop() {
    static uint32_t last_display = 0;
    static uint32_t last_history_reset = 0;

    // ------ display tick @ ~30Hz ------
    if (millis() - last_display > 33) {
        display_tick();
        last_display = millis();
    }

    // ------ phone toggle handling ------
    bool want_on = webserver_is_on();
    if (!want_on && g_state != APP_OFF && g_state != APP_BOOT) {
        Serial.println("[main] phone -> OFF");
        enter(APP_OFF);
        groq_llm_reset_history();
    } else if (want_on && g_state == APP_OFF) {
        Serial.println("[main] phone -> ON");
        enter(APP_IDLE);
    }

    // reset conversation context after 5 min idle
    if (g_state == APP_IDLE && millis() - last_history_reset > 5 * 60 * 1000UL) {
        groq_llm_reset_history();
        last_history_reset = millis();
    }

    // ------ state-specific work ------
    switch (g_state) {

        case APP_IDLE: {
            // continuously read 512-sample frames and feed to wake-word detector
            if (!audio_mic_read(g_ww_frame, WAKE_WORD_FRAME_LEN)) break;
            if (wake_word_process(g_ww_frame)) {
                Serial.println("[main] wake word!");
                enter(APP_RECORDING, "listening...");
            }
            break;
        }

        case APP_RECORDING: {
            // record user speech to WAV in PSRAM
            size_t wav_len = 0;
            uint8_t* wav = audio_record_to_wav(&wav_len);
            Serial.printf("[main] recorded %u bytes\n", (unsigned)wav_len);
            if (wav_len < 200) {
                Serial.println("[main] empty recording, back to idle");
                enter(APP_IDLE);
                break;
            }
            enter(APP_THINKING, "thinking...");

            // STT
            String heard = groq_stt_transcribe(wav, wav_len);
            Serial.printf("[stt] heard: \"%s\"\n", heard.c_str());
            if (heard.length() < 2) {
                enter(APP_IDLE);
                break;
            }

            // LLM
            String reply = groq_llm_chat(heard);
            Serial.printf("[llm] reply: \"%s\"\n", reply.c_str());
            if (reply.length() < 1) {
                enter(APP_IDLE);
                break;
            }

            // TTS
            uint8_t* tts_wav = nullptr;
            size_t tts_len = groq_tts_speak(reply, &tts_wav);
            Serial.printf("[tts] got %u bytes\n", (unsigned)tts_len);
            if (tts_len < 100 || !tts_wav) {
                enter(APP_IDLE);
                break;
            }

            // play it
            enter(APP_SPEAKING, "speaking...");
            audio_speaker_play_wav(tts_wav, tts_len);
            last_history_reset = millis();
            enter(APP_IDLE);
            break;
        }

        case APP_OFF: {
            // do nothing AI-wise, just keep showing clock + sleeping bunny
            delay(50);
            break;
        }

        default:
            delay(10);
            break;
    }
}
