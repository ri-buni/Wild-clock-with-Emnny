// wake_word.h — Picovoice Porcupine wrapper (with energy-based fallback)
#pragma once
#include <Arduino.h>

// initializes Porcupine. returns false if the SDK isn't linked yet —
// in that case wake_word_process() falls back to a simple energy detector
// so you can test the rest of the pipeline before getting Porcupine working.
bool wake_word_begin();

// feed a 512-sample 16-bit mono frame. returns true if wake word fired.
bool wake_word_process(const int16_t* frame_512);

// Porcupine wants exactly 512 samples per call
#define WAKE_WORD_FRAME_LEN 512
