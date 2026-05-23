// groq.h — Groq API client (STT + LLM + TTS)
#pragma once
#include <Arduino.h>

// returns transcribed text (caller does not free). empty string on error.
String groq_stt_transcribe(const uint8_t* wav_data, size_t wav_len);

// returns LLM response text. maintains a small conversation history internally.
String groq_llm_chat(const String& user_text);

// generates speech from text. WAV data is written into out_buf in PSRAM.
// returns size in bytes, or 0 on error. caller must NOT free.
size_t groq_tts_speak(const String& text, uint8_t** out_buf);

// reset conversation history (e.g. on long idle)
void groq_llm_reset_history();
